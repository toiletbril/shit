#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[%job|pid ...]");

HELP_DESCRIPTION_DECL("The wait builtin blocks until the named jobs finish.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Wait);

namespace shit {

namespace {

fn wait_for_job(job &job) throws -> i32
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
      wait_for_job(job);
    cxt.forget_done_jobs();
    return 0;
  }

  for (usize i = 1; i < args.count(); i++) {
    let const &target = args[i];

    LOG(Debug, "wait blocking on target '%s'", target.c_str());

    if (!target.is_empty() && target[0] == '%') {
      job *const matched = cxt.find_job_by_spec(target);

      if (matched != nullptr) {
        status = wait_for_job(*matched);
      } else {
        report_soft_builtin_error(ec, cxt, target + ": no such job",
                                  "List the running jobs with `jobs`");
        status = 127;
      }
    } else {
      let const parsed = target.to<i64>();
      if (parsed.is_error()) {
        report_soft_builtin_error(
            ec, cxt, "'" + target + "': not a pid or valid job spec");
        status = 1;
      } else {
        /* An untracked pid returns 127 with no waitpid, since waiting the raw
           pid would throw on ECHILD and abort the command. */
        job *matched = nullptr;
        for (job &job : cxt.jobs()) {
          if (os::process_has_id(job.pid, parsed.value())) {
            matched = &job;
            break;
          }
        }
        status = matched != nullptr ? wait_for_job(*matched) : 127;
      }
    }
  }

  cxt.forget_done_jobs();
  return status;
}

} // namespace shit
