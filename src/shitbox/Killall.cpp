#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-s signal] name");

HELP_DESCRIPTION_DECL(
    "The killall utility sends a signal to every process whose name matches "
    "the operand exactly. The signal defaults to TERM and may be a name or a "
    "number. The status is zero when a process matched and one when none did.");

FLAG(KILLALL_SIGNAL, String, 's', "signal",
     "The signal to send, a name such as TERM or a number such as 15.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_killall(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.count() != 1) throw Error{"killall expects one process name"};

  let const wanted = operands[0].view();
  let const signal_number = resolve_shitbox_signal(
      FLAG_KILLALL_SIGNAL.is_set() ? FLAG_KILLALL_SIGNAL.value()
                                   : StringView{});

  let const self_pid = os::get_shell_process_id();
  let const processes = os::enumerate_processes();
  bool any_signaled = false;
  for (const os::process_entry &process : processes) {
    if (process.pid == self_pid) continue;
    if (process.name == wanted) {
      if (os::signal_process(os::process_from_pid(process.pid), signal_number))
        any_signaled = true;
    }
  }

  if (!any_signaled)
    report_soft_shitbox_error(
        ec, cxt, "killall: " + String{wanted} + ": no process found");
  return any_signaled ? 0 : 1;
}

} /* namespace shitbox */

} /* namespace shit */
