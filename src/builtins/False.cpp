#include "../Builtin.hpp"

/* No flags. Always returns failure. */

namespace shit {

False::False() = default;

Builtin::Kind
False::kind() const
{
  return Kind::False;
}

i32
False::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(ec);
  SHIT_UNUSED(cxt);
  return 1;
}

} /* namespace shit */
