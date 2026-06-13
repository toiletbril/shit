#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[dir]");

HELP_DESCRIPTION_DECL(
    "The cd builtin changes the working directory to dir, or to the home "
    "directory when no operand is given. A lone dash operand moves to the "
    "previous directory named by OLDPWD and prints where it lands, and a "
    "relative operand is searched against the directories in CDPATH. The "
    "builtin updates PWD and OLDPWD on a successful move.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Cd);

namespace shit {

Cd::Cd() = default;

pure fn Cd::kind() const wontthrow -> Builtin::Kind { return Kind::Cd; }

/* CDPATH resolves a relative operand against a list of directories. An operand
   that is absolute, or whose first component is dot or dot-dot, is taken
   relative to the current directory and skips the search, the way POSIX
   specifies. */
static fn cdpath_search_applies(const String &operand) throws -> bool
{
  if (operand.is_empty() || operand[0] == '/') return false;
  if (operand == "." || operand == "..") return false;
  if (operand.starts_with("./") || operand.starts_with("../")) return false;
  return true;
}

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
      throw Error{"Unable to return to the previous directory because OLDPWD "
                  "is not set"};
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
    if (!p) throw Error{"Unable to determine the home directory"};
    arg_path.append(p->text());
  }

  LOG(verbosity::Info, "cd changing directory to '%s'", arg_path.c_str());

  let target = Path{arg_path};

  /* The first CDPATH entry that yields an existing directory resolves the
     operand. An empty entry, including the one a leading, trailing, or doubled
     colon makes, names the current directory. A move reached through a nonempty
     entry prints the directory it landed in, the way dash announces a CDPATH
     move. */
  bool reached_through_cdpath = false;
  if (!is_to_previous && ec.args().count() > 1 &&
      cdpath_search_applies(arg_path))
  {
    if (let const cdpath = cxt.get_variable_value("CDPATH")) {
      const StringView entries = cdpath->view();
      usize start = 0;
      while (start <= entries.length) {
        usize end = start;
        while (end < entries.length && entries.data[end] != ':')
          end++;
        const StringView entry =
            entries.substring_of_length(start, end - start);
        Path candidate = entry.is_empty()
                             ? Path{arg_path}
                             : Path{entry}.push_component(arg_path.view());
        let resolved = candidate.to_absolute().normalized();
        if (resolved.is_directory()) {
          LOG(verbosity::Info, "cd resolved '%s' through CDPATH entry '%.*s'",
              arg_path.c_str(), static_cast<int>(entry.length), entry.data);
          target = steal(resolved);
          reached_through_cdpath = !entry.is_empty();
          break;
        }
        if (end >= entries.length) break;
        start = end + 1;
      }
    }
  }

  target = target.to_absolute().normalized();

  if (target.exists()) {
    /* Track the directory move in OLDPWD and PWD, as a POSIX shell does. OLDPWD
       takes the logical PWD the shell tracked, so a later cd - returns through
       the same symlinks the path was reached by, the way bash does. An unset or
       relative PWD falls back to the physical directory, and an unreadable
       current directory yields an empty path, so OLDPWD stays as it was. */
    let const logical_pwd = cxt.get_variable_value("PWD");
    let const old_directory =
        (logical_pwd.has_value() && !logical_pwd->is_empty() &&
         logical_pwd->view()[0] == '/')
            ? Path{logical_pwd->view()}
            : Path::current_directory();
    /* A path that exists can still refuse the move, a regular file or a
       directory without execute permission among them. dash reports the
       failure, exits non-zero, and leaves PWD and OLDPWD untouched, so the
       chdir result drives an early throw before either variable is rewritten.
     */
    if (Path::set_current_directory(target).is_error())
      throw Error{StringView{"Unable to change directory to '"} + arg_path +
                  "' because the change failed"};
    /* A relative PATH entry, or the current directory as an empty entry, now
       names a different directory, so a cached resolution may point at the old
       cwd. The cache is marked stale so the next command re-resolves, the way
       dash rehashes after a cd. */
    utils::invalidate_path_cache();
    if (!old_directory.is_empty())
      cxt.set_shell_variable("OLDPWD", old_directory.text());
    cxt.set_shell_variable("PWD", target.text());
    /* Track the visit for the z smart-cd builtin's frecency ranking. */
    record_directory_access(target.text().view());
    /* cd - and a move through a nonempty CDPATH entry report the directory they
       moved to, so a script sees where it landed. A plain cd stays silent. */
    if (is_to_previous || reached_through_cdpath)
      ec.print_to_stdout(target.text() + "\n");
    return 0;
  }

  throw Error{StringView{"Unable to change directory because the path '"} +
              arg_path + "' does not exist"};
}

} /* namespace shit */
