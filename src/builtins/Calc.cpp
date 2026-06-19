#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Lexer.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* calc evaluates each argument as an arithmetic expression and prints the value
   of the last one. It reuses the shell arithmetic evaluator, so the same
   operators and the same located error messages apply. In the default mood the
   value is computed in 128 bits, so a result wider than a signed 64-bit integer
   prints in full. With no expression on a terminal, or with -i, it reads
   expressions interactively the way a freestanding calc does. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-i] [expression ...]");

HELP_DESCRIPTION_DECL(
    "The calc builtin evaluates each argument as an arithmetic expression and "
    "prints the value of the last one. In the default mood it computes in 128 "
    "bits. With no expression on a terminal, or with -i, it reads expressions "
    "interactively.");

FLAG(CALC_INTERACTIVE, Bool, 'i', "interactive",
     "Read and evaluate expressions interactively.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Calc);

namespace shit {

Calc::Calc() = default;

pure Builtin::Kind Calc::kind() const wontthrow { return Kind::Calc; }

namespace {

/* Evaluate one expression and print its value, returning 0, or render a located
   error against the expression and return 1. The evaluator caret indexes the
   expression text, so rendering against it points the caret under the
   offending token rather than the flat message the old path printed. */
fn evaluate_one(ExecContext &ec, EvalContext &cxt, StringView expression) throws
    -> i32
{
  bool nonzero = false;
  try {
    String result = cxt.evaluate_arithmetic_wide(expression, nonzero);
    result += '\n';
    ec.print_to_stdout(result);
    return 0;
  } catch (const ErrorWithLocation &error) {
    show_message(error.to_string(expression));
    return 1;
  } catch (const Error &error) {
    report_soft_builtin_error(ec, cxt,
                              "cannot evaluate '" + String{expression} + "', " +
                                  error.message());
    return 1;
  }
}

/* Apply a calc REPL assignment of the form name=value, returning true when the
   line was an assignment so the caller does not also evaluate it. The value is
   stored as text after one eager evaluation validates it is a number or an
   evaluatable expression, so a stored formula recomputes lazily on each later
   reference. A == comparison is left for the evaluator, and a bad value reports
   a located error and leaves the variable unset. */
fn try_assignment(ExecContext &ec, EvalContext &cxt, StringView line) throws
    -> bool
{
  usize i = 0;
  while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= line.length || !lexer::is_variable_name_start(line[i])) return false;

  let const name_start = i;
  while (i < line.length && lexer::is_variable_name(line[i]))
    i++;
  let const name = line.substring_of_length(name_start, i - name_start);

  while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
    i++;

  /* A single = assigns, while == is a comparison the evaluator handles. */
  if (i >= line.length || line[i] != '=') return false;
  if (i + 1 < line.length && line[i + 1] == '=') return false;

  usize value_start = i + 1;
  while (value_start < line.length &&
         (line[value_start] == ' ' || line[value_start] == '\t'))
    value_start++;
  usize value_end = line.length;
  while (value_end > value_start &&
         (line[value_end - 1] == ' ' || line[value_end - 1] == '\t'))
    value_end--;
  let const value = line.substring_of_length(value_start, value_end - value_start);

  if (value.is_empty()) {
    report_soft_builtin_error(ec, cxt, "calc: assignment to '" + String{name} +
                                           "' needs a value");
    return true;
  }

  bool nonzero = false;
  try {
    String result = cxt.evaluate_arithmetic_wide(value, nonzero);
    cxt.set_shell_variable(name, value);
    result += '\n';
    ec.print_to_stdout(result);
  } catch (const ErrorWithLocation &error) {
    show_message(error.to_string(value));
  } catch (const Error &error) {
    report_soft_builtin_error(ec, cxt, "cannot evaluate '" + String{value} +
                                           "', " + error.message());
  }
  return true;
}

/* Read expressions from the input descriptor and evaluate each, the desk
   calculator loop. A prompt prints to standard error when the input is a
   terminal so it stays out of a captured standard output. The loop ends on end
   of input or an interrupt, and a bad line reports and continues rather than
   ending the session. */
fn run_repl(ExecContext &ec, EvalContext &cxt) throws -> i32
{
  let const input_fd = ec.in_fd.value_or(SHIT_STDIN);
  let const is_prompted = os::is_fd_a_tty(input_fd);

  loop
  {
    if (is_prompted) shit::print_error("calc> ");

    bool was_delimiter_terminated = false;
    Maybe<String> line =
        utils::read_line_from_fd(input_fd, was_delimiter_terminated);
    if (!line.has_value() || os::INTERRUPT_REQUESTED) break;
    if (line->view().is_empty()) continue;
    if (try_assignment(ec, cxt, line->view())) continue;

    evaluate_one(ec, cxt, line->view());
  }

  if (is_prompted) shit::print_error("\n");
  return 0;
}

} /* namespace */

i32 Calc::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const operands = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* With -i, or with no expression on a terminal, calc reads expressions
     interactively. A piped run with no expression keeps the usage error so it
     does not hang waiting on input that is not coming. */
  let const has_expression = operands.count() >= 2;
  let const is_interactive =
      FLAG_CALC_INTERACTIVE.is_enabled() ||
      (!has_expression && os::is_stdin_a_tty() && os::is_stdout_a_tty());
  if (is_interactive) return run_repl(ec, cxt);

  if (!has_expression) return report_usage_error(ec, cxt, ec.program());

  LOG(Debug, "calc evaluating %zu arithmetic expressions",
      operands.count() - 1);

  /* The arguments join into one expression so `calc 1 + 2` reads as a single
     arithmetic expression rather than three separate ones, the way a desk
     calculator and a bare $(( )) do. */
  String expression{};
  for (usize i = 1; i < operands.count(); i++) {
    if (i > 1) expression += ' ';
    expression += operands[i].view();
  }

  return evaluate_one(ec, cxt, expression.view());
}

} /* namespace shit */
