#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* compopt changes the completion options of a command from inside a completion
   function, such as compopt -o nospace. The shell accepts the call so a
   completion function runs, while the interactive completion engine does not
   yet consult these options, so it is a no-op. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-o option] [-DEI] [+o option] [name ...]");
HELP_DESCRIPTION_DECL(
    "The compopt builtin changes the completion options of each named command, "
    "the way a bash completion function adjusts its own behavior. The shell "
    "accepts the call so a completion function runs, while the interactive "
    "completion engine does not yet consult these options, so it is a no-op.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Compopt::Compopt() = default;

pure fn Compopt::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Compopt;
}

fn Compopt::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The options are accepted and ignored, so a completion function that adjusts
     its options keeps running. */
  return 0;
}

} /* namespace shit */
