#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* No flags. */

namespace shit {

Cd::Cd() = default;

Builtin::Kind
Cd::kind() const
{
  return Kind::Cd;
}

i32
Cd::execute(ExecContext &ec) const
{
  std::string arg_path{};

  if (ec.args().size() > 1) {
    arg_path += ec.args()[1];
    for (usize i = 2; i < ec.args().size(); i++) {
      arg_path += ' ';
      arg_path += ec.args()[i];
    }
  } else {
    /* Empty cd should go to the home directory. */
    std::optional<std::filesystem::path> p = os::get_home_directory();
    if (!p) throw Error{"Could not figure out home directory"};
    arg_path = p->string();
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
