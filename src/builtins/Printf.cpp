#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-v var] format [argument ...]");

HELP_DESCRIPTION_DECL(
    "The printf builtin writes the arguments under the control of a format "
    "string with the common conversions and backslash escapes, recycling the "
    "format over any remaining arguments. The -v flag stores the result in the "
    "named shell variable instead of writing it to the standard output.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Printf);

namespace shit {

namespace {

pure fn is_hex_digit(char c) wontthrow -> bool;

struct printf_number
{
  i64 value;
  bool is_valid;
  bool is_hex;
};

/* is_valid is false when a byte follows the number, matching bash. An argument
   that opens with a quote yields the next byte's code. */
fn parse_printf_number(const String &arg) throws -> printf_number
{
  if (!arg.is_empty() && (arg[0] == '\'' || arg[0] == '"')) {
    return {arg.count() > 1 ? static_cast<unsigned char>(arg[1]) : 0, true,
            false};
  }

  usize i = 0;
  while (i < arg.count() && (arg[i] == ' ' || arg[i] == '\t'))
    i++;
  let const number_start = i;
  if (i < arg.count() && (arg[i] == '+' || arg[i] == '-')) i++;

  let const is_hexadecimal = i + 1 < arg.count() && arg[i] == '0' &&
                             (arg[i + 1] == 'x' || arg[i + 1] == 'X');
  let const is_octal = !is_hexadecimal && i < arg.count() && arg[i] == '0' &&
                       i + 1 < arg.count();
  usize digit_start = i;
  if (is_hexadecimal) {
    i += 2;
    digit_start = i;
    while (i < arg.count() && is_hex_digit(arg[i]))
      i++;
  } else if (is_octal) {
    while (i < arg.count() && arg[i] >= '0' && arg[i] <= '7')
      i++;
  } else {
    while (i < arg.count() && arg[i] >= '0' && arg[i] <= '9')
      i++;
  }
  let const number_end = i;

  let const number_text =
      arg.view().substring_of_length(number_start, number_end - number_start);
  let const parsed =
      is_hexadecimal ? utils::parse_integer_in_base(number_text, int_base::hex)
      : is_octal ? utils::parse_integer_in_base(number_text, int_base::octal)
                 : number_text.to<i64>();
  let const has_digits = number_end > digit_start;
  return {parsed.is_error() ? 0 : parsed.value(),
          has_digits && number_end == arg.count(), is_hexadecimal};
}

fn parse_printf_integer(const String &arg) throws -> i64
{
  return parse_printf_number(arg).value;
}

pure fn is_hex_digit(char c) wontthrow -> bool
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

pure fn hex_digit_value(char c) wontthrow -> i32
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

fn append_escape(String &out, const String &fmt, usize &i) throws -> void
{
  ASSERT(i < fmt.length());

  let const e = fmt[i];

  /* The index is left on the last digit consumed, the caller advances past it.
   */
  if (e >= '0' && e <= '7') {
    i32 value = e - '0';
    usize digit_count = 1;
    while (digit_count < 3 && i + 1 < fmt.length() && fmt[i + 1] >= '0' &&
           fmt[i + 1] <= '7')
    {
      i++;
      value = value * 8 + (fmt[i] - '0');
      digit_count++;
    }
    out += static_cast<char>(value);
    return;
  }

  if (e == 'x' && i + 1 < fmt.length() && is_hex_digit(fmt[i + 1])) {
    i32 value = 0;
    usize digit_count = 0;
    while (digit_count < 2 && i + 1 < fmt.length() && is_hex_digit(fmt[i + 1]))
    {
      i++;
      value = value * 16 + hex_digit_value(fmt[i]);
      digit_count++;
    }
    out += static_cast<char>(value);
    return;
  }

  switch (e) {
  case 'n': out += '\n'; break;
  case 't': out += '\t'; break;
  case 'r': out += '\r'; break;
  case 'a': out += '\a'; break;
  case 'b': out += '\b'; break;
  case 'f': out += '\f'; break;
  case 'v': out += '\v'; break;
  case '\\': out += '\\'; break;
  default:
    out += '\\';
    out += e;
    break;
  }
}

/* Returns true when a \c was seen so the caller can abort the whole printf. */
fn append_b_argument(String &out, const String &arg) throws -> bool
{
  for (usize i = 0; i < arg.length(); i++) {
    if (arg[i] != '\\' || i + 1 >= arg.length()) {
      out += arg[i];
      continue;
    }
    let const e = arg[i + 1];
    if (e == 'c') return true;
    if (e == 'x' && i + 2 < arg.length() && is_hex_digit(arg[i + 2])) {
      usize digit_index = i + 2;
      i32 value = 0;
      usize digit_count = 0;
      while (digit_count < 2 && digit_index < arg.length() &&
             is_hex_digit(arg[digit_index]))
      {
        value = value * 16 + hex_digit_value(arg[digit_index]);
        digit_index++;
        digit_count++;
      }
      out += static_cast<char>(value);
      i = digit_index - 1;
      continue;
    }
    if (e == '0' || (e >= '1' && e <= '7')) {
      /* A leading zero does not count toward the three octal digits. */
      usize digit_index = i + 1;
      if (arg[digit_index] == '0') digit_index++;
      i32 value = 0;
      usize digit_count = 0;
      while (digit_count < 3 && digit_index < arg.length() &&
             arg[digit_index] >= '0' && arg[digit_index] <= '7')
      {
        value = value * 8 + (arg[digit_index] - '0');
        digit_index++;
        digit_count++;
      }
      out += static_cast<char>(value);
      i = digit_index - 1;
      continue;
    }
    i++;
    switch (e) {
    case 'n': out += '\n'; break;
    case 't': out += '\t'; break;
    case 'r': out += '\r'; break;
    case 'a': out += '\a'; break;
    case 'b': out += '\b'; break;
    case 'f': out += '\f'; break;
    case 'v': out += '\v'; break;
    case '\\': out += '\\'; break;
    default:
      out += '\\';
      out += e;
      break;
    }
  }
  return false;
}

fn is_q_safe_byte(char c) throws -> bool
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
         c == '/' || c == ':' || c == '%' || c == '+' || c == '@' || c == '=';
}

fn append_q_argument(String &out, const String &arg) throws -> void
{
  if (utils::append_ansi_c_quote_if_needed(out, arg.view())) return;

  for (usize i = 0; i < arg.count(); i++) {
    const char c = arg[i];
    if (!is_q_safe_byte(c)) out += '\\';
    out += c;
  }
}

fn report_invalid_number(ExecContext &ec, const String &arg, bool is_hex,
                         i32 &exit_status, Allocator allocator) throws -> void
{
  let message = String{allocator, "shit: printf: "};
  message += arg.view();
  message += is_hex ? ": invalid hex number\n" : ": invalid number\n";
  ec.print_to_stderr(message.view());
  exit_status = 1;
}

fn append_conversion(String &out, const String &spec, char conv,
                     const String &arg, ExecContext &ec, i32 &exit_status,
                     Allocator allocator) throws -> void
{
  char buffer[256];

  switch (conv) {
  case 'q': append_q_argument(out, arg); break;
  case 's': {
    String with_s = spec.clone();
    with_s.push('s');
    /* A string too long for the stack buffer is rewritten into a heap buffer
       sized from the length snprintf reports. */
    const int needed =
        std::snprintf(buffer, sizeof(buffer), with_s.c_str(), arg.c_str());
    if (needed >= 0 && static_cast<usize>(needed) < sizeof(buffer)) {
      out += buffer;
    } else if (needed > 0) {
      const usize size = static_cast<usize>(needed) + 1;
      char *const big = static_cast<char *>(std::malloc(size));
      if (big != nullptr) {
        std::snprintf(big, size, with_s.c_str(), arg.c_str());
        out += StringView{big, static_cast<usize>(needed)};
        std::free(big);
      }
    }
  } break;
  case 'c': out += arg.is_empty() ? '\0' : arg[0]; break;
  case 'd':
  case 'i': {
    let const number = parse_printf_number(arg);
    if (!number.is_valid)
      report_invalid_number(ec, arg, number.is_hex, exit_status, allocator);
    let const with_ll = spec + "lld";
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<long long>(number.value));
    out += buffer;
  } break;
  case 'x':
  case 'X':
  case 'o':
  case 'u': {
    let const number = parse_printf_number(arg);
    if (!number.is_valid)
      report_invalid_number(ec, arg, number.is_hex, exit_status, allocator);
    String with_ll = spec + "ll";
    with_ll.push(conv);
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<unsigned long long>(number.value));
    out += buffer;
  } break;
  case 'f':
  case 'e':
  case 'E':
  case 'g':
  case 'G': {
    String with_conv = spec.clone();
    with_conv.push(conv);
    const double value = std::strtod(arg.c_str(), nullptr);
    std::snprintf(buffer, sizeof(buffer), with_conv.c_str(), value);
    out += buffer;
  } break;
  default:
    out += spec;
    out += conv;
    break;
  }
}

} // namespace

Printf::Printf() = default;

pure fn Printf::kind() const wontthrow -> Builtin::Kind { return Kind::Printf; }

fn Printf::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (ec.args().count() < 2) return 0;

  LOG(All, "printf formatting %zu arguments", ec.args().count() - 1);

  /* bash printf -v NAME stores the result in NAME, riding every mood but POSIX.
   */
  usize format_index = 1;
  Maybe<String> store_variable;
  if (cxt.bash_additions_enabled() && ec.args()[1] == "-v" &&
      ec.args().count() >= 3)
  {
    store_variable = ec.args()[2];
    format_index = 3;
  }

  if (format_index < ec.args().count() && ec.args()[format_index] == "--") {
    format_index++;
  }

  if (format_index >= ec.args().count()) return 0;

  let const &args = ec.args();
  let const &fmt = args[format_index];
  let const operand_base = format_index + 1;
  let const operand_count = args.count() - operand_base;
  let const empty_operand = String{cxt.scratch_allocator()};
  let do_operand_at = [&](usize index) wontthrow -> const String & {
    return index < operand_count ? args[operand_base + index] : empty_operand;
  };

  let out = String{cxt.scratch_allocator()};
  i32 exit_status = 0;
  usize operand_index = 0;
  bool has_consumed_a_conversion = false;
  bool should_stop = false;

  let do_consume_star = [&](String &spec) throws {
    spec.append(String::from(parse_printf_integer(do_operand_at(operand_index)),
                             cxt.scratch_allocator())
                    .view());
    operand_index++;
    has_consumed_a_conversion = true;
  };

  do {
    has_consumed_a_conversion = false;
    for (usize i = 0; i < fmt.length(); i++) {
      if (fmt[i] == '\\' && i + 1 < fmt.length()) {
        i++;
        append_escape(out, fmt, i);
        continue;
      }
      if (fmt[i] != '%') {
        out += fmt[i];
        continue;
      }

      String spec{cxt.scratch_allocator(), "%"};
      i++;

      while (i < fmt.length() && std::strchr("-+ 0#", fmt[i]) != nullptr)
        spec.push(fmt[i++]);
      if (i < fmt.length() && fmt[i] == '*') {
        do_consume_star(spec);
        i++;
      } else {
        while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
          spec.push(fmt[i++]);
      }
      if (i < fmt.length() && fmt[i] == '.') {
        spec.push(fmt[i++]);
        if (i < fmt.length() && fmt[i] == '*') {
          do_consume_star(spec);
          i++;
        } else {
          while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
            spec.push(fmt[i++]);
        }
      }

      /* bash %(datefmt)T formats a time. The format sits in parentheses where
         the conversion letter would be, followed by T, and the operand is the
         epoch seconds, -1 for now and -2 for the shell start. */
      if (cxt.bash_additions_enabled() && i < fmt.length() && fmt[i] == '(') {
        usize close = i + 1;
        while (close < fmt.length() && fmt[close] != ')')
          close++;
        if (close + 1 < fmt.length() && fmt[close + 1] == 'T') {
          let const date_format =
              fmt.view().substring_of_length(i + 1, close - i - 1);
          let const has_operand = operand_index < operand_count;
          let const epoch =
              has_operand ? parse_printf_integer(do_operand_at(operand_index))
                          : -1;
          let const formatted = os::format_local_time(date_format, epoch);
          if (spec == "%")
            out += formatted;
          else
            append_conversion(out, spec, 's', formatted, ec, exit_status,
                              cxt.scratch_allocator());
          operand_index++;
          has_consumed_a_conversion = true;
          i = close + 1;
          continue;
        }
      }
      if (i >= fmt.length()) {
        out += spec;
        break;
      }

      let const conv = fmt[i];
      if (conv == '%') {
        out += '%';
        continue;
      }
      /* A '(' that did not open a valid %(fmt)T is emitted literally and
         consumes no operand. */
      if (conv == '(') {
        out += spec;
        out += '(';
        continue;
      }

      let const &arg = do_operand_at(operand_index);
      if (conv == 'b') {
        should_stop = append_b_argument(out, arg);
        operand_index++;
        has_consumed_a_conversion = true;
        if (should_stop) break;
        continue;
      }
      append_conversion(out, spec, conv, arg, ec, exit_status,
                        cxt.scratch_allocator());
      operand_index++;
      has_consumed_a_conversion = true;
    }
  } while (!should_stop && operand_index < operand_count &&
           has_consumed_a_conversion);

  if (store_variable.has_value()) {
    let const target = store_variable->view();
    let const open_bracket = target.find_character('[');
    if (open_bracket.has_value() && *open_bracket > 0 &&
        target.length >= *open_bracket + 2 && target[target.length - 1] == ']')
    {
      let const array_name = target.substring_of_length(0, *open_bracket);
      let const subscript = target.substring_of_length(
          *open_bracket + 1, target.length - *open_bracket - 2);
      cxt.assign_array_element(array_name, subscript, out.view(), false);
      return exit_status;
    }
    cxt.set_shell_variable(target, out.view());
    return exit_status;
  }

  ec.print_to_stdout(out);
  return exit_status;
}

} // namespace shit
