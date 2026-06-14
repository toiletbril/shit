#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL(
    "The break builtin exits an enclosing for, while, or until loop. The "
    "optional count n names how many enclosing loops to break and defaults to "
    "one, and a count below one is rejected as an illegal number.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Break);

namespace shit {

Break::Break() = default;

pure fn Break::kind() const wontthrow -> Builtin::Kind { return Kind::Break; }

fn Break::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The optional argument is how many enclosing loops to break, default one. */
  let const level = parse_optional_integer_arg(ec, 1);
  /* A non-positive count is rejected the way dash rejects an illegal number,
     rather than clamped, so break 0 aborts instead of breaking one loop. */
  if (level < 1)
    throw Error{"Unable to break because '" + ec.args()[1] +
                "' is not a valid loop count"};

  LOG(All, "break leaving %lld enclosing loops",
      static_cast<long long>(level));
  cxt.request_break(level, ec.source_location());
  return 0;
}

} /* namespace shit */
