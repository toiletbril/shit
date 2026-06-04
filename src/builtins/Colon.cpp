#include "../Builtin.hpp"

/* No flags. The colon command does nothing and returns success, after its
   arguments are expanded by the caller. */

namespace shit {

Colon::Colon() = default;

Builtin::Kind
Colon::kind() const
{
  return Kind::Colon;
}

i32
Colon::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(ec);
  SHIT_UNUSED(cxt);
  return 0;
}

} /* namespace shit */
