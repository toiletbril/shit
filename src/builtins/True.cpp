#include "../Builtin.hpp"

/* No flags. Always returns success. */

namespace shit {

True::True() = default;

Builtin::Kind True::kind() const { return Kind::True; }

i32 True::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(ec);
  SHIT_UNUSED(cxt);
  return 0;
}

} /* namespace shit */
