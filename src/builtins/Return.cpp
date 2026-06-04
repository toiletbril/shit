#include "../Builtin.hpp"
#include "../Eval.hpp"

#include <cstdlib>

/* No flags. */

namespace shit {

Return::Return() = default;

Builtin::Kind
Return::kind() const
{
  return Kind::Return;
}

i32
Return::execute(ExecContext &ec, EvalContext &cxt) const
{
  /* return with no argument uses the status of the last command. */
  i64 status = ec.args().size() > 1 ? std::atoll(ec.args()[1].c_str())
                                    : cxt.last_exit_status();

  throw FunctionReturn{status};
}

} /* namespace shit */
