#include "../Builtin.hpp"
#include "../Eval.hpp"

/* Always returns success. */

namespace shit {

True::True() = default;

Builtin::Kind True::kind() const { return Kind::True; }

i32 True::execute(ExecContext &ec, EvalContext &cxt) const
{
  unused(ec);
  unused(cxt);
  return 0;
}

} /* namespace shit */
