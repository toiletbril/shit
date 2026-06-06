#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

#include <cstdlib>

/* wait blocks until the named jobs finish, or until every job finishes when no
   operand is given. The status is that of the last job waited for. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("wait [%job|pid ...]");

namespace shit {

namespace {

/* Wait for one job to finish, reusing a status already collected. */
i32
wait_for_job(Job &job)
{
  if (job.state == Job::State::Done) return job.last_status;
  i32 status = os::wait_and_monitor_process(job.pid);
  job.state = Job::State::Done;
  job.last_status = status;
  return status;
}

} /* namespace */

Wait::Wait() = default;

Builtin::Kind
Wait::kind() const
{
  return Kind::Wait;
}

i32
Wait::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> &args = ec.args();

  i32 status = 0;

  if (args.size() == 1) {
    for (Job &job : cxt.jobs())
      status = wait_for_job(job);
    cxt.forget_done_jobs();
    return status;
  }

  for (usize i = 1; i < args.size(); i++) {
    const String &target = args[i];

    bool is_all_digits = !target.empty();
    for (usize position = 0; position < target.size(); position++) {
      char character = target[position];
      if (character < '0' || character > '9') {
        is_all_digits = false;
        break;
      }
    }

    if (!target.empty() && target[0] == '%') {
      int id = static_cast<int>(std::atoll(target.c_str() + 1));
      if (Job *job = cxt.find_job(id))
        status = wait_for_job(*job);
    } else if (is_all_digits) {
      os::process pid = os::process_from_pid(std::atoll(target.c_str()));
      status = os::wait_and_monitor_process(pid);
    }
    /* A non-numeric operand is ignored rather than waiting on waitpid(0), which
       would block on the whole process group. */
  }

  cxt.forget_done_jobs();
  return status;
}

} /* namespace shit */
