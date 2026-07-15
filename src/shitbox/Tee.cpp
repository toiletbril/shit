#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] [file ...]");

HELP_DESCRIPTION_DECL(
    "The tee utility copies standard input to standard output and to each "
    "named "
    "file. With -a it appends to the files. The default truncates them.");

FLAG(TEE_APPEND, Bool, 'a', "", "Append to the files.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Tee);

namespace shit {

namespace shitbox {

Tee::Tee() = default;

pure fn Tee::kind() const wontthrow -> Utility::Kind { return Kind::Tee; }

fn Tee::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args,
                const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args, &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const input = read_fd_to_string(ec.in_fd.value_or(SHIT_STDIN));
  if (os::INTERRUPT_REQUESTED) return 130;
  if (!input.has_value()) {
    report_soft_shitbox_error(
        ec, cxt, "tee: read failed: " + os::last_system_error_message());
    return 1;
  }

  ec.print_to_stdout(input->view());

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
    /* write_fd returns one write's count, which can fall short, so the loop
       writes the rest until the whole input lands or a write fails. */
    usize written_count = 0;
    bool did_write_fail = false;
    while (written_count < input->count()) {
      let const chunk = os::write_fd(*fd, input->view().data + written_count,
                                     input->count() - written_count);
      if (!chunk.has_value() || *chunk == 0) {
        did_write_fail = true;
        break;
      }

      written_count += *chunk;
    }

    if (did_write_fail) {
      report_soft_shitbox_error(
          ec, cxt, "tee: " + operand + ": " + os::last_system_error_message());
      status = 1;
    }
    os::close_fd(*fd);
  }

  return status;
}

} // namespace shitbox

} // namespace shit
