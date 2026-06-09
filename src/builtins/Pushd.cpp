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

/* Collapse a leading home directory to ~ for display, the way bash prints the
   directory stack. */
static fn with_home_tilde(StringView path) throws -> String
{
  if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
    const StringView home_view = home->text().view();
    if (!home_view.is_empty() && path.starts_with(home_view)) {
      String out{};
      out += '~';
      out.append(path.substring(home_view.length));
      return out;
    }
  }
  return String{path};
}

/* The directory stack printed left to right, the current directory first, then
   each saved directory, home collapsed to ~. */
static fn print_directory_stack(ExecContext &ec, EvalContext &cxt) throws -> void
{
  String line{};
  line.append(with_home_tilde(Path::current_directory().text().view()));
  for (const String &directory : cxt.directory_stack()) {
    line += ' ';
    line.append(with_home_tilde(directory.view()));
  }
  line += '\n';
  ec.print_to_stdout(line);
}

/* Change the working directory and track PWD and OLDPWD, the move pushd and popd
   share with cd. */
static fn move_to_directory(EvalContext &cxt, const Path &target) throws -> void
{
  Path resolved = Path{target}.to_absolute().normalized();
  if (!resolved.exists())
    throw Error{StringView{"Path '"} + resolved.text() + "' does not exist"};
  const Path old_directory = Path::current_directory();
  if (Path::set_current_directory(resolved).is_error())
    throw Error{StringView{"Could not cd to '"} + resolved.text() + "'"};
  utils::invalidate_path_cache();
  if (!old_directory.is_empty())
    cxt.set_shell_variable("OLDPWD", old_directory.text());
  cxt.set_shell_variable("PWD", resolved.text());
}

Pushd::Pushd() = default;

pure fn Pushd::kind() const wontthrow -> Builtin::Kind { return Kind::Pushd; }

fn Pushd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  const Path current = Path::current_directory();

  /* pushd with no operand swaps the current directory with the top of the
     stack, a quick toggle between the two most recent directories. */
  if (ec.args().count() < 2) {
    Maybe<String> top = cxt.pop_directory();
    if (!top.has_value())
      throw Error{"pushd: no other directory on the stack"};
    cxt.push_directory(current.text());
    move_to_directory(cxt, Path{top->view()});
    print_directory_stack(ec, cxt);
    return 0;
  }

  cxt.push_directory(current.text());
  move_to_directory(cxt, Path{ec.args()[1]});
  print_directory_stack(ec, cxt);
  return 0;
}

Popd::Popd() = default;

pure fn Popd::kind() const wontthrow -> Builtin::Kind { return Kind::Popd; }

fn Popd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  Maybe<String> top = cxt.pop_directory();
  if (!top.has_value()) throw Error{"popd: directory stack empty"};
  move_to_directory(cxt, Path{top->view()});
  print_directory_stack(ec, cxt);
  return 0;
}

Dirs::Dirs() = default;

pure fn Dirs::kind() const wontthrow -> Builtin::Kind { return Kind::Dirs; }

fn Dirs::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  print_directory_stack(ec, cxt);
  return 0;
}

} /* namespace shit */
