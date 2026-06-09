#include "../Builtin.hpp"
#include "../Eval.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

True::True() = default;

pure Builtin::Kind True::kind() const wontthrow { return Kind::True; }

i32 True::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  return 0;
}

} /* namespace shit */
