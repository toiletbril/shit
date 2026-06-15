#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] [file ...]");

HELP_DESCRIPTION_DECL(
    "The tee utility copies standard input to standard output and to each "
    "named "
    "file. With -a it appends to the files instead of truncating them.");

FLAG(TEE_APPEND, Bool, 'a', "", "Append to the files instead of truncating.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Tee);

namespace shit {

namespace shitbox {

fn util_tee(const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const input = read_fd_to_string(ec.in_fd.value_or(SHIT_STDIN));

  ec.print_to_stdout(input.view());

  let const mode = FLAG_TEE_APPEND.is_enabled() ? os::file_open_mode::Append
                                                : os::file_open_mode::Truncate;
  i32 status = 0;
  for (const String &operand : operands) {
    let const fd = os::open_file_descriptor(operand.view(), mode);
    if (!fd.has_value()) {
      report_soft_shitbox_error(
          ec, cxt, "tee: " + operand + ": " + os::last_system_error_message());
      status = 1;
      continue;
    }
    let const written = os::write_fd(*fd, input.view().data, input.count());
    unused(written);
    os::close_fd(*fd);
  }
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
