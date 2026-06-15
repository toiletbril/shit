#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* calc evaluates each argument as an arithmetic expression and prints the value
   of the last one. It reuses the shell arithmetic evaluator, so the same
   operators and the same located error messages apply. In the default mood the
   value is computed in 128 bits, so a result wider than a signed 64-bit integer
   prints in full. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("expression ...");

HELP_DESCRIPTION_DECL(
    "The calc builtin evaluates each argument as an arithmetic expression and "
    "prints the value of the last one. In the default mood it computes in 128 "
    "bits.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Calc);

namespace shit {

Calc::Calc() = default;

pure Builtin::Kind Calc::kind() const wontthrow { return Kind::Calc; }

i32 Calc::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const operands = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* operands[0] is the program name the way every builtin reads it, so the
     expressions start at index one. */
  if (operands.count() < 2) return report_usage_error(ec, cxt, ec.program());

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

  bool nonzero = false;
  String result{};
  try {
    result = cxt.evaluate_arithmetic_wide(expression.view(), nonzero);
  } catch (const ErrorWithLocation &error) {
    /* The evaluator caret points into the joined expression, which is not the
       command text the top-level handler renders against, so the failure is
       reported as a clear named message that quotes the expression instead. */
    report_soft_builtin_error(
        ec, cxt, "cannot evaluate '" + expression + "', " + error.message());
    return 1;
  } catch (const Error &error) {
    report_soft_builtin_error(
        ec, cxt, "cannot evaluate '" + expression + "', " + error.message());
    return 1;
  }

  result += '\n';
  ec.print_to_stdout(result);
  return 0;
}

} /* namespace shit */
