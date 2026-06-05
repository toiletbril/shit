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

Builtin::Kind
Kill::kind() const
{
  return Kind::Kill;
}

i32
Kill::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> &args = ec.args();

  usize first_target = 1;
  i32 signal_number = os::signal_number_from_name("TERM").value_or(15);

  /* A leading -name or -number names the signal to send. */
  if (args.size() > 1 && args[1].length() > 1 && args[1][0] == '-') {
    StringView name_view = args[1].substring(1);
    std::string name = std::string{name_view.data, name_view.size()};
    Maybe<i32> resolved = os::signal_number_from_name(name);
    if (!resolved) throw Error{"kill: '" + name + "' is not a valid signal"};
    signal_number = *resolved;
    first_target = 2;
  }

  if (first_target >= args.size())
    throw Error{"kill: a job or a process id is required"};

  i32 status = 0;
  for (usize i = first_target; i < args.size(); i++) {
    const String &target = args[i];
    String target_text = target;

    os::process pid{};
    if (!target.empty() && target[0] == '%') {
      ErrorOr<i64> parsed =
          utils::parse_decimal_integer(StringView{target}.substring(1));
      if (parsed.is_error()) {
        show_message(Error{"kill: '" + target_text +
                           "' is not a valid job or process id"}
                         .to_string());
        status = 1;
        continue;
      }
      Job *job = cxt.find_job(static_cast<int>(parsed.value()));
      if (job == nullptr) {
        show_message(Error{"kill: '" + target_text + "' is not a known job"}
                         .to_string());
        status = 1;
        continue;
      }
      pid = job->pid;
    } else {
      ErrorOr<i64> parsed = utils::parse_decimal_integer(target);
      if (parsed.is_error()) {
        /* A non-numeric target must not fall through to kill(0), which would
           signal the whole process group including this shell. */
        show_message(Error{"kill: '" + target_text +
                           "' is not a valid job or process id"}
                         .to_string());
        status = 1;
        continue;
      }
      pid = os::process_from_pid(parsed.value());
    }

    if (!os::signal_process(pid, signal_number)) {
      show_message(Error{"kill: could not signal '" + target_text +
                         "': " + os::last_system_error_message()}
                       .to_string());
      status = 1;
    }
  }

  return status;
}

} /* namespace shit */
