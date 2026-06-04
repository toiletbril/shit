#include "../Builtin.hpp"
#include "../Eval.hpp"

#include <cstdlib>

/* No flags. */

namespace shit {

Continue::Continue() = default;

Builtin::Kind
Continue::kind() const
{
  return Kind::Continue;
}

i32
Continue::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  /* The optional argument is how many enclosing loops to skip, default one. */
  i64 level = ec.args().size() > 1 ? std::atoll(ec.args()[1].c_str()) : 1;
  if (level < 1) level = 1;

  throw LoopControl{LoopControl::Kind::Continue, level};
}

} /* namespace shit */
