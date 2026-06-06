#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* No flags. */

namespace shit {

Exit::Exit() = default;

pure fn Exit::kind() const wontthrow -> Builtin::Kind { return Kind::Exit; }

fn Exit::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  /* exit with no argument uses the status of the last command. */
  i64 status = cxt.last_exit_status();
  if (ec.args().count() > 1) {
    let const parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    status = parsed.value();
  }

  /* Inside a subshell or a command substitution, exit ends only that scope. */
  if (cxt.in_subshell()) {
    cxt.request_exit(status, ec.source_location());
    return static_cast<i32>(status);
  }

  cxt.run_exit_trap();
  utils::quit(static_cast<i32>(status), true);
}

} /* namespace shit */
