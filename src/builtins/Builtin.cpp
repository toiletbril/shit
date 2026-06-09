#include "../Builtin.hpp"

#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../ResolvedCommand.hpp"
#include "../Utils.hpp"

/* builtin runs the named shell builtin with its arguments, bypassing a shell
   function of the same name and the PATH. A bare builtin is a no-op success,
   and a name that is not a registered builtin is an error. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("name [argument ...]");

HELP_DESCRIPTION_DECL(
    "The builtin builtin runs name with its arguments as a shell builtin, "
    "ignoring a shell function of the same name and never searching the PATH. "
    "A bare builtin succeeds without running anything, and a name that is not "
    "a "
    "shell builtin is an error.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

BuiltinBuiltin::BuiltinBuiltin() = default;

pure fn BuiltinBuiltin::kind() const wontthrow -> Builtin::Kind
{
  return Kind::BuiltinBuiltin;
}

fn BuiltinBuiltin::execute(ExecContext &ec, EvalContext &cxt) const throws
    -> i32
{
  /* The flags are not parsed generically, since every argument after the name
     belongs to the target builtin and must pass through untouched. Only the
     bare --help on the builtin word itself is intercepted. */
  if (ec.args().count() < 2) return 0;

  let const &name = ec.args()[1];
  if (name == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const target = search_builtin(name.view());
  if (!target.has_value()) {
    show_message("Unable to run '" + name +
                 "' because it is not a shell builtin");
    return 1;
  }

  /* The name and its arguments are forwarded to a fresh context resolved
     directly to the target builtin, so the function table and the PATH are
     both skipped. */
  let forwarded = ArrayList<String>{};
  for (usize i = 1; i < ec.args().count(); i++)
    forwarded.push(String{heap_allocator(), ec.args()[i]});
  let sub = ExecContext::from_resolved(ec.source_location(),
                                       ResolvedCommand::from_builtin(*target),
                                       steal(forwarded));
  return execute_builtin(steal(sub), cxt);
}

} /* namespace shit */
