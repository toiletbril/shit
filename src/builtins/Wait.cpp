#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

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

i32 wait_for_job(job &job) throws
{
  if (job.state == job::State::Done || job.state == job::State::Stopped) {
    return job.last_status;
  }

  let was_stopped = false;
  let const status = os::wait_and_monitor_process(job.pid, &was_stopped);
  job.state = was_stopped ? job::State::Stopped : job::State::Done;
  job.last_status = status;
  return status;
}

} // namespace

Wait::Wait() = default;

pure fn Wait::kind() const wontthrow -> Builtin::Kind { return Kind::Wait; }

fn Wait::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
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
      let const parsed = StringView{target}.substring(1).to<i64>();
      if (!parsed.is_error()) {
        if (job *const job = cxt.find_job(static_cast<int>(parsed.value()));
            job != nullptr)
          status = wait_for_job(*job);
      }
    } else {
      let const parsed = target.to<i64>();
      if (!parsed.is_error()) {
        /* An untracked pid returns 127 with no waitpid, since waiting the raw
           pid would throw on ECHILD and abort the command. */
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
    /* A non-numeric operand is ignored, since waitpid(0) would block on the
       whole process group. */
  }

  cxt.forget_done_jobs();
  return status;
}

} // namespace shit
