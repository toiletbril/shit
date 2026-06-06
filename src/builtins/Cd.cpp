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
Cd::execute(ExecContext &ec, EvalContext &cxt) const
{
  String arg_path{};

  if (ec.args().size() > 1) {
    arg_path.append(ec.args()[1]);
    for (usize i = 2; i < ec.args().size(); i++) {
      arg_path += ' ';
      arg_path.append(ec.args()[i]);
    }
  } else {
    /* Empty cd should go to the home directory. */
    Maybe<std::filesystem::path> p = os::get_home_directory();
    if (!p) throw Error{"Could not figure out home directory"};
    std::string home = p->string();
    arg_path.append(StringView{home.data(), home.size()});
  }

  std::filesystem::path np{std::string{arg_path.c_str(), arg_path.size()}};
  np = std::filesystem::absolute(np).lexically_normal();

  if (std::filesystem::exists(np)) {
    /* Track the directory move in OLDPWD and PWD, as a POSIX shell does. */
    std::error_code cwd_error{};
    std::filesystem::path old = std::filesystem::current_path(cwd_error);
    utils::set_current_directory(np);
    if (!cwd_error) cxt.set_shell_variable("OLDPWD", old.string());
    cxt.set_shell_variable("PWD", np.string());
    return 0;
  }

  throw Error{"Path '" + std::string{arg_path.c_str(), arg_path.size()} +
              "' does not exist"};
}

} /* namespace shit */
