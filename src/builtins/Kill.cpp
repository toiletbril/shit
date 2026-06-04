#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

#include <cstdlib>

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
  const std::vector<std::string> &args = ec.args();

  usize first_target = 1;
  i32 signal_number = os::signal_number_from_name("TERM").value_or(15);

  /* A leading -name or -number names the signal to send. */
  if (args.size() > 1 && args[1].length() > 1 && args[1][0] == '-') {
    std::string name = args[1].substr(1);
    Maybe<i32> resolved = os::signal_number_from_name(name);
    if (!resolved)
      throw Error{"kill: '" + name + "' is not a valid signal"};
    signal_number = *resolved;
    first_target = 2;
  }

  if (first_target >= args.size())
    throw Error{"kill: a job or a process id is required"};

  i32 status = 0;
  for (usize i = first_target; i < args.size(); i++) {
    const std::string &target = args[i];

    os::process pid{};
    if (!target.empty() && target[0] == '%') {
      int id = static_cast<int>(std::atoll(target.c_str() + 1));
      Job *job = cxt.find_job(id);
      if (job == nullptr) {
        show_message(
            Error{"kill: '" + target + "' is not a known job"}.to_string());
        status = 1;
        continue;
      }
      pid = job->pid;
    } else {
      pid = os::process_from_pid(std::atoll(target.c_str()));
    }

    if (!os::signal_process(pid, signal_number)) {
      show_message(Error{"kill: could not signal '" + target +
                         "': " + os::last_system_error_message()}
                       .to_string());
      status = 1;
    }
  }

  return status;
}

} /* namespace shit */
