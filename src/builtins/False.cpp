#include "../Builtin.hpp"
#include "../Eval.hpp"

/* Always returns failure. */

namespace shit {

False::False() = default;

pure fn False::kind() const wontthrow -> Builtin::Kind { return Kind::False; }

fn False::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(ec);
  unused(cxt);
  return 1;
}

} /* namespace shit */
