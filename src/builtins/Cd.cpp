#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[dir]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Cd::Cd() = default;

pure fn Cd::kind() const wontthrow -> Builtin::Kind { return Kind::Cd; }

fn Cd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let arg_path = String{};

  /* A lone dash operand names the previous directory, so cd - moves to OLDPWD
     and prints the directory it lands in, the way POSIX and dash do. */
  let const is_to_previous = ec.args().count() == 2 && ec.args()[1] == "-";

  if (is_to_previous) {
    let const old_directory = cxt.get_variable_value("OLDPWD");
    if (!old_directory || old_directory->is_empty())
      throw Error{"OLDPWD not set"};
    arg_path.append(old_directory->view());
  } else if (ec.args().count() > 1) {
    arg_path.append(ec.args()[1]);
    for (usize i = 2; i < ec.args().count(); i++) {
      arg_path += ' ';
      arg_path.append(ec.args()[i]);
    }
  } else {
    /* Empty cd should go to the home directory. */
    let const p = os::get_home_directory();
    if (!p) throw Error{"Could not figure out home directory"};
    arg_path.append(p->text());
  }

  Path target{arg_path};
  target = target.to_absolute().normalized();

  if (target.exists()) {
    /* Track the directory move in OLDPWD and PWD, as a POSIX shell does. An
       unreadable current directory yields an empty path, so OLDPWD stays as it
       was. */
    let const old_directory = Path::current_directory();
    /* A path that exists can still refuse the move, a regular file or a
       directory without execute permission among them. dash reports the
       failure, exits non-zero, and leaves PWD and OLDPWD untouched, so the
       chdir result drives an early throw before either variable is rewritten.
     */
    if (Path::set_current_directory(target).is_error())
      throw Error{StringView{"Could not cd to '"} + arg_path + "'"};
    /* A relative PATH entry, or the current directory as an empty entry, now
       names a different directory, so a cached resolution may point at the old
       cwd. The cache is marked stale so the next command re-resolves, the way
       dash rehashes after a cd. */
    utils::invalidate_path_cache();
    if (!old_directory.is_empty())
      cxt.set_shell_variable("OLDPWD", old_directory.text());
    cxt.set_shell_variable("PWD", target.text());
    /* cd - reports the directory it moved to, so a script that toggles between
       two directories sees where it landed. A plain cd stays silent. */
    if (is_to_previous) ec.print_to_stdout(target.text() + "\n");
    return 0;
  }

  throw Error{StringView{"Path '"} + arg_path + "' does not exist"};
}

} /* namespace shit */
