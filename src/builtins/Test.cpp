#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

#include <filesystem>
#include <string>

namespace shit {

namespace {

bool
parse_integer(const std::string &text, i64 &out)
{
  if (text.empty()) return false;
  usize start = (text[0] == '+' || text[0] == '-') ? 1 : 0;
  if (start == text.length()) return false;
  for (usize i = start; i < text.length(); i++)
    if (text[i] < '0' || text[i] > '9') return false;
  try {
    out = std::stoll(text);
  } catch (...) {
    return false;
  }
  return true;
}

bool
path_has_mode(const std::string &path, std::filesystem::perms mask)
{
  std::error_code ec{};
  std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (ec) return false;
  return (status.permissions() & mask) != std::filesystem::perms::none;
}

/* A recursive-descent evaluator over the test arguments, following the POSIX
   test grammar so -a binds tighter than -o and ! and parentheses nest. */
struct TestEvaluator
{
  const std::vector<std::string> &args;
  usize pos;
  bool had_error;

  const std::string &
  current() const
  {
    return args[pos];
  }
  bool
  at_end() const
  {
    return pos >= args.size();
  }

  void
  fail(const std::string &message)
  {
    if (!had_error)
      shit::print_to_standard_error("test: " + message + "\n");
    had_error = true;
  }

  bool
  evaluate_unary(const std::string &op, const std::string &operand)
  {
    if (op == "-z") return operand.empty();
    if (op == "-n") return !operand.empty();
    std::error_code ec{};
    if (op == "-e") return std::filesystem::exists(operand, ec);
    if (op == "-f") return std::filesystem::is_regular_file(operand, ec);
    if (op == "-d") return std::filesystem::is_directory(operand, ec);
    if (op == "-s")
      return std::filesystem::exists(operand, ec) &&
             std::filesystem::file_size(operand, ec) > 0;
    if (op == "-r")
      return path_has_mode(operand, std::filesystem::perms::owner_read);
    if (op == "-w")
      return path_has_mode(operand, std::filesystem::perms::owner_write);
    if (op == "-x")
      return path_has_mode(operand, std::filesystem::perms::owner_exec);
    fail("unknown unary operator '" + op + "'");
    return false;
  }

  bool
  evaluate_binary(const std::string &left, const std::string &op,
                  const std::string &right)
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
    fail("unknown binary operator '" + op + "'");
    return false;
  }

  bool
  is_unary_operator(const std::string &s)
  {
    return s == "-z" || s == "-n" || s == "-e" || s == "-f" || s == "-d" ||
           s == "-s" || s == "-r" || s == "-w" || s == "-x";
  }

  bool
  is_binary_operator(const std::string &s)
  {
    return s == "=" || s == "!=" || s == "-eq" || s == "-ne" || s == "-lt" ||
           s == "-le" || s == "-gt" || s == "-ge";
  }

  bool
  parse_factor()
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
      bool result = parse_expression();
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
        fail("argument expected after '" + args[pos + 1] + "'");
        pos = args.size();
        return false;
      }
      const std::string &left = args[pos];
      const std::string &op = args[pos + 1];
      const std::string &right = args[pos + 2];
      pos += 3;
      return evaluate_binary(left, op, right);
    }
    if (is_unary_operator(current()) && pos + 1 < args.size()) {
      const std::string &op = args[pos];
      const std::string &operand = args[pos + 1];
      pos += 2;
      return evaluate_unary(op, operand);
    }
    bool result = !current().empty();
    pos++;
    return result;
  }

  bool
  parse_term()
  {
    bool result = parse_factor();
    while (!at_end() && current() == "-a") {
      pos++;
      bool right = parse_factor();
      result = result && right;
    }
    return result;
  }

  bool
  parse_expression()
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

Builtin::Kind
Test::kind() const
{
  return Kind::Test;
}

i32
Test::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  /* Strip the program name, and for the [ form the required trailing ]. The
     evaluator works over std::string, so the String arguments are copied into
     std::string operands once here. */
  std::vector<std::string> operands{};
  for (usize i = 1; i < ec.args().size(); i++) {
    const String &argument = ec.args()[i];
    operands.emplace_back(argument.c_str(), argument.size());
  }
  if (ec.program() == "[") {
    if (operands.empty() || operands.back() != "]") {
      shit::print_to_standard_error("[: missing closing ']'\n");
      return 2;
    }
    operands.pop_back();
  }

  /* An empty expression is false, as POSIX specifies for test with no
     arguments. */
  if (operands.empty()) return 1;

  TestEvaluator evaluator{operands, 0, false};
  bool result = evaluator.parse_expression();
  if (evaluator.had_error) return 2;
  if (evaluator.pos != operands.size()) {
    shit::print_to_standard_error("test: unexpected argument '" +
                                  operands[evaluator.pos] + "'\n");
    return 2;
  }
  return result ? 0 : 1;
}

} /* namespace shit */
