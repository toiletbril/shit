#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Utils.hpp"

/* _get_osfhandle maps a shell fd number to its Windows handle for the -t test. */
#if SHIT_PLATFORM_IS WIN32
#include <io.h>
#endif

namespace shit {

namespace {

bool parse_integer(StringView text, i64 &out) throws
{
  let const parsed = utils::parse_decimal_integer(text);
  if (parsed.is_error()) return false;
  out = parsed.value();
  return true;
}

/* A recursive-descent evaluator over the test arguments, following the POSIX
   test grammar so -a binds tighter than -o and ! and parentheses nest. The
   window is [pos, end), so the argument-count rules can strip a wrapping paren
   pair by narrowing end and pos before the grammar runs. */
class TestEvaluator
{
public:
  const ArrayList<String> &args;
  usize pos;
  usize end;
  /* bash accepts == as a synonym for = in test, while dash and POSIX reject it,
     so the operator is accepted only in the bash mood and rejected otherwise to
     keep the default mood matching dash. */
  bool bash_compatible;

  pure const String &current() const wontthrow
  {
    ASSERT(pos < end);
    return args[pos];
  }
  pure bool at_end() const wontthrow { return pos >= end; }

  /* A malformed expression throws, so the builtin dispatch wraps the message
     with the test name and a caret at the command word. */
  void fail(StringView message) throws { throw Error{String{message}}; }

  bool evaluate_unary(const String &op, const String &operand) throws
  {
    if (op == "-z") return operand.is_empty();
    if (op == "-n") return !operand.is_empty();
    let const operand_path = Path{operand};
    if (op == "-e") return operand_path.exists();
    if (op == "-f") return operand_path.is_regular_file();
    if (op == "-d") return operand_path.is_directory();
    if (op == "-s") {
      let const size = operand_path.file_size();
      return size.has_value() && size.value() > 0;
    }
    if (op == "-r") return operand_path.is_readable();
    if (op == "-w") return operand_path.is_writable();
    if (op == "-x") return operand_path.is_executable();
    if (op == "-L" || op == "-h") return operand_path.is_symbolic_link();
    if (op == "-b") return operand_path.is_block_device();
    if (op == "-c") return operand_path.is_character_device();
    if (op == "-p") return operand_path.is_fifo();
    if (op == "-S") return operand_path.is_socket();
    /* -t takes a file-descriptor number rather than a path and is true when
       that descriptor is a terminal. The common 0, 1, and 2 map to the standard
       streams, and any other descriptor reads as not a terminal. */
    if (op == "-t") {
      i64 file_descriptor = 0;
      if (!parse_integer(operand.view(), file_descriptor)) return false;
      /* Any descriptor is checked, not only the standard three, since a config
         dups the controlling terminal onto a higher descriptor and tests it. */
#if SHIT_PLATFORM_IS WIN32
      /* A Windows descriptor is a HANDLE, so the shell fd number is mapped to
         its C runtime handle before the tty check. */
      return os::is_fd_a_tty(reinterpret_cast<os::descriptor>(
          _get_osfhandle(static_cast<int>(file_descriptor))));
#else
      return os::is_fd_a_tty(static_cast<os::descriptor>(file_descriptor));
#endif
    }
    fail(StringView{"Unable to evaluate the test because '"} + op +
         "' is not a known unary operator, expected one of -z -n -e -f -d -s -r "
         "-w -x -L -h -b -c -p -S -t");
    return false;
  }

  bool evaluate_binary(const String &left, const String &op,
                       const String &right) throws
  {
    /* == is a bashism for string equality. bash accepts it, so the bash mood
       treats it as =, while the default and POSIX moods reject it the way dash
       does. The analysis stage also warns on it as SC3014. */
    if (op == "=" || (op == "==" && bash_compatible)) return left == right;
    if (op == "==") {
      fail("Unable to evaluate the test because '==' is a bashism, use = for "
           "string equality in POSIX mode");
      return false;
    }
    if (op == "!=") return left != right;
    /* < and > compare the two operands byte by byte, the locale-byte order
       POSIX specifies, so the same memcmp order String::operator< gives. */
    if (op == "<") return left < right;
    if (op == ">") return right < left;

    /* -ef, -nt, and -ot compare two files on disk rather than their names, so
       the operands are taken as paths and the result reads false when either
       path is missing, matching dash. */
    if (op == "-ef") return Path{left}.is_same_file_as(Path{right});
    if (op == "-nt") return Path{left}.is_newer_than(Path{right});
    if (op == "-ot") return Path{left}.is_older_than(Path{right});

    i64 a = 0, b = 0;
    if (op == "-eq" || op == "-ne" || op == "-lt" || op == "-le" ||
        op == "-gt" || op == "-ge")
    {
      let const left_is_integer = parse_integer(left, a);
      let const right_is_integer = parse_integer(right, b);
      if (!left_is_integer || !right_is_integer) {
        let const &not_a_number = left_is_integer ? right : left;
        fail(StringView{"Unable to compare with '"} + op + "' because '" +
             not_a_number + "' is not an integer");
        return false;
      }
      if (op == "-eq") return a == b;
      if (op == "-ne") return a != b;
      if (op == "-lt") return a < b;
      if (op == "-le") return a <= b;
      if (op == "-gt") return a > b;
      return a >= b;
    }
    fail(StringView{"Unable to evaluate the test because '"} + op +
         "' is not a known binary operator, expected one of = != < > -eq -ne "
         "-lt -le -gt -ge -ef -nt -ot");
    return false;
  }

  pure bool is_unary_operator(const String &s) const wontthrow
  {
    return s == "-z" || s == "-n" || s == "-e" || s == "-f" || s == "-d" ||
           s == "-s" || s == "-r" || s == "-w" || s == "-x" || s == "-L" ||
           s == "-h" || s == "-b" || s == "-c" || s == "-p" || s == "-S" ||
           s == "-t";
  }

  pure bool is_binary_operator(const String &s) const wontthrow
  {
    /* == is listed so the bashism routes to evaluate_binary, where it is
       accepted as =, rather than falling through to the unexpected-argument
       error. -ef, -nt, and -ot are the file-compare operators evaluate_binary
       resolves against the two paths. */
    return s == "=" || s == "==" || s == "!=" || s == "<" || s == ">" ||
           s == "-eq" || s == "-ne" || s == "-lt" || s == "-le" || s == "-gt" ||
           s == "-ge" || s == "-ef" || s == "-nt" || s == "-ot";
  }

  /* A unary operator at index reads as a plain operand rather than an operator
     when nothing follows it, or when two or more tokens follow and the next is
     a binary operator, mirroring dash's isoperand. With exactly one token after
     it the operator is a real unary primary. */
  pure bool is_unary_in_operand_position(usize index) const wontthrow
  {
    if (index + 1 >= end) return true;
    if (index + 2 >= end) return false;
    return is_binary_operator(args[index + 1]);
  }

  /* A bare ( reads as a plain operand rather than a grouping paren when no
     token follows it, mirroring dash's t_lex special case for a trailing paren.
   */
  pure bool is_open_paren_token(usize index) const wontthrow
  {
    return args[index] == "(" && index + 1 < end;
  }

  pure bool is_unary_operator_token(usize index) const wontthrow
  {
    return is_unary_operator(args[index]) &&
           !is_unary_in_operand_position(index);
  }

  bool parse_factor() throws
  {
    if (at_end()) {
      fail("Unable to evaluate the test because an argument is expected");
      return false;
    }
    if (current() == "!") {
      /* A lone ! with nothing after it is the one-argument test of a non-empty
         string, which is true, not a negation missing its operand. */
      if (pos + 1 >= end) {
        pos++;
        return true;
      }
      pos++;
      return !parse_factor();
    }
    if (is_open_paren_token(pos)) {
      pos++;
      let const result = parse_expression();
      if (at_end() || current() != ")")
        fail("Unable to evaluate the test because a ')' is expected");
      else
        pos++;
      return result;
    }
    if (is_unary_operator_token(pos)) {
      let const &op = args[pos];
      let const &operand = args[pos + 1];
      pos += 2;
      return evaluate_unary(op, operand);
    }
    /* A binary test needs the operator in the next position, otherwise a single
       argument is true when it is non-empty. */
    if (pos + 1 < end && is_binary_operator(args[pos + 1])) {
      if (pos + 2 >= end) {
        fail(StringView{"Unable to evaluate the test because an argument is "
                        "expected after '"} +
             args[pos + 1] + "'");
        pos = end;
        return false;
      }
      let const &left = args[pos];
      let const &op = args[pos + 1];
      let const &right = args[pos + 2];
      pos += 3;
      return evaluate_binary(left, op, right);
    }
    let const result = !current().is_empty();
    pos++;
    return result;
  }

  bool parse_term() throws
  {
    bool result = parse_factor();
    while (!at_end() && current() == "-a") {
      pos++;
      let const right = parse_factor();
      result = result && right;
    }
    return result;
  }

  bool parse_expression() throws
  {
    bool result = parse_term();
    while (!at_end() && current() == "-o") {
      pos++;
      let const right = parse_term();
      result = result || right;
    }
    return result;
  }

  /* The POSIX argument-count rules disambiguate the short forms before the
     grammar runs, mirroring dash's testcmd. A three-argument form whose middle
     operand is a binary primary is the binary test, so a literal paren in the
     first or third operand stays a plain operand. A three or four argument form
     wrapped in a paren pair strips the pair, and one led by ! strips the bang
     and flips the verdict, then rechecks the shorter window. Whatever remains
     runs through the grammar, where a paren groups and -a and -o connect. The
     window narrows by pos and end so the stripping needs no copy. */
  bool evaluate_top() throws
  {
    let should_negate = false;
    for (;;) {
      let const count = end - pos;
      if (count < 1) return !should_negate;

      /* A three-argument form whose middle operand is a binary primary is the
         binary test, so the first and third operands are plain strings rather
         than a grouping paren or a unary primary. dash evaluates it directly
         with the first token forced to an operand, so the binary is evaluated
         here without entering the paren-aware grammar. */
      if (count == 3 && is_binary_operator(args[pos + 1])) {
        let const result =
            evaluate_binary(args[pos], args[pos + 1], args[pos + 2]);
        pos = end;
        return should_negate ? !result : result;
      }

      /* A three or four argument form wrapped in a paren pair strips the pair,
         and one led by ! strips the bang and flips the verdict, then rechecks
         the shorter window. */
      if (count == 3 || count == 4) {
        if (args[pos] == "(" && args[end - 1] == ")") {
          pos++;
          end--;
          continue;
        }
        if (args[pos] == "!") {
          should_negate = !should_negate;
          pos++;
          continue;
        }
      }
      break;
    }

    let const result = parse_expression();
    return should_negate ? !result : result;
  }
};

} /* namespace */

Test::Test() = default;

pure Builtin::Kind Test::kind() const wontthrow { return Kind::Test; }

i32 Test::execute(ExecContext &ec, EvalContext &cxt) const throws
{

  /* Strip the program name, and for the [ form the required trailing ]. The
     last operand index ends the expression, one before the trailing ] in the
     bracket form. */
  let const &arguments = ec.args();
  ASSERT(!arguments.is_empty());

  usize expression_end = arguments.count();
  if (ec.program() == "[") {
    if (arguments.count() < 2 || arguments[arguments.count() - 1] != "]")
      throw Error{
          "The closing ']' is missing"};
    expression_end = arguments.count() - 1;
  }

  let operands = ArrayList<String>{};
  operands.reserve(expression_end - 1);
  for (usize i = 1; i < expression_end; i++)
    operands.push(arguments[i]);

  /* An empty expression is false, as POSIX specifies for test with no
     arguments. */
  if (operands.is_empty()) return 1;

  let evaluator =
      TestEvaluator{operands, 0, operands.count(), cxt.is_bash_compatible()};
  let const result = evaluator.evaluate_top();
  /* A paren pair the argument-count rules stripped narrowed end past the
     closing paren, so the leftover check runs against the narrowed window
     rather than the original operand count. */
  if (evaluator.pos != evaluator.end) {
    ASSERT(evaluator.pos < evaluator.end);
    throw Error{StringView{"'"} + operands[evaluator.pos] +
                "' is an unexpected argument"};
  }
  return result ? 0 : 1;
}

} /* namespace shit */
