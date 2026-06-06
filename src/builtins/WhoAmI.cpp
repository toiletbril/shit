#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

WhoAmI::WhoAmI() = default;

Builtin::Kind WhoAmI::kind() const { return Kind::WhoAmI; }

i32 WhoAmI::execute(ExecContext &ec, EvalContext &cxt) const
{
  unused(cxt);

  const ArrayList<String> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  String p{};

  if (const Maybe<String> u = os::get_current_user(); u.has_value()) {
    p.append(u->view());
    p += '\n';
    ec.print_to_stdout(p);
    return 0;
  }

  return 1;
}

} /* namespace shit */
