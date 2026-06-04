#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

#include <cstdlib>

/* No flags. */

namespace shit {

Exit::Exit() = default;

Builtin::Kind
Exit::kind() const
{
  return Kind::Exit;
}

i32
Exit::execute(ExecContext &ec, EvalContext &cxt) const
{
  /* exit with no argument uses the status of the last command. */
  i64 status = ec.args().size() > 1 ? std::atoll(ec.args()[1].c_str())
                                    : cxt.last_exit_status();

  /* Inside a subshell or a command substitution, exit ends only that scope. */
  if (cxt.in_subshell()) throw ShellExit{status};

  utils::quit(static_cast<i32>(status), true);
}

} /* namespace shit */
