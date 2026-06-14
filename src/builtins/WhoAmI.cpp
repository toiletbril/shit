#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

HELP_DESCRIPTION_DECL(
    "The whoami builtin prints the name of the current user. The status is "
    "non-zero when the user cannot be determined.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(WhoAmI);

namespace shit {

WhoAmI::WhoAmI() = default;

pure Builtin::Kind WhoAmI::kind() const wontthrow { return Kind::WhoAmI; }

i32 WhoAmI::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(Debug, "whoami printing the current user name");

  let p = String{};

  if (let const u = os::get_current_user(); u.has_value()) {
    p.append(u->view());
    p += '\n';
    ec.print_to_stdout(p);
    return 0;
  }

  return 1;
}

} /* namespace shit */
