#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* No flags. */

namespace shit {

Cd::Cd() = default;

Builtin::Kind Cd::kind() const { return Kind::Cd; }

i32 Cd::execute(ExecContext &ec, EvalContext &cxt) const
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
    Maybe<Path> p = os::get_home_directory();
    if (!p) throw Error{"Could not figure out home directory"};
    arg_path.append(p->text());
  }

  Path target{arg_path};
  target = target.to_absolute().normalized();

  if (target.exists()) {
    /* Track the directory move in OLDPWD and PWD, as a POSIX shell does. An
       unreadable current directory yields an empty path, so OLDPWD stays as it
       was. */
    Path old_directory = Path::current_directory();
    ErrorOr<Ok> changed = Path::set_current_directory(target);
    SHIT_UNUSED(changed);
    if (!old_directory.empty())
      cxt.set_shell_variable("OLDPWD", old_directory.text());
    cxt.set_shell_variable("PWD", target.text());
    return 0;
  }

  throw Error{StringView{"Path '"} + arg_path + "' does not exist"};
}

} /* namespace shit */
