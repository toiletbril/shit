#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

/* Interprets a format string with the common conversions and backslash escapes,
   recycling the format over any remaining arguments, like the POSIX utility. */

namespace shit {

namespace {

/* Parse one signed integer argument the way printf does, in base zero. A leading
   0x marks hexadecimal, otherwise the digits are decimal. A malformed argument
   yields zero. */
i64
parse_printf_integer(const String &arg)
{
  usize first_digit = 0;
  if (first_digit < arg.size() &&
      (arg[first_digit] == '+' || arg[first_digit] == '-'))
    first_digit++;
  bool is_hexadecimal = first_digit + 1 < arg.size() &&
                        arg[first_digit] == '0' &&
                        (arg[first_digit + 1] == 'x' ||
                         arg[first_digit + 1] == 'X');
  ErrorOr<i64> parsed = is_hexadecimal ? utils::parse_hexadecimal_integer(arg)
                                       : utils::parse_decimal_integer(arg);
  return parsed.is_error() ? 0 : parsed.value();
}

void
append_escape(String &out, const String &fmt, usize &i)
{
  char e = fmt[i];
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

/* Render one conversion through the C library, so a width or a precision in the
   specification is honored. */
void
append_conversion(String &out, const std::string &spec, char conv,
                  const String &arg)
{
  char buffer[256];
  std::string full = spec + conv;

  if (conv == 's') {
    std::string with_s = spec + 's';
    std::snprintf(buffer, sizeof(buffer), with_s.c_str(), arg.c_str());
    out += buffer;
  } else if (conv == 'c') {
    out += arg.empty() ? '\0' : arg[0];
  } else if (conv == 'd' || conv == 'i') {
    std::string with_ll = spec + "lld";
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<long long>(parse_printf_integer(arg)));
    out += buffer;
  } else if (conv == 'x' || conv == 'X' || conv == 'o' || conv == 'u') {
    std::string with_ll = spec + "ll" + conv;
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<unsigned long long>(
                      std::strtoull(arg.c_str(), nullptr, 0)));
    out += buffer;
  } else {
    /* An unknown conversion is emitted verbatim. */
    out += full;
  }
}

} /* namespace */

Printf::Printf() = default;

Builtin::Kind
Printf::kind() const
{
  return Kind::Printf;
}

i32
Printf::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  if (ec.args().size() < 2) return 0;

  const String &fmt = ec.args()[1];
  ArrayList<String> operands{};
  for (usize i = 2; i < ec.args().size(); i++)
    operands.push(ec.args()[i]);

  String out{};
  usize operand_index = 0;
  bool consumed_a_conversion = false;

  do {
    consumed_a_conversion = false;
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

      /* Collect a conversion specification, the flags, the width, and the
         precision, up to the conversion character. */
      std::string spec = "%";
      i++;
      while (i < fmt.length() && std::strchr("-+ 0#", fmt[i]) != nullptr)
        spec += fmt[i++];
      while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
        spec += fmt[i++];
      if (i < fmt.length() && fmt[i] == '.') {
        spec += fmt[i++];
        while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
          spec += fmt[i++];
      }
      if (i >= fmt.length()) {
        out += spec;
        break;
      }

      char conv = fmt[i];
      if (conv == '%') {
        out += '%';
        continue;
      }

      String arg =
          operand_index < operands.size() ? operands[operand_index] : String{};
      append_conversion(out, spec, conv, arg);
      operand_index++;
      consumed_a_conversion = true;
    }
  } while (operand_index < operands.size() && consumed_a_conversion);

  ec.print_to_stdout(out);
  return 0;
}

} /* namespace shit */
