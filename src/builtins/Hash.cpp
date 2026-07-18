#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-r] [name ...]");
HELP_DESCRIPTION_DECL(
    "The hash builtin manages the cache of resolved command locations.");

FLAG(RESET, Bool, 'r', "", "Forget remembered command locations.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Hash);

namespace shit {

Hash::Hash() = default;

pure fn Hash::kind() const wontthrow -> Builtin::Kind { return Kind::Hash; }

fn Hash::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (FLAG_RESET.is_enabled()) {
    LOG(Info, "hash forgetting every remembered command location");
    utils::invalidate_path_cache();
  }

  i32 status = 0;
  for (usize i = 1; i < args.count(); i++) {
    let const &name = args[i];

    LOG(Debug, "hash resolving '%s' to remember its location", name.c_str());

    if (utils::search_program_path(
            name, false, utils::program_path_requirement::Runnable, true)
            .count() == 0)
    {
      report_soft_builtin_error(ec, cxt,
                                "The command '" + name + "' was not found");
      status = 1;
    }
  }

  return status;
}

} // namespace shit
