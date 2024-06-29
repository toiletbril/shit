#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Utils.hpp"

namespace shit {

Cd::Cd() = default;

Builtin::Kind
Cd::kind() const
{
  return Kind::Cd;
}

i32
Cd::execute(utils::ExecContext &ec) const
{
  std::string arg_path{};

  if (ec.args().size() > 1) {
    arg_path += ec.args()[1];
    for (usize i = 2; i < ec.args().size(); i++) {
      arg_path += ' ';
      arg_path += ec.args()[i];
    }
  } else {
    /* Empty cd should go to the parent directory. */
    arg_path = "..";
  }

  std::filesystem::path np{arg_path};
  np = std::filesystem::absolute(np).lexically_normal();

  if (std::filesystem::exists(np)) {
    utils::set_current_directory(np);
    return 0;
  }

  throw Error{"Path '" + arg_path + "' does not exist"};
}

} /* namespace shit */
