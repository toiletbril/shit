#include "Common.hpp"
#include "Debug.hpp"
#include "ErrorOr.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Lexer.hpp"
#include "Maybe.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <cstdint>

namespace shit {

namespace {

/* The count of leading bytes that are digits in the given radix, so a value
   with trailing non-digit bytes reads only its numeric prefix the way base-0
   strtoll did. A hexadecimal scan accepts both letter cases. */
pure fn count_leading_digits(StringView text, u32 radix) wontthrow -> usize
{
  usize length = 0;

  while (length < text.length) {
    let const current_byte = text[length];
    u32 digit;
    if (current_byte >= '0' && current_byte <= '9') {
      digit = static_cast<u32>(current_byte - '0');
    } else if (current_byte >= 'a' && current_byte <= 'f') {
      digit = static_cast<u32>(current_byte - 'a') + 10;
    } else if (current_byte >= 'A' && current_byte <= 'F') {
      digit = static_cast<u32>(current_byte - 'A') + 10;
    } else {
      break;
    }
    if (digit >= radix) break;
    length++;
  }

  return length;
}

/* Read a numeric operand the way base-0 strtoll did, detecting the radix from
   the prefix so a leading 0x reads as hexadecimal, a leading 0 reads as octal,
   and anything else reads as decimal. Only the leading run of valid digits is
   read, so a trailing non-digit suffix is ignored rather than rejected. A value
   with no leading digit or one that overflows reads as zero, the same result
   the old strtoll path produced after its throw was caught. The utils parsers
   take no base argument, so the radix is chosen here from the prefix and the
   matching parser runs on the scanned digit run. */
pure fn parse_arithmetic_operand(StringView text) wontthrow -> i64
{
  let body = text;
  let is_negative = false;
  if (body.length > 0 && (body[0] == '+' || body[0] == '-')) {
    is_negative = body[0] == '-';
    body = body.substring(1);
  }

  let const parsed_value = [&]() -> ErrorOr<i64> {
    if (body.length >= 2 && body[0] == '0' &&
        (body[1] == 'x' || body[1] == 'X'))
    {
      let const digits = body.substring(2);
      return utils::parse_hexadecimal_integer(
          digits.substring_of_length(0, count_leading_digits(digits, 16)));
    }
    if (body.length >= 1 && body[0] == '0') {
      return utils::parse_octal_integer(
          body.substring_of_length(0, count_leading_digits(body, 8)));
    }
    return utils::parse_decimal_integer(
        body.substring_of_length(0, count_leading_digits(body, 10)));
  }();

  if (parsed_value.is_error()) return 0;
  return is_negative ? -parsed_value.value() : parsed_value.value();
}

/* Signed arithmetic in $((...)) wraps two's-complement the way dash does, so
   the add, subtract, and multiply run in u64 where overflow is defined and the
   result casts back to i64. A direct i64 overflow would be undefined and trips
   UBSan in the dbg build. */
pure fn arithmetic_add(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) + static_cast<u64>(rhs));
}

pure fn arithmetic_subtract(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) - static_cast<u64>(rhs));
}

pure fn arithmetic_multiply(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) * static_cast<u64>(rhs));
}

/* Exponentiation by squaring in u64 so the result wraps in 64 bits the way the
   other operators do. The caller rejects a negative exponent, so the count is
   non-negative here. */
pure fn arithmetic_power(i64 base, i64 exponent) wontthrow -> i64
{
  let result = static_cast<u64>(1);
  let factor = static_cast<u64>(base);
  let remaining = static_cast<u64>(exponent);
  while (remaining > 0) {
    if ((remaining & 1u) != 0) result *= factor;
    factor *= factor;
    remaining >>= 1;
  }
  return static_cast<i64>(result);
}

/* INT64_MIN / -1 and INT64_MIN % -1 overflow the signed result and trap on
   x86, so the wrapped values are returned directly. Division yields INT64_MIN
   and modulo yields 0, which is the two's-complement wrap. */
pure fn arithmetic_divide(i64 lhs, i64 rhs) wontthrow -> i64
{
  if (lhs == INT64_MIN && rhs == -1) return INT64_MIN;
  return lhs / rhs;
}

pure fn arithmetic_modulo(i64 lhs, i64 rhs) wontthrow -> i64
{
  if (lhs == INT64_MIN && rhs == -1) return 0;
  return lhs % rhs;
}

/* dash masks the shift count to the low 6 bits, so a count of 64 shifts by 0
   and a negative count shifts by its low 6 bits. The shift runs in u64 where a
   shift by a value below the width is defined, and for the right shift the sign
   is carried by hand so a negative operand keeps its arithmetic-shift result.
 */
pure fn arithmetic_shift_left(i64 lhs, i64 rhs) wontthrow -> i64
{
  let const count = static_cast<u64>(rhs) & 63u;
  return static_cast<i64>(static_cast<u64>(lhs) << count);
}

pure fn arithmetic_shift_right(i64 lhs, i64 rhs) wontthrow -> i64
{
  let const count = static_cast<u64>(rhs) & 63u;
  let const is_negative = lhs < 0;
  let value = static_cast<u64>(lhs) >> count;
  if (is_negative && count > 0) {
    value |= ~(~static_cast<u64>(0) >> count);
  }
  return static_cast<i64>(value);
}

/* Defined below the parser, declared here so parse_primary reads a number
   literal through the same lexer the cached-token path uses. */
static fn lex_arith_number(StringView from, i64 *out_value) throws -> usize;

/* A recursive-descent evaluator for $((...)), following C operator precedence,
   that resolves and assigns shell variables through the context. */
class ArithmeticParser
{
public:
  /* Null only on the analyze-time constant fold, where the expression holds no
     variable and no assignment, so neither read_variable_value nor the
     assignment path that dereferences the context is ever reached. */
  EvalContext *context;
  StringView source;
  usize pos;

  /* A parenthesized subexpression descends through parse_primary, so a source
     such as thousands of open parentheses would overflow the native stack. The
     depth is counted at each primary and capped before the recursion. */
  usize depth{0};
  static constexpr usize MAX_DEPTH = 512;

  /* The dead operand of a short-circuited || or && and the untaken arm of a
     ternary are still parsed so their tokens are consumed, but an assignment
     inside them must not take effect. While this flag is set the assignment
     path skips the store, matching the side-effect semantics dash gives. */
  bool m_is_skipping{false};

  [[noreturn]] cold fn fail(StringView message) throws -> void
  {
    throw Error{"Arithmetic: " + message};
  }

  fn skip_spaces() wontthrow -> void
  {
    while (pos < source.length && (source[pos] == ' ' || source[pos] == '\t' ||
                                   source[pos] == '\n' || source[pos] == '\r'))
      pos++;
  }

  fn starts_with(StringView op) wontthrow -> bool
  {
    skip_spaces();
    if (pos + op.length > source.length) return false;
    /* The operator probes run hot with one to three bytes each, so the
       compare is an unrolled byte loop the compiler keeps inline rather than
       a memcmp call per probe. */
    for (usize k = 0; k < op.length; k++)
      if (source[pos + k] != op[k]) return false;
    return true;
  }

  fn consume(StringView op) wontthrow -> bool
  {
    if (!starts_with(op)) return false;
    pos += op.length;
    return true;
  }

  fn read_variable_value(StringView name) throws -> i64
  {
    /* A plain shell variable, the common operand, reads its digits straight
       from the stored value with no copy. The operand parser stops at the first
       non-digit and reads a non-numeric value as zero, which matches the old
       strtoll path. */
    ASSERT(context != nullptr);
    if (let const *stored = context->lookup_shell_variable(name)) {
      if (stored->count() == 0) return 0;
      return parse_arithmetic_operand(stored->view());
    }

    let const value = context->get_variable_value(name);
    if (!value.has_value()) {
      /* An unset name in arithmetic goes through the same reporter as an unset
         parameter expansion, so $((nope)) is not silently zero under the
         strict mood. A skipped ternary branch reads without effect the way its
         assignments are suppressed, so it never reports. */
      if (!m_is_skipping) context->report_unset_reference(name);
      return 0;
    }
    if (value->is_empty()) return 0;
    return parse_arithmetic_operand(value->view());
  }

  /* The name of the variable at the cursor, or an empty view when no name sits
     there. Used as the target of an increment or a decrement. */
  fn read_lvalue_name() wontthrow -> StringView
  {
    skip_spaces();
    if (pos >= source.length || !lexer::is_variable_name_start(source[pos])) {
      return StringView{};
    }
    let const name_start = pos;
    while (pos < source.length && lexer::is_variable_name(source[pos]))
      pos++;
    return source.substring_of_length(name_start, pos - name_start);
  }

  /* Store a new integer value for a name unless the parser is walking a skipped
     branch of a ternary, where the assignment must not take effect. */
  fn write_variable(StringView name, i64 value) throws -> void
  {
    if (m_is_skipping) return;
    ASSERT(context != nullptr);
    /* The store copies the value into its own heap String, so the conversion
       writes into a stack buffer and passes a view, with no transient heap
       allocation at all. */
    char buffer[24];
    context->set_shell_variable(
        name, utils::int_to_text_into(value, buffer, sizeof(buffer)));
  }

  /* An assignment or step target, a name and an optional [subscript] for an
     array element. The subscript is the raw bytes between the brackets, which
     the evaluator expands when the element is read or written. */
  struct lvalue
  {
    StringView name;
    Maybe<StringView> subscript;
  };

  /* A [subscript] right after a name, with the bracket content returned as a
     view, or None when no bracket sits there. Nested brackets are balanced so
     a[b[0]] reads the whole inner expression. */
  fn read_optional_subscript() throws -> Maybe<StringView>
  {
    if (pos >= source.length || source[pos] != '[') return None;
    pos++;
    let const inner_start = pos;
    usize depth = 1;
    while (pos < source.length && depth > 0) {
      if (source[pos] == '[')
        depth++;
      else if (source[pos] == ']' && --depth == 0)
        break;
      pos++;
    }
    if (depth != 0) fail("expected ']' after an array subscript");
    let const subscript =
        source.substring_of_length(inner_start, pos - inner_start);
    pos++;
    return subscript;
  }

  /* A name at the cursor with its optional array subscript. */
  fn read_lvalue() throws -> lvalue
  {
    let const name = read_lvalue_name();
    if (name.is_empty()) return lvalue{name, None};
    return lvalue{name, read_optional_subscript()};
  }

  fn read_lvalue_value(const lvalue &target) throws -> i64
  {
    if (target.subscript.has_value()) {
      ASSERT(context != nullptr);
      return context->read_array_element_integer(target.name,
                                                 *target.subscript);
    }
    return read_variable_value(target.name);
  }

  fn write_lvalue(const lvalue &target, i64 value) throws -> void
  {
    if (m_is_skipping) return;
    if (target.subscript.has_value()) {
      ASSERT(context != nullptr);
      char buffer[24];
      context->assign_array_element(
          target.name, *target.subscript,
          utils::int_to_text_into(value, buffer, sizeof(buffer)), false);
      return;
    }
    write_variable(target.name, value);
  }

  /* A prefix ++ or -- changes the variable and yields the new value. */
  fn prefix_step(i64 delta) throws -> i64
  {
    const lvalue target = read_lvalue();
    if (target.name.is_empty()) fail("expected a variable after '++' or '--'");
    const i64 updated = arithmetic_add(read_lvalue_value(target), delta);
    write_lvalue(target, updated);
    return updated;
  }

  /* A postfix ++ or -- yields the old value and then changes the variable. */
  fn postfix_step(const lvalue &target, i64 delta) throws -> i64
  {
    const i64 original = read_lvalue_value(target);
    write_lvalue(target, arithmetic_add(original, delta));
    return original;
  }

  fn parse() throws -> i64
  {
    /* An empty expression is zero, the way bash reads $(( )) and ${a[$unset]}
       with the subscript expanding to nothing. */
    skip_spaces();
    if (pos == source.length) return 0;
    let const result = parse_comma();
    skip_spaces();
    if (pos != source.length) fail("unexpected trailing characters");
    return result;
  }

  /* The comma operator evaluates each subexpression in order and yields the
     last, the lowest precedence so a C-style for clause such as i=0, j=10
     runs both assignments. */
  fn parse_comma() throws -> i64
  {
    i64 result = parse_assignment();
    while (consume(","))
      result = parse_assignment();
    return result;
  }

  fn apply_compound(i64 lhs, i64 rhs, char kind) throws -> i64
  {
    switch (kind) {
    case '+': return arithmetic_add(lhs, rhs);
    case '-': return arithmetic_subtract(lhs, rhs);
    case '*': return arithmetic_multiply(lhs, rhs);
    case '/':
      if (rhs == 0) fail("division by zero");
      return arithmetic_divide(lhs, rhs);
    case '%':
      if (rhs == 0) fail("division by zero");
      return arithmetic_modulo(lhs, rhs);
    case '&': return lhs & rhs;
    case '|': return lhs | rhs;
    case '^': return lhs ^ rhs;
    case 'L': return arithmetic_shift_left(lhs, rhs);
    case 'R': return arithmetic_shift_right(lhs, rhs);
    default: return rhs;
    }
  }

  fn parse_assignment() throws -> i64
  {
    /* An assignment has a bare variable name on the left, so try it and rewind
       when the name is not followed by an assignment operator. */
    let const save = pos;
    skip_spaces();
    if (pos < source.length && lexer::is_variable_name_start(source[pos])) {
      /* The name is a contiguous slice of the expression the parser holds for
         the whole evaluation, so a view into it avoids a per-read allocation.
       */
      let const name_start = pos;
      while (pos < source.length && lexer::is_variable_name(source[pos]))
        pos++;
      let const name = source.substring_of_length(name_start, pos - name_start);
      const lvalue target{name, read_optional_subscript()};

      struct compound_operator
      {
        StringView token;
        u8 kind;
      };
      static const compound_operator compound_operators[] = {
          {"<<=", 'L'},
          {">>=", 'R'},
          {"+=",  '+'},
          {"-=",  '-'},
          {"*=",  '*'},
          {"/=",  '/'},
          {"%=",  '%'},
          {"&=",  '&'},
          {"|=",  '|'},
          {"^=",  '^'},
      };
      /* The probe loop runs only when the next byte can open a compound
         operator, so the common plain assignment and the bare-name read skip
         the ten probes. */
      skip_spaces();
      let const next_byte = pos < source.length ? source[pos] : '\0';
      if (next_byte == '<' || next_byte == '>' || next_byte == '+' ||
          next_byte == '-' || next_byte == '*' || next_byte == '/' ||
          next_byte == '%' || next_byte == '&' || next_byte == '|' ||
          next_byte == '^')
      {
        for (let const &[ op, kind ] : compound_operators) {
          if (consume(op)) {
            let const rhs = parse_assignment();
            let const result =
                apply_compound(read_lvalue_value(target), rhs, kind);
            write_lvalue(target, result);
            return result;
          }
        }
      }
      if (starts_with("=") && !starts_with("==")) {
        consume("=");
        let const rhs = parse_assignment();
        write_lvalue(target, rhs);
        return rhs;
      }
      pos = save;
    }
    return parse_ternary();
  }

  /* Parse an operand while suppressing its assignments so its tokens are
     consumed without taking effect. The flag is saved and restored, so a
     nested skip region inside an already-skipped one stays skipped. */
  fn parse_skipped(i64 (ArithmeticParser::*parse_branch)()) throws -> i64
  {
    let const was_skipping = m_is_skipping;
    m_is_skipping = true;
    defer { m_is_skipping = was_skipping; };
    return (this->*parse_branch)();
  }

  fn parse_ternary() throws -> i64
  {
    let const condition = parse_binary(1);
    if (consume("?")) {
      /* Only the taken arm runs, the other arm is parsed but suppressed so an
         assignment in the dead arm leaves the variable unchanged. */
      if (condition != 0) {
        let const if_true = parse_assignment();
        if (!consume(":")) fail("expected ':' in a conditional");
        let const if_false = parse_skipped(&ArithmeticParser::parse_ternary);
        unused(if_false);
        return if_true;
      }
      let const if_true = parse_skipped(&ArithmeticParser::parse_assignment);
      unused(if_true);
      if (!consume(":")) fail("expected ':' in a conditional");
      return parse_ternary();
    }
    return condition;
  }

  /* One binary operator at the cursor, its dispatch tag, its precedence with
     11 the tightest ** and 1 the loosest ||, and its token length. Precedence
     0 means no binary operator opens here. */
  struct binary_operator
  {
    char kind;
    u8 precedence;
    u8 length;
  };

  /* Reads the operator at the cursor without consuming it. The byte pair
     decides the doubled forms, so | against || and < against << need no
     rescans, and a compound assignment suffix such as += or <<= answers no
     operator since the assignment level above the ladder owns those. */
  fn peek_binary_operator() wontthrow -> binary_operator
  {
    skip_spaces();
    if (pos >= source.length) return {0, 0, 0};
    let const first_byte = source[pos];
    let const second_byte = pos + 1 < source.length ? source[pos + 1] : '\0';
    let const third_byte = pos + 2 < source.length ? source[pos + 2] : '\0';
    switch (first_byte) {
    case '*':
      if (second_byte == '*') return {'P', 11, 2};
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'*', 10, 1};
    case '/':
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'/', 10, 1};
    case '%':
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'%', 10, 1};
    case '+':
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'+', 9, 1};
    case '-':
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'-', 9, 1};
    case '<':
      if (second_byte == '<')
        return third_byte == '=' ? binary_operator{0, 0, 0}
                                 : binary_operator{'L', 8, 2};
      if (second_byte == '=') return {'l', 7, 2};
      return {'<', 7, 1};
    case '>':
      if (second_byte == '>')
        return third_byte == '=' ? binary_operator{0, 0, 0}
                                 : binary_operator{'R', 8, 2};
      if (second_byte == '=') return {'g', 7, 2};
      return {'>', 7, 1};
    case '=':
      return second_byte == '=' ? binary_operator{'e', 6, 2}
                                : binary_operator{0, 0, 0};
    case '!':
      return second_byte == '=' ? binary_operator{'n', 6, 2}
                                : binary_operator{0, 0, 0};
    case '&':
      if (second_byte == '&') return {'A', 2, 2};
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'&', 5, 1};
    case '^':
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'^', 4, 1};
    case '|':
      if (second_byte == '|') return {'O', 1, 2};
      return second_byte == '=' ? binary_operator{0, 0, 0}
                                : binary_operator{'|', 3, 1};
    default: return {0, 0, 0};
    }
  }

  /* The binary ladder as one precedence-climbing loop, one frame for a whole
     run of operators instead of a call per precedence level, since the chain
     of nine cascade levels showed up whole in the profile. ** climbs right
     associatively by re-entering at its own precedence, the logical pair
     short-circuits by parsing the dead side under suppression the way the
     cascade did, and the ternary, assignment, and comma levels stay above. */
  fn parse_binary(u8 min_precedence) throws -> i64
  {
    let lhs = parse_unary();
    for (;;) {
      let const op = peek_binary_operator();
      if (op.precedence < min_precedence) return lhs;
      pos += op.length;

      if (op.kind == 'A' || op.kind == 'O') {
        /* The dead side is parsed under suppression so its tokens are
           consumed without its assignments taking effect. */
        let const lhs_decides = (op.kind == 'A') == (lhs == 0);
        i64 rhs = 0;
        if (lhs_decides) {
          let const was_skipping = m_is_skipping;
          m_is_skipping = true;
          defer { m_is_skipping = was_skipping; };
          rhs = parse_binary(op.precedence + 1);
        } else {
          rhs = parse_binary(op.precedence + 1);
        }
        lhs = op.kind == 'A' ? ((lhs != 0 && rhs != 0) ? 1 : 0)
                             : ((lhs != 0 || rhs != 0) ? 1 : 0);
        continue;
      }

      /* ** is right-associative, so it re-enters at its own precedence, and
         bash rejects a negative exponent in integer arithmetic. */
      let const rhs =
          parse_binary(op.kind == 'P' ? op.precedence : op.precedence + 1);
      switch (op.kind) {
      case 'P':
        if (rhs < 0) fail("exponent less than 0");
        lhs = arithmetic_power(lhs, rhs);
        break;
      case '*': lhs = arithmetic_multiply(lhs, rhs); break;
      case '/':
        if (rhs == 0) fail("division by zero");
        lhs = arithmetic_divide(lhs, rhs);
        break;
      case '%':
        if (rhs == 0) fail("division by zero");
        lhs = arithmetic_modulo(lhs, rhs);
        break;
      case '+': lhs = arithmetic_add(lhs, rhs); break;
      case '-': lhs = arithmetic_subtract(lhs, rhs); break;
      case 'L': lhs = arithmetic_shift_left(lhs, rhs); break;
      case 'R': lhs = arithmetic_shift_right(lhs, rhs); break;
      case '<': lhs = lhs < rhs ? 1 : 0; break;
      case 'l': lhs = lhs <= rhs ? 1 : 0; break;
      case '>': lhs = lhs > rhs ? 1 : 0; break;
      case 'g': lhs = lhs >= rhs ? 1 : 0; break;
      case 'e': lhs = lhs == rhs ? 1 : 0; break;
      case 'n': lhs = lhs != rhs ? 1 : 0; break;
      case '&': lhs = lhs & rhs; break;
      case '^': lhs = lhs ^ rhs; break;
      case '|': lhs = lhs | rhs; break;
      default: unreachable();
      }
    }
  }

  fn parse_unary() throws -> i64
  {
    /* The doubled operators are checked before the single + and - so a leading
       ++ or -- is read as one prefix step rather than two unary signs. The
       first byte gates the probes, so the common operand pays one read. */
    skip_spaces();
    let const first = pos < source.length ? source[pos] : '\0';
    if (first == '+') {
      if (consume("++")) return prefix_step(1);
      pos++;
      return parse_unary();
    }
    if (first == '-') {
      if (consume("--")) return prefix_step(-1);
      pos++;
      return arithmetic_subtract(0, parse_unary());
    }
    if (first == '!') {
      pos++;
      return parse_unary() == 0 ? 1 : 0;
    }
    if (first == '~') {
      pos++;
      return ~parse_unary();
    }
    return parse_primary();
  }

  fn parse_primary() throws -> i64
  {
    depth++;
    defer { depth--; };
    if (depth > MAX_DEPTH) fail("expression nested too deeply");

    skip_spaces();
    if (consume("(")) {
      let const value = parse_comma();
      if (!consume(")")) fail("expected ')'");
      return value;
    }
    if (pos < source.length && lexer::is_number(source[pos])) {
      /* A number literal reads through the shared lexer, which measures the
         base#digits radix form, a 0x hex, a 0 octal, or a decimal run and
         reports the bytes it consumed. */
      i64 value = 0;
      pos += lex_arith_number(source.substring(pos), &value);
      return value;
    }
    if (pos < source.length && lexer::is_variable_name_start(source[pos])) {
      /* The name is a contiguous slice of the expression the parser holds for
         the whole evaluation, so a view into it avoids a per-read allocation. A
         trailing ++ or -- right after the name is a postfix step on it. */
      const lvalue target = read_lvalue();
      if (consume("++")) return postfix_step(target, 1);
      if (consume("--")) return postfix_step(target, -1);
      return read_lvalue_value(target);
    }
    fail("unexpected character");
  }
};

/* Lexes one arithmetic number literal at the front of `from`, the same forms
   parse_primary reads, a base#digits radix literal, a 0x hex, a 0 octal, or a
   decimal run. Stores the value and returns the byte count it consumed. */
static fn lex_arith_number(StringView from, i64 *out_value) throws -> usize
{
  if (let const base_length = count_leading_digits(from, 10);
      base_length > 0 && base_length < from.length && from[base_length] == '#')
  {
    let const base =
        parse_arithmetic_operand(from.substring_of_length(0, base_length));
    if (base < 2 || base > 64) {
      throw Error{"Arithmetic: an arithmetic base must be between 2 and 64"};
    }
    let const do_digit_value = [base](char c) -> i64 {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'z') return c - 'a' + 10;
      if (c >= 'A' && c <= 'Z') return base <= 36 ? c - 'A' + 10 : c - 'A' + 36;
      if (c == '@') return 62;
      if (c == '_') return 63;
      return -1;
    };
    i64 value = 0;
    usize i = base_length + 1;
    while (i < from.length) {
      let const digit = do_digit_value(from[i]);
      if (digit < 0 || digit >= base) {
        break;
      }
      value = value * base + digit;
      i++;
    }
    *out_value = value;
    return i;
  }

  usize consumed;
  if (from.length >= 2 && from[0] == '0' && (from[1] == 'x' || from[1] == 'X'))
  {
    consumed = 2 + count_leading_digits(from.substring(2), 16);
  } else if (from.length >= 1 && from[0] == '0') {
    consumed = count_leading_digits(from, 8);
  } else {
    consumed = count_leading_digits(from, 10);
  }
  if (consumed == 0) consumed = 1;
  *out_value = parse_arithmetic_operand(from.substring_of_length(0, consumed));
  return consumed;
}

/* The operators an expression may hold, longest first so the scan munches
   maximally, <<= before << before <. */
static const StringView ARITH_OPERATORS[] = {
    "<<=", ">>=", "**", "<<", ">>", "<=", ">=", "==", "!=", "&&",
    "||",  "++",  "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=",
    "^=",  "(",   ")",  ",",  "?",  ":",  "+",  "-",  "*",  "/",
    "%",   "<",   ">",  "=",  "&",  "|",  "^",  "!",  "~",
};

/* Lexes the whole arithmetic expression into tokens once. A number carries its
   value, a name and an operator carry a view into the source, and an array
   subscript carries the balanced raw bytes between its brackets. */
static fn tokenize_arithmetic(StringView src,
                              ArrayList<arith_token> &out) throws -> void
{
  usize i = 0;
  while (i < src.length) {
    let const current_byte = src[i];
    if (current_byte == ' ' || current_byte == '\t' || current_byte == '\n' ||
        current_byte == '\r')
    {
      i++;
      continue;
    }
    if (lexer::is_number(current_byte)) {
      i64 value = 0;
      let const consumed = lex_arith_number(src.substring(i), &value);
      out.push(arith_token{arith_token::kind::number, value,
                           src.substring_of_length(i, consumed)});
      i += consumed;
      continue;
    }
    if (lexer::is_variable_name_start(current_byte)) {
      let const name_start = i;
      i++;
      while (i < src.length && lexer::is_variable_name(src[i]))
        i++;
      out.push(
          arith_token{arith_token::kind::name, 0,
                      src.substring_of_length(name_start, i - name_start)});
      if (i < src.length && src[i] == '[') {
        i++;
        let const inner_start = i;
        usize depth = 1;
        while (i < src.length && depth > 0) {
          if (src[i] == '[')
            depth++;
          else if (src[i] == ']' && --depth == 0)
            break;
          i++;
        }
        if (depth != 0)
          throw Error{"Arithmetic: expected ']' after an array subscript"};
        out.push(
            arith_token{arith_token::kind::subscript, 0,
                        src.substring_of_length(inner_start, i - inner_start)});
        i++;
      }
      continue;
    }
    bool is_matched = false;
    for (let const &op : ARITH_OPERATORS) {
      if (i + op.length <= src.length &&
          src.substring_of_length(i, op.length) == op)
      {
        out.push(arith_token{arith_token::kind::op, 0,
                             src.substring_of_length(i, op.length)});
        i += op.length;
        is_matched = true;
        break;
      }
    }
    /* An unrecognized byte becomes a one-byte op so the simple evaluator fails
       on it the way the char parser does. */
    if (!is_matched) {
      out.push(
          arith_token{arith_token::kind::op, 0, src.substring_of_length(i, 1)});
      i++;
    }
  }
}

/* An operator that assigns, steps, short-circuits, or branches forces the full
   char parser, since the token fast path keeps no side-effect ordering. */
static pure fn arith_op_is_complex(StringView t) wontthrow -> bool
{
  return t == "=" || t == "+=" || t == "-=" || t == "*=" || t == "/=" ||
         t == "%=" || t == "&=" || t == "|=" || t == "^=" || t == "<<=" ||
         t == ">>=" || t == "?" || t == ":" || t == "," || t == "++" ||
         t == "--" || t == "&&" || t == "||";
}

/* True when every token is a plain arithmetic token, so the fast path can
   evaluate it with no assignment, branch, or short-circuit to order. */
static pure fn
arith_tokens_are_simple(const ArrayList<arith_token> &toks) wontthrow -> bool
{
  for (let const &t : toks) {
    if (t.k == arith_token::kind::subscript) return false;
    if (t.k == arith_token::kind::op && arith_op_is_complex(t.text)) {
      return false;
    }
  }
  return true;
}

struct arith_binop
{
  char kind;
  u8 precedence;
};

/* The dispatch tag and precedence of a binary operator token, mirroring
   peek_binary_operator. Precedence 0 means the token is not a binary operator,
   the short-circuit pair is excluded since a simple expression never holds it.
 */
static pure fn arith_classify_binop(StringView t) wontthrow -> arith_binop
{
  if (t == "**") return {'P', 11};
  if (t == "*") return {'*', 10};
  if (t == "/") return {'/', 10};
  if (t == "%") return {'%', 10};
  if (t == "+") return {'+', 9};
  if (t == "-") return {'-', 9};
  if (t == "<<") return {'L', 8};
  if (t == ">>") return {'R', 8};
  if (t == "<") return {'<', 7};
  if (t == "<=") return {'l', 7};
  if (t == ">") return {'>', 7};
  if (t == ">=") return {'g', 7};
  if (t == "==") return {'e', 6};
  if (t == "!=") return {'n', 6};
  if (t == "&") return {'&', 5};
  if (t == "^") return {'^', 4};
  if (t == "|") return {'|', 3};
  return {0, 0};
}

/* Applies a binary operator, the same arithmetic helpers and rules the char
   parser's ladder uses, so the fast path and the full parser agree. */
static fn arith_apply_binop(char kind, i64 lhs, i64 rhs) throws -> i64
{
  switch (kind) {
  case 'P':
    if (rhs < 0) throw Error{"Arithmetic: exponent less than 0"};
    return arithmetic_power(lhs, rhs);
  case '*': return arithmetic_multiply(lhs, rhs);
  case '/':
    if (rhs == 0) throw Error{"Arithmetic: division by zero"};
    return arithmetic_divide(lhs, rhs);
  case '%':
    if (rhs == 0) throw Error{"Arithmetic: division by zero"};
    return arithmetic_modulo(lhs, rhs);
  case '+': return arithmetic_add(lhs, rhs);
  case '-': return arithmetic_subtract(lhs, rhs);
  case 'L': return arithmetic_shift_left(lhs, rhs);
  case 'R': return arithmetic_shift_right(lhs, rhs);
  case '<': return lhs < rhs ? 1 : 0;
  case 'l': return lhs <= rhs ? 1 : 0;
  case '>': return lhs > rhs ? 1 : 0;
  case 'g': return lhs >= rhs ? 1 : 0;
  case 'e': return lhs == rhs ? 1 : 0;
  case 'n': return lhs != rhs ? 1 : 0;
  case '&': return lhs & rhs;
  case '^': return lhs ^ rhs;
  case '|': return lhs | rhs;
  default: return rhs;
  }
}

/* Reads a variable's integer value, the same path read_variable_value takes, an
   unset name reports and reads as zero. The fast path runs only on a
   side-effect-free expression, so the report is never suppressed. */
static fn arith_read_variable(EvalContext *context, StringView name) throws
    -> i64
{
  ASSERT(context != nullptr);
  if (let const *stored = context->lookup_shell_variable(name)) {
    if (stored->count() == 0) return 0;
    return parse_arithmetic_operand(stored->view());
  }
  let const value = context->get_variable_value(name);
  if (!value.has_value()) {
    context->report_unset_reference(name);
    return 0;
  }
  if (value->is_empty()) return 0;
  return parse_arithmetic_operand(value->view());
}

/* A precedence-climbing evaluator over the cached token stream for a simple
   expression, numbers, variable reads, unary operators, parentheses, and binary
   operators. It has no assignment, ternary, comma, or short-circuit, so it
   never orders a side effect, which the caller guarantees by the simple check.
 */
class ArithmeticTokenEvaluator
{
public:
  EvalContext *context;
  const ArrayList<arith_token> &toks;
  usize ti{0};
  usize depth{0};
  static constexpr usize MAX_DEPTH = 512;

  pure fn at_op(StringView s) wontthrow -> bool
  {
    return ti < toks.count() && toks[ti].k == arith_token::kind::op &&
           toks[ti].text == s;
  }

  fn parse_operand() throws -> i64
  {
    depth++;
    defer { depth--; };
    if (depth > MAX_DEPTH)
      throw Error{"Arithmetic: expression nested too deeply"};

    if (at_op("+")) {
      ti++;
      return parse_operand();
    }
    if (at_op("-")) {
      ti++;
      return arithmetic_subtract(0, parse_operand());
    }
    if (at_op("!")) {
      ti++;
      return parse_operand() == 0 ? 1 : 0;
    }
    if (at_op("~")) {
      ti++;
      return ~parse_operand();
    }
    if (at_op("(")) {
      ti++;
      let const value = parse_binary(1);
      if (!at_op(")")) throw Error{"Arithmetic: expected ')'"};
      ti++;
      return value;
    }
    if (ti < toks.count() && toks[ti].k == arith_token::kind::number) {
      let const value = toks[ti].value;
      ti++;
      return value;
    }
    if (ti < toks.count() && toks[ti].k == arith_token::kind::name) {
      let const name = toks[ti].text;
      ti++;
      return arith_read_variable(context, name);
    }
    throw Error{"Arithmetic: unexpected character"};
  }

  fn parse_binary(u8 min_precedence) throws -> i64
  {
    let lhs = parse_operand();
    for (;;) {
      if (ti >= toks.count() || toks[ti].k != arith_token::kind::op) {
        return lhs;
      }
      let const op = arith_classify_binop(toks[ti].text);
      if (op.precedence < min_precedence) return lhs;
      ti++;
      let const rhs =
          parse_binary(op.kind == 'P' ? op.precedence : op.precedence + 1);
      lhs = arith_apply_binop(op.kind, lhs, rhs);
    }
  }

  fn run() throws -> i64
  {
    if (toks.is_empty()) return 0;
    let const result = parse_binary(1);
    if (ti != toks.count())
      throw Error{"Arithmetic: unexpected trailing characters"};
    return result;
  }
};

} /* namespace */

fn EvalContext::read_array_element_integer(StringView name,
                                           StringView subscript) throws -> i64
{
  return parse_arithmetic_operand(
      apply_array_subscript(name, subscript).view());
}

fn EvalContext::evaluate_arithmetic(StringView expression) throws -> i64
{
  LOG(All, "evaluating the arithmetic expression of %zu bytes",
      expression.length);
  /* Parameter expansion runs first, so a $1, a $x, or a ${...} inside the
     arithmetic becomes its value before the expression is parsed. A bare name
     is still resolved during evaluation. When the source holds no parameter to
     expand, which the d=$((d+1)) hot loop hits every iteration, the expansion
     copy is skipped and the original is parsed directly. */
  if (!expression.find_character('$').has_value() &&
      !expression.find_character('`').has_value())
  {
    let parser = ArithmeticParser{this, expression, 0};
    return parser.parse();
  }

  /* The expanded word owns the bytes the parser views, so it outlives the
     parser below. */
  LOG(All, "expanding parameters inside the arithmetic before the parse");
  let const expanded_word = expand_modifier_word(expression);
  let parser = ArithmeticParser{this, expanded_word.view(), 0};
  return parser.parse();
}

fn EvalContext::evaluate_arithmetic_cached(const WordSegment &segment) throws
    -> i64
{
  let const expr = segment.text.view();
  /* A parameter or command substitution inside the arithmetic needs the full
     expansion path, so only a substitution-free expression, the hot loop case
     such as d=$((d+1)), takes the cached token path. */
  if (expr.find_character('$').has_value() ||
      expr.find_character('`').has_value())
  {
    return evaluate_arithmetic(expr);
  }

  if (!segment.arith_tokenized) {
    segment.cached_arith_tokens.clear();
    try {
      tokenize_arithmetic(expr, segment.cached_arith_tokens);
    } catch (...) {
      /* A lexing failure leaves the token path off for this segment, so the
         char parser reports the same error and the cache never adds one. */
      segment.cached_arith_tokens.clear();
      segment.arith_tokenized = true;
      segment.arith_simple = false;
      return evaluate_arithmetic(expr);
    }
    segment.arith_tokenized = true;
    segment.arith_simple = arith_tokens_are_simple(segment.cached_arith_tokens);
  }

  /* A complex expression, an assignment, a ternary, a comma, a short-circuit,
     an increment, or an array element, runs through the full char parser, which
     keeps the side-effect ordering the fast path does not. */
  if (!segment.arith_simple) return evaluate_arithmetic(expr);

  ArithmeticTokenEvaluator evaluator{this, segment.cached_arith_tokens};
  return evaluator.run();
}

fn evaluate_constant_arithmetic(StringView expression) throws -> i64
{
  /* The optimizer has already proven the expression holds no variable and no
     assignment, so the parser never dereferences its context and a null one is
     safe. */
  let parser = ArithmeticParser{nullptr, expression, 0};
  return parser.parse();
}

} /* namespace shit */
