#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("expression ...");

HELP_DESCRIPTION_DECL(
    "The let builtin evaluates each argument as an arithmetic expression. The "
    "status is zero when the last expression evaluates to a non-zero value and "
    "one when it evaluates to zero, the result ((...)) reports.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Let);

namespace shit {

Let::Let() = default;

pure fn Let::kind() const wontthrow -> Builtin::Kind { return Kind::Let; }

fn Let::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* An empty let is an error reported with status 1, the value ((...)) gives a
     zero result, rather than the usage status 2, matching bash. */
  if (ec.args().count() < 2) {
    report_soft_builtin_error(ec, cxt, "expression expected");
    return 1;
  }

  /* An earlier assignment is visible to a later expression, since they evaluate
     in order. */
  LOG(Debug, "let evaluating %zu arithmetic expressions",
      ec.args().count() - 1);

  i64 last_value = 0;
  for (usize i = 1; i < ec.args().count(); i++)
    last_value = cxt.evaluate_arithmetic(ec.args()[i].view());

  return last_value != 0 ? 0 : 1;
}

} // namespace shit
