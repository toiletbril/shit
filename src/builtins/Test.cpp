#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Utils.hpp"

namespace shit {

namespace {

bool parse_integer(StringView text, i64 &out)
{
  const ErrorOr<i64> parsed = utils::parse_decimal_integer(text);
  if (parsed.is_error()) return false;
  out = parsed.value();
  return true;
}

/* A recursive-descent evaluator over the test arguments, following the POSIX
   test grammar so -a binds tighter than -o and ! and parentheses nest. */
struct TestEvaluator
{
  const ArrayList<String> &args;
  usize pos;
  bool had_error;

  const String &current() const { return args[pos]; }
  bool at_end() const { return pos >= args.size(); }

  void fail(StringView message)
  {
    if (!had_error) shit::print_error(StringView{"test: "} + message + "\n");
    had_error = true;
  }

  bool evaluate_unary(const String &op, const String &operand)
  {
    if (op == "-z") return operand.empty();
    if (op == "-n") return !operand.empty();
    const Path operand_path{operand};
    if (op == "-e") return operand_path.exists();
    if (op == "-f") return operand_path.is_regular_file();
    if (op == "-d") return operand_path.is_directory();
    if (op == "-s") {
      const Maybe<u64> size = operand_path.file_size();
      return size.has_value() && size.value() > 0;
    }
    if (op == "-r") return operand_path.is_readable();
    if (op == "-w") return operand_path.is_writable();
    if (op == "-x") return operand_path.is_executable();
    fail(StringView{"unknown unary operator '"} + op + "'");
    return false;
  }

  bool evaluate_binary(const String &left, const String &op,
                       const String &right)
  {
    if (op == "=") return left == right;
    if (op == "!=") return left != right;
    i64 a = 0, b = 0;
    if (op == "-eq" || op == "-ne" || op == "-lt" || op == "-le" ||
        op == "-gt" || op == "-ge")
    {
      if (!parse_integer(left, a) || !parse_integer(right, b)) {
        fail("integer expression expected");
        return false;
      }
      if (op == "-eq") return a == b;
      if (op == "-ne") return a != b;
      if (op == "-lt") return a < b;
      if (op == "-le") return a <= b;
      if (op == "-gt") return a > b;
      return a >= b;
    }
    fail(StringView{"unknown binary operator '"} + op + "'");
    return false;
  }

  bool is_unary_operator(const String &s)
  {
    return s == "-z" || s == "-n" || s == "-e" || s == "-f" || s == "-d" ||
           s == "-s" || s == "-r" || s == "-w" || s == "-x";
  }

  bool is_binary_operator(const String &s)
  {
    return s == "=" || s == "!=" || s == "-eq" || s == "-ne" || s == "-lt" ||
           s == "-le" || s == "-gt" || s == "-ge";
  }

  bool parse_factor()
  {
    if (at_end()) {
      fail("argument expected");
      return false;
    }
    if (current() == "!") {
      pos++;
      return !parse_factor();
    }
    if (current() == "(") {
      pos++;
      const bool result = parse_expression();
      if (at_end() || current() != ")")
        fail("expected ')'");
      else
        pos++;
      return result;
    }
    /* A binary test needs the operator in the next position, otherwise a single
       argument is true when it is non-empty. */
    if (pos + 1 < args.size() && is_binary_operator(args[pos + 1])) {
      if (pos + 2 >= args.size()) {
        fail(StringView{"argument expected after '"} + args[pos + 1] + "'");
        pos = args.size();
        return false;
      }
      const String &left = args[pos];
      const String &op = args[pos + 1];
      const String &right = args[pos + 2];
      pos += 3;
      return evaluate_binary(left, op, right);
    }
    if (is_unary_operator(current()) && pos + 1 < args.size()) {
      const String &op = args[pos];
      const String &operand = args[pos + 1];
      pos += 2;
      return evaluate_unary(op, operand);
    }
    const bool result = !current().empty();
    pos++;
    return result;
  }

  bool parse_term()
  {
    bool result = parse_factor();
    while (!at_end() && current() == "-a") {
      pos++;
      bool right = parse_factor();
      result = result && right;
    }
    return result;
  }

  bool parse_expression()
  {
    bool result = parse_term();
    while (!at_end() && current() == "-o") {
      pos++;
      bool right = parse_term();
      result = result || right;
    }
    return result;
  }
};

} /* namespace */

Test::Test() = default;

Builtin::Kind Test::kind() const { return Kind::Test; }

i32 Test::execute(ExecContext &ec, EvalContext &cxt) const
{
  unused(cxt);

  /* Strip the program name, and for the [ form the required trailing ]. The
     last operand index ends the expression, one before the trailing ] in the
     bracket form. */
  const ArrayList<String> &arguments = ec.args();
  usize expression_end = arguments.size();
  if (ec.program() == "[") {
    if (arguments.size() < 2 || arguments[arguments.size() - 1] != "]") {
      shit::print_error("[: missing closing ']'\n");
      return 2;
    }
    expression_end = arguments.size() - 1;
  }

  ArrayList<String> operands{};
  for (usize i = 1; i < expression_end; i++)
    operands.push(arguments[i]);

  /* An empty expression is false, as POSIX specifies for test with no
     arguments. */
  if (operands.empty()) return 1;

  TestEvaluator evaluator{operands, 0, false};
  const bool result = evaluator.parse_expression();
  if (evaluator.had_error) return 2;
  if (evaluator.pos != operands.size()) {
    shit::print_error(StringView{"test: unexpected argument '"} +
                      operands[evaluator.pos] + "'\n");
    return 2;
  }
  return result ? 0 : 1;
}

} /* namespace shit */
