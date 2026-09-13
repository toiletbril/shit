/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the od utility. It applies byte ranges and address
 * bases, renders selected numeric or character formats, and folds repeated
 * output rows.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-v] [-A base] [-j skip] [-N count] [-t type]... "
                   "[file ...]");

HELP_DESCRIPTION_DECL("The od utility writes formatted file bytes.");

FLAG(OD_ADDRESS, String, 'A', "address-radix", "Use d, o, x, or n addresses.");
FLAG(OD_SKIP, String, 'j', "skip-bytes", "Skip this many input bytes.");
FLAG(OD_COUNT, String, 'N', "read-bytes", "Read at most this many bytes.");
FLAG(OD_TYPE, ManyStrings, 't', "format", "Add an output type.");
FLAG(OD_VERBOSE, Bool, 'v', "output-duplicates",
     "Write every repeated group of input data.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Od);

namespace koshka::koshkit {

static fn append_od_padded(String &output, u64 value, usize width_columns,
                           int_base base) throws -> void
{
  let const digits =
      String::from_in_base(value, false, base, output.allocator());
  if (digits.length() < width_columns)
    output.append_repeated('0', width_columns - digits.length());

  output += digits.view();
}

static fn append_od_character(String &output, u8 byte) throws -> void
{
  static constexpr StringView NAMES[8] = {"nul", "soh", "stx", "etx",
                                          "eot", "enq", "ack", "bel"};

  switch (byte) {
  case 0:
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7: output += NAMES[byte]; return;

  case '\b': output += " \\b"; return;
  case '\t': output += " \\t"; return;
  case '\n': output += " \\n"; return;
  case '\f': output += " \\f"; return;
  case '\r': output += " \\r"; return;

  default: break;
  }

  if (byte >= 0x20 && byte <= 0x7e) {
    output += "  ";
    output += static_cast<char>(byte);
    return;
  }

  append_od_padded(output, byte, 3, int_base::octal);
}

static fn od_base(char radix) wontthrow -> int_base
{
  switch (radix) {
  case 'x': return int_base::hex;
  case 'd': return int_base::decimal;
  default: return int_base::octal;
  }
}

struct od_format
{
  int_base base;
  usize unit_size_bytes;
  usize width_columns;
  bool is_character;
};

Od::Od() = default;

pure fn Od::kind() const wontthrow -> Utility::Kind { return Kind::Od; }

fn Od::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const operands =
      parse_util_operands(FLAG_LIST, args, &arg_locations, &operand_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  char address_radix = 'o';
  if (FLAG_OD_ADDRESS.is_set()) {
    let const radix_value = FLAG_OD_ADDRESS.value();
    let is_valid_radix = false;

    if (radix_value.length == 1) {
      switch (radix_value[0]) {
      case 'd':
      case 'o':
      case 'x':
      case 'n': is_valid_radix = true; break;

      default: break;
      }
    }

    if (!is_valid_radix) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_OD_ADDRESS.value_location(),
                              "the address radix must be d, o, x, or n");
      return 1;
    }

    address_radix = radix_value[0];
  }

  u64 skip_count = 0;
  if (FLAG_OD_SKIP.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_OD_SKIP.value());
    if (parsed.is_error()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_OD_SKIP.value_location(),
                              "invalid skip count '" + FLAG_OD_SKIP.value() +
                                  "'",
                              "use a nonnegative decimal byte count");
      return 1;
    }

    skip_count = parsed.value();
  }
  u64 byte_limit = UINT64_MAX;
  if (FLAG_OD_COUNT.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_OD_COUNT.value());
    if (parsed.is_error()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_OD_COUNT.value_location(),
                              "invalid byte count '" + FLAG_OD_COUNT.value() +
                                  "'",
                              "use a nonnegative decimal byte count");
      return 1;
    }

    byte_limit = parsed.value();
  }

  let input_operands = ArrayList<String>{cxt.scratch_allocator()};
  let operand_count = operands.count();
  let const has_legacy_blocking_option =
      FLAG_OD_ADDRESS.is_set() || FLAG_OD_SKIP.is_set() ||
      FLAG_OD_COUNT.is_set() || !FLAG_OD_TYPE.is_empty() ||
      FLAG_OD_VERBOSE.is_enabled();
  let const has_legacy_plus_offset = !operands.is_empty() &&
                                     !operands.back().is_empty() &&
                                     operands.back()[0] == '+';
  let const has_legacy_numeric_second_operand =
      operands.count() == 2 && !operands.back().is_empty() &&
      operands.back()[0] >= '0' && operands.back()[0] <= '9';
  if (operand_count <= 2 && !has_legacy_blocking_option &&
      (has_legacy_plus_offset || has_legacy_numeric_second_operand))
  {
    let offset = operands.back().view();
    if (has_legacy_plus_offset) offset = offset.substring(1);
    u64 multiplier = 1;
    if (!offset.is_empty() && offset[offset.length - 1] == 'b') {
      multiplier = 512;
      offset = offset.substring_of_length(0, offset.length - 1);
    }
    let const is_decimal =
        !offset.is_empty() && offset[offset.length - 1] == '.';
    if (is_decimal) offset = offset.substring_of_length(0, offset.length - 1);
    let const parsed = utils::parse_integer_in_base_u64(
        offset, is_decimal ? int_base::decimal : int_base::octal);
    if (parsed.is_error() || parsed.value() > UINT64_MAX / multiplier) {
      KOSHKIT_REPORT_ERROR_AT(
          operand_locations[operand_count - 1],
          "invalid legacy offset '" + operands.back() + "'",
          "use [+]OCTAL, [+]DECIMAL., or either form followed by b");
      return 1;
    }
    skip_count = parsed.value() * multiplier;
    operand_count--;
  }
  for (usize index = 0; index < operand_count; index++)
    input_operands.push(operands[index].clone());

  let input_bytes = String{cxt.scratch_allocator()};
  let const sources =
      source_list_from_operands(input_operands, cxt.scratch_allocator());
  i32 status = 0;
  for (let const source : sources) {
    let const content = read_named_or_stdin(ec, source);
    if (!content.has_value()) {
      report_soft_koshkit_error(ec, cxt,
                                "od: cannot read '" +
                                    String{cxt.scratch_allocator(), source} +
                                    "': " + os::last_system_error_message());
      status = 1;
      continue;
    }
    input_bytes += content->view();
  }

  let const first = skip_count < input_bytes.length()
                        ? static_cast<usize>(skip_count)
                        : input_bytes.length();
  let available = input_bytes.length() - first;
  if (byte_limit < available) available = static_cast<usize>(byte_limit);
  let const bytes = input_bytes.view().substring_of_length(first, available);
  let output = String{cxt.scratch_allocator()};
  let const type_count = FLAG_OD_TYPE.count();
  let const format_count = type_count == 0 ? 1 : type_count;
  let formats = ArrayList<od_format>{cxt.scratch_allocator()};
  formats.reserve(format_count);

  for (usize format_index = 0; format_index < format_count; format_index++) {
    let const format =
        type_count == 0 ? StringView{"o2"} : FLAG_OD_TYPE.get(format_index);
    if (format == "c") {
      formats.push(od_format{int_base::octal, 1, 0, true});
      continue;
    }

    let const format_location = type_count == 0
                                    ? SourceLocation{}
                                    : FLAG_OD_TYPE.get_location(format_index);
    if (format.is_empty() || format.length > 2) {
      KOSHKIT_REPORT_ERROR_AT(
          format_location, "unsupported output type '" + String{format} + "'",
          "use c, d, o, u, or x with an optional width of 1, 2, 4, or 8");
      return 1;
    }

    let const radix = format[0];
    usize unit_size_bytes = 2;
    if (format.length == 2) {
      switch (format[1]) {
      case '1': unit_size_bytes = 1; break;
      case '2': break;
      case '4': unit_size_bytes = 4; break;
      case '8': unit_size_bytes = 8; break;

      default:
        KOSHKIT_REPORT_ERROR_AT(format_location,
                                "unsupported integer width in '" +
                                    String{format} + "'",
                                "use a width of 1, 2, 4, or 8");
        return 1;
      }
    }

    int_base base = int_base::decimal;
    usize width_columns = unit_size_bytes * 3;
    switch (radix) {
    case 'x':
      base = int_base::hex;
      width_columns = unit_size_bytes * 2;
      break;

    case 'o':
      base = int_base::octal;
      width_columns = (unit_size_bytes * 8 + 2) / 3;
      break;

    case 'd':
    case 'u': break;

    default: {
      KOSHKIT_REPORT_ERROR_AT(
          format_location, "unsupported output type '" + String{format} + "'",
          "use c, d, o, u, or x with an optional width of 1, 2, 4, or 8");
      return 1;
    }
    }

    formats.push(od_format{base, unit_size_bytes, width_columns, false});
  }

  let const address_base = od_base(address_radix);
  let const should_print_address = address_radix != 'n';
  let const is_verbose = FLAG_OD_VERBOSE.is_enabled();
  StringView previous_row;
  bool did_suppress = false;

  for (usize row_start = 0; row_start < bytes.length; row_start += 16) {
    let const row_length =
        bytes.length - row_start < 16 ? bytes.length - row_start : 16;
    let const row = bytes.substring_of_length(row_start, row_length);
    if (!is_verbose && row == previous_row) {
      if (!did_suppress) output += "*\n";
      did_suppress = true;
      continue;
    }

    previous_row = row;
    did_suppress = false;

    for (usize format_index = 0; format_index < format_count; format_index++) {
      if (should_print_address && format_index == 0) {
        append_od_padded(output, first + row_start, 7, address_base);
      } else {
        output += "       ";
      }

      let const &format = formats[format_index];
      if (format.is_character) {
        for (usize position = 0; position < row_length; position++) {
          output += ' ';
          append_od_character(output,
                              static_cast<u8>(bytes[row_start + position]));
        }
      } else {
        for (usize position = 0; position < row_length;
             position += format.unit_size_bytes)
        {
          u64 value = 0;
          let const current_size =
              row_length - position < format.unit_size_bytes
                  ? row_length - position
                  : format.unit_size_bytes;

          for (usize byte_index = 0; byte_index < current_size; byte_index++)
            value |= static_cast<u64>(static_cast<u8>(
                         bytes[row_start + position + byte_index]))
                     << (byte_index * 8);

          output += ' ';
          append_od_padded(output, value, format.width_columns, format.base);
        }
      }

      output += '\n';
    }
  }

  if (should_print_address) {
    append_od_padded(output, first + bytes.length, 7, address_base);
    output += '\n';
  }
  ec.print_to_stdout(output);
  return status;
}

} // namespace koshka::koshkit
