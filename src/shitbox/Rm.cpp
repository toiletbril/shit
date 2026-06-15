#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-rRf] path ...");

HELP_DESCRIPTION_DECL(
    "The rm utility removes each path. With -r it removes a directory and its "
    "contents. With -f a missing path is not an error.");

FLAG(RM_RECURSIVE_R, Bool, 'r', "", "Remove directories and their contents.");
FLAG(RM_RECURSIVE_UPPER, Bool, 'R', "",
     "Remove directories and their contents.");
FLAG(RM_FORCE, Bool, 'f', "", "Ignore a missing path and never prompt.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

/* Remove a path, descending into a directory first when recursive. Returns
   false on the first failure with the reason in last_system_error_message. */
static fn remove_path(StringView path, bool recursive) throws -> bool
{
  let const target = Path{path};
  if (recursive && target.is_directory() && !target.is_symbolic_link()) {
    Maybe<ArrayList<String>> names = Path::read_directory(target);
    if (names.has_value())
      for (const String &name : *names) {
        let const child = PathBuilder{path}.append(name.view()).build();
        if (!remove_path(child.text().view(), recursive)) return false;
      }
    return os::remove_directory(path);
  }
  return os::remove_file(path);
}

fn util_rm(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const force = FLAG_RM_FORCE.is_enabled();
  let const recursive =
      FLAG_RM_RECURSIVE_R.is_enabled() || FLAG_RM_RECURSIVE_UPPER.is_enabled();

  if (operands.is_empty() && !force) throw Error{"rm expects a path"};

  i32 status = 0;
  for (const String &operand : operands) {
    if (!Path{operand.view()}.exists()) {
      if (force) continue;
      report_soft_shitbox_error(ec, cxt,
                                "rm: cannot remove '" + operand +
                                    "': no such file or directory");
      status = 1;
      continue;
    }
    if (!remove_path(operand.view(), recursive)) {
      if (force) continue;
      report_soft_shitbox_error(ec, cxt,
                                "rm: cannot remove '" + operand +
                                    "': " + os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
