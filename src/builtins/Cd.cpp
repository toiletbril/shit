#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[dir]");

HELP_DESCRIPTION_DECL("The cd builtin changes the working directory.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Cd);

namespace shit {

Cd::Cd() = default;

pure fn Cd::kind() const wontthrow -> Builtin::Kind { return Kind::Cd; }

/* An absolute operand, or one led by dot or dot-dot, skips the CDPATH search.
 */
static fn cdpath_search_applies(const String &operand) throws -> bool
{
  if (operand.is_empty() || os::path_is_absolute(operand.view()) ||
      os::path_is_drive_relative(operand.view()))
  {
    return false;
  }
  if (operand == "." || operand == "..") return false;
  if (operand.length() >= 2 && operand[0] == '.' &&
      os::is_directory_separator(operand[1]))
  {
    return false;
  }
  if (operand.length() >= 3 && operand[0] == '.' && operand[1] == '.' &&
      os::is_directory_separator(operand[2]))
  {
    return false;
  }
  return true;
}

fn Cd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  if (cxt.restricted_enforcement_active())
    throw ErrorWithLocation{ec.source_location(),
                            "cd is forbidden in a restricted shell"};

  let is_physical = cxt.shell_option_state(shell_option_id::Physical);
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

    is_physical = option[option.length - 1] == 'P';
    operand_index++;
  }

  let const operand_count = ec.args().count() - operand_index;

  if (operand_count > 1) {
    let too_many = ErrorWithLocationAndDetails{
        ec.source_location(), "cd accepts only a single directory operand",
        "Quote a path that contains spaces"};
    too_many.set_command_status(2);
    throw too_many;
  }

  let arg_path = String{cxt.scratch_allocator()};

  let const is_to_previous =
      operand_count == 1 && ec.args()[operand_index] == "-";

  if (is_to_previous) {
    let const old_directory = cxt.get_variable_value("OLDPWD");
    if (!old_directory.has_value() || old_directory->is_empty()) {
      throw ErrorWithLocationAndDetails{
          ec.source_location(),
          "Unable to return to the previous directory because OLDPWD is not "
          "set",
          "Change directory at least once before `cd -`"};
    }
    arg_path.append(old_directory->view());
  } else if (operand_count > 0) {
    arg_path.append(ec.args()[operand_index]);
  } else {
    let const home_directory = os::get_home_directory();
    if (!home_directory.has_value())
      throw ErrorWithLocationAndDetails{
          ec.source_location(), "Unable to determine the home directory",
          "Set `HOME` to a valid path"};
    arg_path.append(home_directory->text());
  }

  LOG(Info, "cd changing directory to '%s'", arg_path.c_str());

  let target = Path{arg_path};
  let old_directory = Path{};

  /* An empty CDPATH entry, including one a leading, trailing, or doubled colon
     makes, names the current directory. */
  let was_reached_through_cdpath = false;
  if (!is_to_previous && operand_count > 0 && cdpath_search_applies(arg_path)) {
    if (let const cdpath = cxt.get_variable_value("CDPATH")) {
      const StringView entries = cdpath->view();
      usize start = 0;
      while (start <= entries.length) {
        usize end = start;
        while (end < entries.length && entries.data[end] != os::PATH_DELIMITER)
          end++;
        const StringView entry =
            entries.substring_of_length(start, end - start);
        Path candidate = entry.is_empty()
                             ? Path{arg_path}
                             : Path{entry}.push_component(arg_path.view());
        let resolved = candidate;
        if (is_physical) {
          if (resolved.is_relative()) {
            let current_directory = Path::current_directory();
            if (current_directory.is_empty()) break;
            resolved = current_directory.push_component(resolved.text().view());
          }
          if (let canonical = os::canonical_path(resolved))
            resolved = canonical.take();
        } else {
          resolved = resolved.to_absolute().normalized();
        }
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
  if (is_physical) {
    if (target.is_relative()) {
      target = target.to_absolute();
      if (!target.is_absolute())
        throw ErrorWithLocation{
            ec.source_location(),
            StringView{"Unable to resolve '"} + arg_path +
                "' because the current directory is unavailable"};
    }

    if (let resolved = os::canonical_path(target)) target = resolved.take();
  } else if (target.is_absolute() ||
             os::path_is_drive_relative(target.text().view()))
  {
    target = target.to_absolute().normalized();
  } else {
    old_directory = logical_working_directory(cxt);
    let logical_target = Path{old_directory.text().view()};
    logical_target.push_component(target.text().view());
    logical_target = logical_target.normalized();

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

  if (target.exists()) {
    if (old_directory.is_empty())
      old_directory = logical_working_directory(cxt);
    /* A path that exists can still refuse the move, so the chdir failure throws
       before PWD and OLDPWD are rewritten, leaving them untouched like dash. */
    if (Path::set_current_directory(target).is_error()) {
      throw ErrorWithLocation{
          ec.source_location(),
          StringView{"Unable to change to the directory '"} + arg_path +
              "': " + os::last_system_error_message()};
    }
    /* A relative or empty PATH entry now names a different directory, so a
       cached resolution is marked stale for the next command to re-resolve. */
    cxt.get_program_resolver().working_directory_changed();
    if (!old_directory.is_empty())
      cxt.set_shell_variable("OLDPWD", old_directory.text());
    cxt.set_shell_variable("PWD", target.text());
    record_directory_access(target.text().view(), cxt.scratch_allocator());
    if (is_to_previous || was_reached_through_cdpath) {
      ec.print_to_stdout(target.text() + "\n");
    }
    return 0;
  }

  let details = String{cxt.scratch_allocator(),
                       "Check the spelling or create it with `mkdir -p`"};
  if (!is_to_previous && operand_count > 0) {
    if (let const suggested_name =
            utils::suggest_directory_entry(target.parent(), target.filename()))
    {
      let suggested_path = Path{arg_path.view()}.normalized().parent();
      suggested_path.push_component(suggested_name->view());
      let quoted_path = String{cxt.scratch_allocator()};
      append_shell_quoted_arg(quoted_path, suggested_path.text().view());
      details = "Did you mean `" + quoted_path + "`?";
    }
  }

  throw ErrorWithLocationAndDetails{
      ec.source_location(),
      StringView{"The directory '"} + arg_path + "' does not exist", details};
}

} // namespace shit
