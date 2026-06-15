#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-signal] pid ...");

HELP_DESCRIPTION_DECL(
    "The kill utility sends a signal to each process id. The signal defaults to "
    "TERM and is named with a leading minus, such as -KILL, -9, or -SIGKILL.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Kill);

namespace shit {

namespace shitbox {

fn util_kill(const ExecContext &ec, EvalContext &cxt,
             const ArrayList<String> &args) throws -> i32
{
  /* The flag parser would reject a -9 form, so --help is matched by hand rather
     than through parse_util_operands. */
  for (usize i = 1; i < args.count(); i++)
    if (args[i] == "--help") {
      print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                      FLAG_LIST);
      return 0;
    }

  /* A leading -name or -number names the signal to send, the way the kill
     builtin reads it, otherwise the signal is TERM. */
  usize first_target = 1;
  i32 signal_number = SIGTERM;
  if (args.count() > 1 && args[1].count() > 1 && args[1][0] == '-') {
    signal_number = resolve_shitbox_signal(args[1].view().substring(1));
    first_target = 2;
  }

  if (first_target >= args.count())
    return report_usage_error(ec, cxt, args[0].view());

  i32 status = 0;
  for (usize i = first_target; i < args.count(); i++) {
    let const parsed = utils::parse_decimal_integer(args[i].view());
    if (parsed.is_error()) {
      report_soft_shitbox_error(ec, cxt,
                                "kill: '" + args[i] + "' is not a process id");
      status = 1;
      continue;
    }
    if (!os::signal_process(os::process_from_pid(parsed.value()), signal_number))
    {
      report_soft_shitbox_error(ec, cxt,
                                "kill: unable to signal '" + args[i] +
                                    "' because " +
                                    os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
