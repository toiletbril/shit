#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

HELP_DESCRIPTION_DECL(
    "The true builtin does nothing and always returns a success status. It "
    "ignores any arguments it is given.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

True::True() = default;

pure Builtin::Kind True::kind() const wontthrow { return Kind::True; }

i32 True::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(verbosity::All, "true returning a success status");
  return 0;
}

} /* namespace shit */
