#include "../Eval.hpp"

#include "../Builtin.hpp"

/* The eval builtin joins its arguments with spaces and runs the result in the
   current shell. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[arg ...]");

HELP_DESCRIPTION_DECL(
    "The eval builtin joins its arguments with spaces and runs the result as a "
    "command in the current shell. A return or an exit left pending by the "
    "evaluated text propagates to the enclosing function or the shell.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Eval::Eval() = default;

pure fn Eval::kind() const wontthrow -> Builtin::Kind { return Kind::Eval; }

fn Eval::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* A leading -- ends eval's own option scan, so the code that follows it runs
     even when it begins with a dash, the way bash treats eval -- "$code". */
  usize first = 1;
  if (ec.args().count() > 1 && ec.args()[1] == "--") first = 2;

  let joined = String{};
  for (usize i = first; i < ec.args().count(); i++) {
    if (i > first) joined += ' ';
    joined.append(ec.args()[i].view());
  }

  if (joined.is_empty()) return 0;

  /* eval leaves a return pending so it ends the enclosing function or the
     shell, the way dash propagates it, rather than ending the eval itself. */
  return cxt.run_source(joined, "eval", false, ec.source_location(),
                        StringView{"eval"});
}

} /* namespace shit */
