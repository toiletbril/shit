#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");
HELP_DESCRIPTION_DECL(
    "The false builtin does nothing and always exits with a failure status of "
    "1, ignoring any arguments.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(False);

namespace shit {

False::False() = default;

pure fn False::kind() const wontthrow -> Builtin::Kind { return Kind::False; }

fn False::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(verbosity::All, "false returning a failure status");
  return 1;
}

} /* namespace shit */
