#include "../Builtin.hpp"
#include "../Utils.hpp"

/* No flags. */

namespace shit {

Exit::Exit() = default;

Builtin::Kind
Exit::kind() const
{
  return Kind::Exit;
}

i32
Exit::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  utils::quit(ec.args().size() > 1 ? std::atoi(ec.args()[1].c_str()) : 0, true);
}

} /* namespace shit */
