#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n count] [-c count] [file ...]");

HELP_DESCRIPTION_DECL(
    "The head utility writes the first count lines of each file, ten by "
    "default, reading standard input when no file is given. It stops once it "
    "has the lines it needs.");

FLAG(HEAD_LINES, String, 'n', "", "Write the first count lines.");
FLAG(HEAD_BYTES, String, 'c', "", "Write the first count bytes.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Head);

namespace shit {

namespace shitbox {

/* Read from the descriptor until max_lines newlines have arrived or the input
   ends, copying no further. Stopping early matters for an endless producer such
   as yes, since head reading to the end would never return and would keep the
   producer alive. */
static fn read_up_to_lines(os::descriptor fd, i64 max_lines) throws -> String
{
  String out{};
  i64 line_count = 0;
  char buffer[4096];
  while (line_count < max_lines) {
    let const read_count = os::read_fd(fd, buffer, sizeof(buffer));
    if (!read_count.has_value() || *read_count == 0) {
      break;
    }
    for (usize i = 0; i < *read_count && line_count < max_lines; i++) {
      out.push(buffer[i]);
      if (buffer[i] == '\n') line_count++;
    }
  }

  return out;
}

/* Read from the descriptor until max_bytes have arrived or the input ends. The
   byte cap stops an endless producer the way the line cap does. */
static fn read_up_to_bytes(os::descriptor fd, i64 max_bytes) throws -> String
{
  String out{};
  i64 byte_count = 0;
  char buffer[4096];
  while (byte_count < max_bytes) {
    let const read_count = os::read_fd(fd, buffer, sizeof(buffer));
    if (!read_count.has_value() || *read_count == 0) {
      break;
    }
    let const remaining_count = static_cast<usize>(max_bytes - byte_count);
    let const take_count = *read_count < remaining_count ? *read_count
                                                         : remaining_count;
    out.append(StringView{buffer, take_count});
    byte_count += static_cast<i64>(take_count);
  }

  return out;
}

Head::Head() = default;

pure Utility::Kind Head::kind() const wontthrow { return Kind::Head; }

fn Head::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  /* The -c byte count takes precedence over -n when both are given, the way GNU
     head reads the last of the two. */
  let const is_byte_mode = FLAG_HEAD_BYTES.is_set();
  i64 count = 10;
  if (is_byte_mode) {
    let const parsed_value =
        utils::parse_decimal_integer(FLAG_HEAD_BYTES.value());
    if (parsed_value.is_error() || parsed_value.value() < 0) {
      throw Error{"head: invalid byte count '" +
                  String{FLAG_HEAD_BYTES.value()} + "'"};
    }
    count = parsed_value.value();
  } else if (FLAG_HEAD_LINES.is_set()) {
    let const parsed_value =
        utils::parse_decimal_integer(FLAG_HEAD_LINES.value());
    if (parsed_value.is_error() || parsed_value.value() < 0) {
      throw Error{"head: invalid line count '" +
                  String{FLAG_HEAD_LINES.value()} + "'"};
    }
    count = parsed_value.value();
  }

  ArrayList<StringView> sources{};
  if (operands.is_empty())
    sources.push(StringView{"-"});
  else
    for (const String &operand : operands)
      sources.push(operand.view());

  let const should_print_headers = sources.count() > 1;
  i32 status = 0;
  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    /* The input descriptor is read directly so a regular file or a pipe is read
       only as far as the requested lines, rather than slurped whole. */
    os::descriptor fd;
    bool was_opened = false;
    if (sources[source_index] == "-") {
      fd = ec.in_fd.value_or(SHIT_STDIN);
    } else {
      let const opened_fd = os::open_file_descriptor(sources[source_index],
                                                     os::file_open_mode::Read);
      if (!opened_fd.has_value()) {
        report_soft_shitbox_error(ec, cxt,
                                  "head: cannot open '" +
                                      String{sources[source_index]} +
                                      "': " + os::last_system_error_message());
        status = 1;
        continue;
      }
      fd = *opened_fd;
      was_opened = true;
    }

    let const text = is_byte_mode ? read_up_to_bytes(fd, count)
                                  : read_up_to_lines(fd, count);
    if (was_opened) os::close_fd(fd);
    /* A Ctrl-C during the read returns 130 rather than freezing the utility. */
    if (os::INTERRUPT_REQUESTED) return 130;

    let output = String{};
    if (should_print_headers) {
      if (source_index > 0) output += '\n';
      output += "==> ";
      output += sources[source_index];
      output += " <==\n";
    }
    output += text.view();
    ec.print_to_stdout(output);
  }

  return status;
}

} /* namespace shitbox */

} /* namespace shit */
