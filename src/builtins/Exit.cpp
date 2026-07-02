#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL("The exit builtin ends the shell with a status.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Exit);

namespace shit {

Exit::Exit() = default;

pure fn Exit::kind() const wontthrow -> Builtin::Kind { return Kind::Exit; }

fn Exit::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const status = parse_optional_integer_arg(ec, cxt.last_exit_status());

  LOG(Debug, "exit ending the shell with status %lld",
      static_cast<long long>(status));

  if (cxt.in_subshell()) {
    cxt.request_exit(status, ec.source_location());
    return static_cast<i32>(status);
  }

  cxt.run_exit_trap();
  utils::quit(static_cast<i32>(status), true);
}

} // namespace shit
