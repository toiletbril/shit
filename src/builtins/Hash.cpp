#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-r] [name ...]");
HELP_DESCRIPTION_DECL(
    "The hash builtin manages the cache of resolved command locations. With -r "
    "it forgets every remembered location. The next use of each command "
    "re-resolves it on PATH.");

FLAG(RESET, Bool, 'r', "", "Forget remembered command locations.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Hash);

namespace shit {

Hash::Hash() = default;

pure fn Hash::kind() const wontthrow -> Builtin::Kind { return Kind::Hash; }

fn Hash::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (FLAG_RESET.is_enabled()) {
    LOG(Info, "hash forgetting every remembered command location");
    utils::invalidate_path_cache();
  }

  return 0;
}

} // namespace shit
