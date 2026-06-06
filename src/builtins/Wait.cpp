#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* wait blocks until the named jobs finish, or until every job finishes when no
   operand is given. The status is that of the last job waited for. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("wait [%job|pid ...]");

namespace shit {

namespace {

/* Wait for one job to finish, reusing a status already collected. */
i32 wait_for_job(job &job) throws
{
  if (job.state == job::State::Done) return job.last_status;
  let const status = os::wait_and_monitor_process(job.pid);
  job.state = job::State::Done;
  job.last_status = status;
  return status;
}

} /* namespace */

Wait::Wait() = default;

pure Builtin::Kind Wait::kind() const wontthrow { return Kind::Wait; }

i32 Wait::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.empty());

  i32 status = 0;

  if (args.size() == 1) {
    for (job &job : cxt.jobs())
      status = wait_for_job(job);
    cxt.forget_done_jobs();
    return status;
  }

  for (usize i = 1; i < args.size(); i++) {
    let const &target = args[i];

    if (!target.empty() && target[0] == '%') {
      let const parsed =
          utils::parse_decimal_integer(StringView{target}.substring(1));
      if (!parsed.is_error()) {
        if (job *const job = cxt.find_job(static_cast<int>(parsed.value())))
          status = wait_for_job(*job);
      }
    } else {
      let const parsed = utils::parse_decimal_integer(target);
      if (!parsed.is_error()) {
        let const pid = os::process_from_pid(parsed.value());
        status = os::wait_and_monitor_process(pid);
      }
    }
    /* A non-numeric operand is ignored rather than waiting on waitpid(0), which
       would block on the whole process group. */
  }

  cxt.forget_done_jobs();
  return status;
}

} /* namespace shit */
