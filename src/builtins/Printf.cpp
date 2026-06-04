#include "../Builtin.hpp"
#include "../Eval.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Interprets a format string with the common conversions and backslash escapes,
   recycling the format over any remaining arguments, like the POSIX utility. */

namespace shit {

namespace {

void
append_escape(std::string &out, const std::string &fmt, usize &i)
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
append_conversion(std::string &out, const std::string &spec, char conv,
                  const std::string &arg)
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
    std::snprintf(
        buffer, sizeof(buffer), with_ll.c_str(),
        static_cast<long long>(std::strtoll(arg.c_str(), nullptr, 0)));
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

  const std::string &fmt = ec.args()[1];
  std::vector<std::string> operands{ec.args().begin() + 2, ec.args().end()};

  std::string out{};
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

      std::string arg =
          operand_index < operands.size() ? operands[operand_index] : "";
      append_conversion(out, spec, conv, arg);
      operand_index++;
      consumed_a_conversion = true;
    }
  } while (operand_index < operands.size() && consumed_a_conversion);

  ec.print_to_stdout(out);
  return 0;
}

} /* namespace shit */
