#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL(
    "The break builtin exits an enclosing for, while, or until loop. The "
    "optional count n names how many enclosing loops to break and defaults to "
    "one, and a count below one is rejected as an illegal number.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Break::Break() = default;

pure fn Break::kind() const wontthrow -> Builtin::Kind { return Kind::Break; }

fn Break::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The optional argument is how many enclosing loops to break, default one. */
  i64 level = 1;
  if (ec.args().count() > 1) {
    let const parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    level = parsed.value();
  }
  /* A non-positive count is rejected the way dash rejects an illegal number,
     rather than clamped, so break 0 aborts instead of breaking one loop. */
  if (level < 1)
    throw Error{"Unable to break because '" + ec.args()[1] +
                "' is not a valid loop count"};

  cxt.request_break(level, ec.source_location());
  return 0;
}

} /* namespace shit */
