#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-c] file ...");

HELP_DESCRIPTION_DECL(
    "The touch utility creates each named file when it is missing. With -c it "
    "does not create a missing file.");

FLAG(TOUCH_NO_CREATE, Bool, 'c', "", "Do not create a file that is missing.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_touch(const ExecContext &ec, EvalContext &cxt,
              const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  if (operands.is_empty()) throw Error{"touch expects a file name"};

  i32 status = 0;
  for (const String &operand : operands) {
    if (Path{operand.view()}.exists()) continue;
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
  }
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
