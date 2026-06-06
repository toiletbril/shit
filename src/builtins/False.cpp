#include "../Builtin.hpp"
#include "../Eval.hpp"

/* Always returns failure. */

namespace shit {

False::False() = default;

fn False::kind() const -> Builtin::Kind { return Kind::False; }

fn False::execute(ExecContext &ec, EvalContext &cxt) const -> i32
{
  SHIT_UNUSED(ec);
  SHIT_UNUSED(cxt);
  return 1;
}

} /* namespace shit */
