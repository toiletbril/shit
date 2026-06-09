#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL(
    "The continue builtin skips to the next iteration of an enclosing for, "
    "while, or until loop. The optional count n names how many enclosing loops "
    "to skip and defaults to one, and a count below one is rejected as an "
    "illegal number.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Continue::Continue() = default;

pure fn Continue::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Continue;
}

fn Continue::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The optional argument is how many enclosing loops to skip, default one. */
  i64 level = 1;
  if (ec.args().count() > 1) {
    let const parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    level = parsed.value();
  }
  /* A non-positive count is rejected the way dash rejects an illegal number,
     rather than clamped, so continue 0 aborts instead of skipping one loop. */
  if (level < 1)
    throw Error{"Unable to continue because '" + ec.args()[1] +
                "' is not a valid loop count"};

  cxt.request_continue(level, ec.source_location());
  return 0;
}

} /* namespace shit */
