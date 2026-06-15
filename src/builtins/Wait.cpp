#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* wait blocks until the named jobs finish, or until every job finishes when no
   operand is given. The status is that of the last job waited for. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[%job|pid ...]");

HELP_DESCRIPTION_DECL(
    "The wait builtin blocks until the named jobs finish, or until every job "
    "finishes when no operand is given. An operand names a job by %number or a "
    "child by pid, and the status is that of the last job waited for.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Wait);

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
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  i32 status = 0;

  if (args.count() == 1) {
    LOG(Debug, "wait blocking on every job of %zu", cxt.jobs().count());
    for (job &job : cxt.jobs())
      status = wait_for_job(job);
    cxt.forget_done_jobs();
    return status;
  }

  for (usize i = 1; i < args.count(); i++) {
    let const &target = args[i];

    LOG(Debug, "wait blocking on target '%s'", target.c_str());

    if (!target.is_empty() && target[0] == '%') {
      let const parsed =
          utils::parse_decimal_integer(StringView{target}.substring(1));
      if (!parsed.is_error()) {
        if (job *const job = cxt.find_job(static_cast<int>(parsed.value())))
          status = wait_for_job(*job);
      }
    } else {
      let const parsed = utils::parse_decimal_integer(target);
      if (!parsed.is_error()) {
        /* wait only waits for this shell's own children. A pid that names no
           tracked job is not a child, so the status is 127 and waitpid is never
           called, matching dash. Waiting the raw pid would throw on ECHILD and
           abort the command. */
        job *matched = nullptr;
        for (job &job : cxt.jobs()) {
#if SHIT_PLATFORM_IS WIN32
          /* A Windows process is a HANDLE, so the stored handle is resolved to
             its numeric process id before the operand is matched. */
          if (os::process_id_of(job.pid) == parsed.value()) {
#else
          if (job.pid == static_cast<os::process>(parsed.value())) {
#endif
            matched = &job;
            break;
          }
        }
        status = matched != nullptr ? wait_for_job(*matched) : 127;
      }
    }
    /* A non-numeric operand is ignored rather than waiting on waitpid(0), which
       would block on the whole process group. */
  }

  cxt.forget_done_jobs();
  return status;
}

} /* namespace shit */
