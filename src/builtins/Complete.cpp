#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* complete registers a programmable-completion spec for a command, the bash
   builtin a completion script calls, such as complete -o default -F _name name.
   The shell accepts the spec and its options so a bash config that registers
   completions sources cleanly. The interactive completion engine does not yet
   consult the registered specs, so the registration is recorded as a no-op. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abcdefgjksuv] [-o option] [-A action] [-G globpat] "
                   "[-W wordlist] [-F function] [-C command] [-X filterpat] "
                   "[-P prefix] [-S suffix] [-pr] [name ...]");
HELP_DESCRIPTION_DECL(
    "The complete builtin registers a programmable-completion spec for each "
    "named command, the way a bash completion script does. The shell accepts "
    "the spec and its options so a config that registers completions sources "
    "cleanly. The interactive completion engine does not yet consult the "
    "registered specs, so the registration is a no-op.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Complete::Complete() = default;

pure fn Complete::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Complete;
}

fn Complete::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The spec and its options are accepted and ignored, so a config that calls
     complete to register a completion keeps running. */
  return 0;
}

} /* namespace shit */
