#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("directory ...");

HELP_DESCRIPTION_DECL(
    "The rmdir utility removes each named directory, which must be empty.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_rmdir(const ExecContext &ec, EvalContext &cxt,
              const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  if (operands.is_empty()) throw Error{"rmdir expects a directory name"};

  i32 status = 0;
  for (const String &operand : operands) {
    if (!os::remove_directory(operand.view())) {
      report_soft_shitbox_error(ec, cxt,
                                "rmdir: failed to remove '" + operand +
                                    "': " + os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
