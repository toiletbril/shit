#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-p] directory ...");

HELP_DESCRIPTION_DECL(
    "The mkdir utility creates each named directory. With -p it creates the "
    "missing parents and treats an existing directory as success.");

FLAG(MKDIR_PARENTS, Bool, 'p', "",
     "Create the missing parent directories and ignore one that already "
     "exists.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Mkdir);

namespace shit {

namespace shitbox {

/* Create one directory, with the -p case treating an already-present directory
   as success the way mkdir -p does. */
static fn make_one(StringView path, bool ignore_existing) throws -> bool
{
  if (os::make_directory(path, 0777)) return true;
  if (ignore_existing && Path{path}.is_directory()) return true;
  return false;
}

fn util_mkdir(const ExecContext &ec, EvalContext &cxt,
              const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const make_parents = FLAG_MKDIR_PARENTS.is_enabled();
  i32 status = 0;
  for (const String &operand : operands) {
    if (make_parents) {
      /* Each prefix of the path is created in turn, so a/b/c builds a, then
         a/b, then a/b/c, with an existing prefix passed over. */
      let const text = operand.view();
      for (usize i = 1; i <= text.length; i++) {
        if (i < text.length && text[i] != '/') continue;
        let const prefix = text.substring_of_length(0, i);
        if (prefix.is_empty()) continue;
        if (!make_one(prefix, true)) {
          report_soft_shitbox_error(
              ec, cxt,
              "mkdir: cannot create directory '" + String{prefix} +
                  "': " + os::last_system_error_message());
          status = 1;
          break;
        }
      }
    } else if (!make_one(operand.view(), false)) {
      report_soft_shitbox_error(ec, cxt,
                                "mkdir: cannot create directory '" + operand +
                                    "': " + os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
