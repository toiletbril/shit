#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("directory ...");

HELP_DESCRIPTION_DECL(
    "The rmdir utility removes each named directory, which must be empty.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Rmdir);

namespace shit {

namespace shitbox {

Rmdir::Rmdir() = default;

pure Utility::Kind Rmdir::kind() const wontthrow { return Kind::Rmdir; }

fn Rmdir::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

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

} // namespace shitbox

} // namespace shit
