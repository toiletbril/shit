#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

/* hash remembers command locations in shells that cache the PATH search. This
   shell resolves the PATH lazily per command, so there is no table to fill, and
   hash is accepted for script compatibility with None to do. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("hash [-r] [name ...]");

FLAG(RESET, Bool, 'r', "", "Forget remembered locations, a no-op here.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Hash::Hash() = default;

Builtin::Kind Hash::kind() const { return Kind::Hash; }

i32 Hash::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  const ArrayList<String> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  return 0;
}

} /* namespace shit */
