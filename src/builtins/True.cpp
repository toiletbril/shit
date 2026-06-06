#include "../Builtin.hpp"
#include "../Eval.hpp"

/* Always returns success. */

namespace shit {

True::True() = default;

pure Builtin::Kind True::kind() const wontthrow { return Kind::True; }

i32 True::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(ec);
  unused(cxt);
  return 0;
}

} /* namespace shit */
