#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Interprets a format string with the common conversions and backslash escapes,
   recycling the format over any remaining arguments, like the POSIX utility. */

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

void append_escape(String &out, const String &fmt, usize &i) throws
{
  ASSERT(i < fmt.length());

  let const e = fmt[i];

  /* A format-string octal escape is the byte for up to three octal digits, so
     \0 is a NUL and \101 is an A, the way bash and POSIX printf read it. The
     index is left on the last digit consumed since the caller advances past it.
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

  /* A \xHH escape is the byte for up to two hexadecimal digits, the bash
     extension over POSIX. */
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

/* Expand the backslash escapes in a %b argument, the same set the format string
   itself takes plus a \c that stops all further output. The octal form here
   allows an optional leading zero that does not count toward the three digits.
   Returns true when a \c was seen so the caller can abort the whole printf,
   matching the POSIX utility. */
bool append_b_argument(String &out, const String &arg) throws
{
  for (usize i = 0; i < arg.length(); i++) {
    if (arg[i] != '\\' || i + 1 >= arg.length()) {
      out += arg[i];
      continue;
    }
    let const e = arg[i + 1];
    if (e == 'c') return true;
    if (e == 'x' && i + 2 < arg.length() && is_hex_digit(arg[i + 2])) {
      /* A \xHH escape takes up to two hexadecimal digits, the bash extension.
       */
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
      /* An octal escape takes up to three octal digits, after an optional
         leading zero that does not count toward the three. */
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

/* A byte that needs no quoting outside a quoted span, so a word of only these
   reuses as shell input unchanged. */
bool is_q_safe_byte(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
         c == '/' || c == ':' || c == '%' || c == '+' || c == '@' || c == '=';
}

/* Quote the argument so it reads back as one shell word, the way bash %q does.
   An empty argument becomes '', a string with a control byte becomes the
   $'...' form so the byte survives, and any other special byte is
   backslash-escaped. */
void append_q_argument(String &out, const String &arg) throws
{
  if (arg.is_empty()) {
    out += "''";
    return;
  }

  bool has_control_byte = false;
  for (usize i = 0; i < arg.count(); i++) {
    const unsigned char byte = static_cast<unsigned char>(arg[i]);
    if (byte < 0x20 || byte == 0x7f) {
      has_control_byte = true;
      break;
    }
  }

  if (has_control_byte) {
    out += "$'";
    for (usize i = 0; i < arg.count(); i++) {
      const char c = arg[i];
      switch (c) {
      case '\a': out += "\\a"; break;
      case '\b': out += "\\b"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\v': out += "\\v"; break;
      case '\f': out += "\\f"; break;
      case '\r': out += "\\r"; break;
      case '\'': out += "\\'"; break;
      case '\\': out += "\\\\"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20 ||
            static_cast<unsigned char>(c) == 0x7f)
        {
          char octal[8];
          std::snprintf(
              octal, sizeof(octal), "\\%03o",
              static_cast<unsigned int>(static_cast<unsigned char>(c)));
          out += octal;
        } else {
          out += c;
        }
        break;
      }
    }
    out += "'";
    return;
  }

  for (usize i = 0; i < arg.count(); i++) {
    const char c = arg[i];
    if (!is_q_safe_byte(c)) out += '\\';
    out += c;
  }
}

/* Render one conversion through the C library, so a width or a precision in the
   specification is honored. */
void append_conversion(String &out, const String &spec, char conv,
                       const String &arg) throws
{
  char buffer[256];

  switch (conv) {
  case 'q': append_q_argument(out, arg); break;
  case 's': {
    String with_s = spec.clone();
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
  } break;
  case 'c': out += arg.is_empty() ? '\0' : arg[0]; break;
  case 'd':
  case 'i': {
    let const with_ll = spec + "lld";
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<long long>(parse_printf_integer(arg)));
    out += buffer;
  } break;
  case 'x':
  case 'X':
  case 'o':
  case 'u': {
    String with_ll = spec + "ll";
    with_ll.push(conv);
    /* The unsigned conversions share the char-code and base parsing with the
       signed ones, so printf '%x' "'A" yields the char code the same way. */
    std::snprintf(buffer, sizeof(buffer), with_ll.c_str(),
                  static_cast<unsigned long long>(parse_printf_integer(arg)));
    out += buffer;
  } break;
  case 'f':
  case 'e':
  case 'E':
  case 'g':
  case 'G': {
    /* The float conversions parse the argument as a double through strtod, the
       way the C printf renders it, so the width and the precision in the spec
       are honored. A malformed argument parses as zero. */
    String with_conv = spec.clone();
    with_conv.push(conv);
    const double value = std::strtod(arg.c_str(), nullptr);
    std::snprintf(buffer, sizeof(buffer), with_conv.c_str(), value);
    out += buffer;
  } break;
  default:
    /* An unknown conversion is emitted verbatim. */
    out += spec;
    out += conv;
    break;
  }
}

} /* namespace */

Printf::Printf() = default;

pure Builtin::Kind Printf::kind() const wontthrow { return Kind::Printf; }

i32 Printf::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (ec.args().count() < 2) return 0;

  LOG(All, "printf formatting %zu arguments", ec.args().count() - 1);

  /* bash printf -v NAME stores the result in the named variable instead of
     printing it, so the format and operands shift two places past -v NAME.
     The form is a pure addition, so it rides every mood but POSIX the way
     the other bash extensions do. */
  usize format_index = 1;
  Maybe<String> store_variable;
  if (cxt.bash_additions_enabled() && ec.args()[1] == "-v" &&
      ec.args().count() >= 3)
  {
    store_variable = ec.args()[2];
    format_index = 3;
  }

  /* A -- ends the option scan, so a format that begins with a dash still reads
     as the format the way the POSIX printf -- "%s" does. */
  if (format_index < ec.args().count() && ec.args()[format_index] == "--")
    format_index++;

  if (format_index >= ec.args().count()) return 0;

  /* The operands are read in place from the argument list, so no copy of the
     argument strings is made. A missing operand reads as the shared empty
     string, like an absent argument elsewhere. */
  let const &args = ec.args();
  let const &fmt = args[format_index];
  let const operand_base = format_index + 1;
  let const operand_count = args.count() - operand_base;
  let const empty_operand = String{};
  auto do_operand_at = [&](usize index) wontthrow -> const String & {
    return index < operand_count ? args[operand_base + index] : empty_operand;
  };

  let out = String{};
  usize operand_index = 0;
  bool consumed_a_conversion = false;
  bool should_stop = false;

  /* Read the next operand as the integer value of a * field width or precision,
     append its decimal text into the spec, and advance the operand cursor. */
  auto do_consume_star = [&](String &spec) throws {
    spec.append(
        utils::int_to_text(parse_printf_integer(do_operand_at(operand_index)))
            .view());
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

      /* bash %(datefmt)T formats a time through strftime. The format sits in
         parentheses where the conversion letter would be, followed by T, and
         the operand is the epoch seconds, -1 for now and -2 for the shell
         start. A field width sits before the ( the way bash reads it, so the
         scanned spec pads the formatted time as a string. */
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
            append_conversion(out, spec, 's', formatted);
          operand_index++;
          consumed_a_conversion = true;
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

      let const &arg = do_operand_at(operand_index);
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
  } while (!should_stop && operand_index < operand_count &&
           consumed_a_conversion);

  if (store_variable.has_value()) {
    /* A name[subscript] target writes one array element, the form the
       bash-completion word reassembly stores through, while a plain name
       writes the scalar. */
    let const target = store_variable->view();
    let const open_bracket = target.find_character('[');
    if (open_bracket.has_value() && *open_bracket > 0 &&
        target.length >= *open_bracket + 2 && target[target.length - 1] == ']')
    {
      let const array_name = target.substring_of_length(0, *open_bracket);
      let const subscript = target.substring_of_length(
          *open_bracket + 1, target.length - *open_bracket - 2);
      cxt.assign_array_element(array_name, subscript, out.view(), false);
      return 0;
    }
    cxt.set_shell_variable(target, out.view());
    return 0;
  }

  ec.print_to_stdout(out);
  return 0;
}

} /* namespace shit */
