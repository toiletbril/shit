#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* kill sends a signal to a job or a process. The signal defaults to TERM and is
   named with a leading minus, such as -KILL, -9, or -SIGKILL. A target with a
   leading percent names a job, otherwise it is a process id. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("kill [-signal] %job|pid [...]");

namespace shit {

Kill::Kill() = default;

pure fn Kill::kind() const wontthrow -> Builtin::Kind { return Kind::Kill; }

fn Kill::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  usize first_target = 1;
  let signal_number = os::signal_number_from_name("TERM").value_or(15);

  /* A leading -name or -number names the signal to send. */
  if (args.count() > 1 && args[1].length() > 1 && args[1][0] == '-') {
    let const name = String{args[1].substring(1)};
    let const resolved = os::signal_number_from_name(name);
    if (!resolved) throw Error{"Kill: '" + name + "' is not a valid signal"};
    signal_number = *resolved;
    first_target = 2;
  }

  if (first_target >= args.count())
    throw Error{"Kill: a job or a process id is required"};

  i32 status = 0;
  for (usize i = first_target; i < args.count(); i++) {
    const String &target = args[i];
    const String target_text = target;

    os::process pid{};
    if (!target.is_empty() && target[0] == '%') {
      const ErrorOr<i64> parsed =
          utils::parse_decimal_integer(StringView{target}.substring(1));
      if (parsed.is_error()) {
        show_message(Error{"Kill: '" + target_text +
                           "' is not a valid job or process id"}
                         .to_string());
        status = 1;
        continue;
      }
      job *const job = cxt.find_job(static_cast<int>(parsed.value()));
      if (job == nullptr) {
        show_message(Error{"Kill: '" + target_text + "' is not a known job"}
                         .to_string());
        status = 1;
        continue;
      }
      ASSERT(job != nullptr);
      pid = job->pid;
    } else {
      const ErrorOr<i64> parsed = utils::parse_decimal_integer(target);
      if (parsed.is_error()) {
        /* A non-numeric target must not fall through to kill(0), which would
           signal the whole process group including this shell. */
        show_message(Error{"Kill: '" + target_text +
                           "' is not a valid job or process id"}
                         .to_string());
        status = 1;
        continue;
      }
      pid = os::process_from_pid(parsed.value());
    }

    if (!os::signal_process(pid, signal_number)) {
      show_message(Error{"Kill: could not signal '" + target_text +
                         "': " + os::last_system_error_message()}
                       .to_string());
      status = 1;
    }
  }

  return status;
}

} /* namespace shit */
