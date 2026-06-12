#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* let evaluates each argument as an arithmetic expression. The status is zero
   when the last expression evaluates to a non-zero value and one when it
   evaluates to zero, the same result ((...)) reports. It is an error with no
   expression. */

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

pure Builtin::Kind Let::kind() const wontthrow { return Kind::Let; }

i32 Let::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (ec.args().count() < 2)
    throw Error{"Unable to evaluate let because it was given no expression"};

  /* Each argument is one expression, evaluated in order so an earlier
     assignment is visible to a later one, and the last value decides the
     status. */
  LOG(verbosity::Debug, "let evaluating %zu arithmetic expressions",
      ec.args().count() - 1);

  i64 last = 0;
  for (usize i = 1; i < ec.args().count(); i++)
    last = cxt.evaluate_arithmetic(ec.args()[i].view());

  return last != 0 ? 0 : 1;
}

} /* namespace shit */
