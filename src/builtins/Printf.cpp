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

/* Expand the backslash escapes in a %b argument, the same set the format string
   itself takes plus an octal \ooo with an optional leading zero and a \c that
   stops all further output. Returns true when a \c was seen so the caller can
   abort the whole printf, matching the POSIX utility. */
bool append_b_argument(String &out, const String &arg) throws
{
  for (usize i = 0; i < arg.length(); i++) {
    if (arg[i] != '\\' || i + 1 >= arg.length()) {
      out += arg[i];
      continue;
    }
    let const e = arg[i + 1];
    if (e == 'c') return true;
    if (e == '0' || (e >= '1' && e <= '7')) {
      /* An octal escape takes up to three octal digits, after an optional
         leading zero that does not count toward the three. */
      usize digit_index = i + 1;
      if (arg[digit_index] == '0') digit_index++;
      i32 value = 0;
      usize digits_read = 0;
      while (digits_read < 3 && digit_index < arg.length() &&
             arg[digit_index] >= '0' && arg[digit_index] <= '7')
      {
        value = value * 8 + (arg[digit_index] - '0');
        digit_index++;
        digits_read++;
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
  } else if (conv == 'f' || conv == 'e' || conv == 'E' || conv == 'g' ||
             conv == 'G')
  {
    /* The float conversions parse the argument as a double through strtod, the
       way the C printf renders it, so the width and the precision in the spec
       are honored. A malformed argument parses as zero. */
    String with_conv = spec;
    with_conv.push(conv);
    const double value = std::strtod(arg.c_str(), nullptr);
    std::snprintf(buffer, sizeof(buffer), with_conv.c_str(), value);
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
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() < 2) return 0;

  /* bash printf -v NAME stores the result in the named variable instead of
     printing it, so the format and operands shift two places past -v NAME. */
  usize format_index = 1;
  Maybe<String> store_variable;
  if (cxt.is_bash_compatible() && ec.args()[1] == "-v" &&
      ec.args().count() >= 3)
  {
    store_variable = ec.args()[2];
    format_index = 3;
  }

  if (format_index >= ec.args().count()) return 0;

  let const &fmt = ec.args()[format_index];
  ArrayList<String> operands{};
  for (usize i = format_index + 1; i < ec.args().count(); i++)
    operands.push(ec.args()[i]);

  String out{};
  usize operand_index = 0;
  bool consumed_a_conversion = false;
  bool should_stop = false;

  /* Read the next operand as the integer value of a * field width or precision,
     append its decimal text into the spec, and advance the operand cursor. A
     missing operand reads as zero, like an absent argument elsewhere. */
  auto consume_star = [&](String &spec) throws {
    let const star_arg =
        operand_index < operands.count() ? operands[operand_index] : String{};
    spec.append(utils::int_to_text(parse_printf_integer(star_arg)).view());
    operand_index++;
    consumed_a_conversion = true;
  };

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
      if (i < fmt.length() && fmt[i] == '*') {
        consume_star(spec);
        i++;
      } else {
        while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
          spec.push(fmt[i++]);
      }
      if (i < fmt.length() && fmt[i] == '.') {
        spec.push(fmt[i++]);
        if (i < fmt.length() && fmt[i] == '*') {
          consume_star(spec);
          i++;
        } else {
          while (i < fmt.length() && fmt[i] >= '0' && fmt[i] <= '9')
            spec.push(fmt[i++]);
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

      let const arg =
          operand_index < operands.count() ? operands[operand_index] : String{};
      if (conv == 'b') {
        /* %b prints the argument with its backslash escapes expanded, and a \c
           inside it stops the whole printf. */
        should_stop = append_b_argument(out, arg);
        operand_index++;
        consumed_a_conversion = true;
        if (should_stop) break;
        continue;
      }
      append_conversion(out, spec, conv, arg);
      operand_index++;
      consumed_a_conversion = true;
    }
  } while (!should_stop && operand_index < operands.count() &&
           consumed_a_conversion);

  if (store_variable.has_value()) {
    cxt.set_shell_variable(store_variable->view(), out.view());
    return 0;
  }

  ec.print_to_stdout(out);
  return 0;
}

} /* namespace shit */
