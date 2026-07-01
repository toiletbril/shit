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

/* An absolute operand, or one led by dot or dot-dot, skips the CDPATH search.
 */
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

  let is_physical = false;
  usize operand_index = 1;
  while (operand_index < ec.args().count()) {
    const StringView option = ec.args()[operand_index].view();
    if (option == "--") {
      operand_index++;
      break;
    }

    if (option.length < 2 || option[0] != '-') break;
    let is_options = true;
    for (usize k = 1; k < option.length; k++)
      if (option[k] != 'L' && option[k] != 'P') {
        is_options = false;
        break;
      }
    if (!is_options) break;

    for (usize k = 1; k < option.length; k++)
      is_physical = option[k] == 'P';
    operand_index++;
  }

  let const operand_count = ec.args().count() - operand_index;
  let arg_path = String{cxt.scratch_allocator()};

  let const is_to_previous =
      operand_count == 1 && ec.args()[operand_index] == "-";

  if (is_to_previous) {
    let const old_directory = cxt.get_variable_value("OLDPWD");
    if (!old_directory.has_value() || old_directory->is_empty()) {
      throw ErrorWithLocation{ec.source_location(),
                              "Unable to return to the previous directory "
                              "because OLDPWD is not set"};
    }
    arg_path.append(old_directory->view());
  } else if (operand_count > 0) {
    arg_path.append(ec.args()[operand_index]);
    for (usize i = operand_index + 1; i < ec.args().count(); i++) {
      arg_path += ' ';
      arg_path.append(ec.args()[i]);
    }
  } else {
    let const home_directory = os::get_home_directory();
    if (!home_directory.has_value())
      throw ErrorWithLocation{ec.source_location(),
                              "Unable to determine the home directory"};
    arg_path.append(home_directory->text());
  }

  LOG(Info, "cd changing directory to '%s'", arg_path.c_str());

  let target = Path{arg_path};

  /* An empty CDPATH entry, including one a leading, trailing, or doubled colon
     makes, names the current directory. */
  bool was_reached_through_cdpath = false;
  if (!is_to_previous && operand_count > 0 && cdpath_search_applies(arg_path)) {
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
          LOG(Info, "cd resolved '%s' through CDPATH entry '%.*s'",
              arg_path.c_str(), static_cast<int>(entry.length), entry.data);
          target = steal(resolved);
          was_reached_through_cdpath = !entry.is_empty();
          break;
        }
        if (end >= entries.length) break;
        start = end + 1;
      }
    }
  }

  /* A relative operand joins onto the logical PWD when that names a directory,
     the bash -L default, so cd .. out of a symlinked directory returns to the
     symlink's parent. */
  if (target.is_absolute()) {
    target = target.normalized();
  } else {
    let const logical_pwd = cxt.get_variable_value("PWD");
    let logical_target = Path{};
    if (logical_pwd.has_value() && !logical_pwd->is_empty() &&
        logical_pwd->view()[0] == '/')
    {
      logical_target = Path{logical_pwd->view()}
                           .push_component(target.text().view())
                           .normalized();
    }

    if (!logical_target.is_empty() && logical_target.is_directory()) {
      target = steal(logical_target);
    } else {
      target = target.to_absolute().normalized();
      /* getcwd yields an empty path when the current directory was removed, so
         the result stays relative and the throw names that failure. */
      if (!target.is_absolute())
        throw ErrorWithLocation{
            ec.source_location(),
            StringView{"Unable to resolve '"} + arg_path +
                "' because the current directory is unavailable"};
    }
  }

  if (is_physical) {
    if (let resolved = os::canonical_path(target)) target = resolved.take();
  }

  if (target.exists()) {
    /* OLDPWD takes the logical PWD so a later cd - returns through the same
       symlinks. An unset or relative PWD falls back to the physical directory.
     */
    let const logical_pwd = cxt.get_variable_value("PWD");
    let const old_directory =
        (logical_pwd.has_value() && !logical_pwd->is_empty() &&
         logical_pwd->view()[0] == '/')
            ? Path{logical_pwd->view()}
            : Path::current_directory();
    /* A path that exists can still refuse the move, so the chdir failure throws
       before PWD and OLDPWD are rewritten, leaving them untouched like dash. */
    if (Path::set_current_directory(target).is_error())
      throw ErrorWithLocation{
          ec.source_location(),
          StringView{"Unable to change to the directory '"} + arg_path + "'"};
    /* A relative or empty PATH entry now names a different directory, so a
       cached resolution is marked stale for the next command to re-resolve. */
    utils::invalidate_path_cache();
    if (!old_directory.is_empty())
      cxt.set_shell_variable("OLDPWD", old_directory.text());
    cxt.set_shell_variable("PWD", target.text());
    record_directory_access(target.text().view(), cxt.scratch_allocator());
    if (is_to_previous || was_reached_through_cdpath) {
      ec.print_to_stdout(target.text() + "\n");
    }
    return 0;
  }

  throw ErrorWithLocation{ec.source_location(), StringView{"The directory '"} +
                                                    arg_path +
                                                    "' does not exist"};
}

} // namespace shit
