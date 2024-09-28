#include "../Builtin.hpp"
#include "../Utils.hpp"

namespace shit {

Pwd::Pwd() = default;

Builtin::Kind
Pwd::kind() const
{
  return Kind::Pwd;
}

i32
Pwd::execute(ExecContext &ec) const
{
  std::string p{};
  p = utils::get_current_directory().string();
  p += '\n';
  ec.print_to_stdout(p);
  return 0;
}

} /* namespace shit */
