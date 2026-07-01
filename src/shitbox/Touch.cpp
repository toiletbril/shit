#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-c] file ...");

HELP_DESCRIPTION_DECL("The touch utility sets the access and the modification "
                      "times of each named "
                      "file to the current time, creating the file when it is "
                      "missing. With -c it "
                      "does not create a missing file.");

FLAG(TOUCH_NO_CREATE, Bool, 'c', "", "Do not create a file that is missing.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Touch);

namespace shit {

namespace shitbox {

Touch::Touch() = default;

pure fn Touch::kind() const wontthrow -> Utility::Kind { return Kind::Touch; }

fn Touch::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  i32 status = 0;
  for (const String &operand : operands) {
    if (Path{operand.view()}.exists()) {
      if (!os::touch_file_times(operand.view())) {
        report_soft_shitbox_error(ec, cxt,
                                  "touch: cannot touch '" + operand +
                                      "': " + os::last_system_error_message());
        status = 1;
      }
      continue;
    }

    if (FLAG_TOUCH_NO_CREATE.is_enabled()) continue;

    let const fd =
        os::open_file_descriptor(operand.view(), os::file_open_mode::Append);
    if (!fd.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "touch: cannot touch '" + operand +
                                    "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    os::close_fd(*fd);

    if (!os::touch_file_times(operand.view())) {
      report_soft_shitbox_error(ec, cxt,
                                "touch: cannot touch '" + operand +
                                    "': " + os::last_system_error_message());
      status = 1;
    }
  }

  return status;
}

} // namespace shitbox

} // namespace shit
