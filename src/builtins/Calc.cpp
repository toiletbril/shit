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
  if (operands.count() < 2) throw Error{"calc was given no expression"};

  LOG(Debug, "calc evaluating %zu arithmetic expressions",
      operands.count() - 1);

  /* Each argument is one expression, evaluated in order so an earlier value
     feeds a later read, and the last result is printed. */
  String result{};
  bool nonzero = false;
  for (usize i = 1; i < operands.count(); i++)
    result = cxt.evaluate_arithmetic_wide(operands[i].view(), nonzero);

  result += '\n';
  ec.print_to_stdout(result);
  return 0;
}

} /* namespace shit */
