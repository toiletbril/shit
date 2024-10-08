#include "../Builtin.hpp"
#include "../Utils.hpp"

namespace shit {

Exit::Exit() = default;

Builtin::Kind
Exit::kind() const
{
  return Kind::Exit;
}

i32
Exit::execute(ExecContext &ec) const
{
  utils::quit(ec.args().size() > 0 ? std::atoi(ec.args()[0].c_str()) : 0, true);
}

} /* namespace shit */
