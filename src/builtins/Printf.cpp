#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Interprets a format string with the common conversions and backslash escapes,
   recycling the format over any remaining arguments, like the POSIX utility. */

namespace shit {

namespace {

/* Parse one signed integer argument the way printf does, in base zero. A
   leading 0x marks hexadecimal, otherwise the digits are decimal. An argument
   that opens with a single or a double quote yields the char code of the byte
   that follows, or zero when nothing follows, as POSIX specifies. A malformed
   argument yields zero. */
i64 parse_printf_integer(const String &arg) throws
{
  if (!arg.is_empty() && (arg[0] == '\'' || arg[0] == '"'))
    return arg.count() > 1 ? static_cast<unsigned char>(arg[1]) : 0;

  usize first_digit = 0;
  if (first_digit < arg.count() &&
      (arg[first_digit] == '+' || arg[first_digit] == '-'))
    first_digit++;
  let const is_hexadecimal =
      first_digit + 1 < arg.count() && arg[first_digit] == '0' &&
      (arg[first_digit + 1] == 'x' || arg[first_digit + 1] == 'X');
  /* A leading zero that is not 0x marks octal, as the C base-zero strtoll the
     old code used did, so printf '%d' 010 yields 8. */
  let const is_octal = !is_hexadecimal && first_digit < arg.count() &&
                       arg[first_digit] == '0' && first_digit + 1 < arg.count();
  let const parsed = is_hexadecimal ? utils::parse_hexadecimal_integer(arg)
                     : is_octal     ? utils::parse_octal_integer(arg)
                                    : utils::parse_decimal_integer(arg);
  return parsed.is_error() ? 0 : parsed.value();
}

void append_escape(String &out, const String &fmt, usize &i) throws
{
  ASSERT(i < fmt.length());

  let const e = fmt[i];
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
void append_conversion(String &out, const String &spec, char conv,
                       const String &arg) throws
{
  char buffer[256];

  if (conv == 's') {
    String with_s = spec;
    with_s.push('s');
    /* A %s argument can be arbitrarily long, so a fixed buffer would truncate
       it. The first write into the stack buffer serves the common short string,
       and snprintf reports the length it needed, so a longer one writes into a
       heap buffer sized to fit. */
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
  } else if (conv == 'c') {
    out += arg.is_empty() ? '\0' : arg[0];
  } else if (conv == 'd' || conv == 'i') {
    let const with_ll = spec + "lld";
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<long long>(parse_printf_integer(arg)));
    out += buffer;
  } else if (conv == 'x' || conv == 'X' || conv == 'o' || conv == 'u') {
    String with_ll = spec + "ll";
    with_ll.push(conv);
    /* The unsigned conversions share the char-code and base parsing with the
       signed ones, so printf '%x' "'A" yields the char code the same way. */
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<unsigned long long>(parse_printf_integer(arg)));
    out += buffer;
  } else {
    /* An unknown conversion is emitted verbatim. */
    out += spec;
    out += conv;
  }
}

} /* namespace */

Printf::Printf() = default;

pure Builtin::Kind Printf::kind() const wontthrow { return Kind::Printf; }

i32 Printf::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  ASSERT(!ec.args().is_empty());

  if (ec.args().count() < 2) return 0;

  let const &fmt = ec.args()[1];
  ArrayList<String> operands{};
  for (usize i = 2; i < ec.args().count(); i++)
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

      String spec = "%";
      i++;
      while (i < fmt.length() && std::strchr("-+ 0#", fmt[i]) != nullptr)
        spec.push(fmt[i++]);
      while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
        spec.push(fmt[i++]);
      if (i < fmt.length() && fmt[i] == '.') {
        spec.push(fmt[i++]);
        while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
          spec.push(fmt[i++]);
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

      let const arg =
          operand_index < operands.count() ? operands[operand_index] : String{};
      append_conversion(out, spec, conv, arg);
      operand_index++;
      consumed_a_conversion = true;
    }
  } while (operand_index < operands.count() && consumed_a_conversion);

  ec.print_to_stdout(out);
  return 0;
}

} /* namespace shit */
