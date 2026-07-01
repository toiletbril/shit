#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] [-r] [-h] [%job ...]");

HELP_DESCRIPTION_DECL(
    "The disown builtin removes a job from the job table so the shell stops "
    "tracking it, and the removed job keeps running detached. With a %job "
    "operand it removes that job, with no operand it removes the most recent "
    "job, -a removes every job, and -r removes only the running jobs.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(ALL, Bool, 'a', "all", "Remove every job.");
FLAG(RUNNING, Bool, 'r', "running", "Remove only the running jobs.");
FLAG(NO_HUP, Bool, 'h', "nohup",
     "Mark the job to skip a hangup on shell exit rather than remove it.");

REGISTER_BUILTIN_FLAGS(Disown);

namespace shit {

Disown::Disown() = default;

pure fn Disown::kind() const wontthrow -> Builtin::Kind { return Kind::Disown; }

fn Disown::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const names = PARSE_BUILTIN_ARGS(ec);

  ASSERT(!names.is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (FLAG_NO_HUP.is_enabled()) return 0;

  if (FLAG_ALL.is_enabled()) {
    let ids = ArrayList<i32>{};
    for (const job &job : cxt.jobs())
      ids.push(job.id);
    for (const i32 id : ids)
      cxt.remove_job(id);
    return 0;
  }

  if (FLAG_RUNNING.is_enabled()) {
    /* The recorded state is refreshed first, so a job stopped since the last
       poll reads as Stopped and is kept rather than dropped as running. */
    cxt.update_jobs();
    let ids = ArrayList<i32>{};
    for (const job &job : cxt.jobs()) {
      if (job.state == job::State::Running) ids.push(job.id);
    }
    for (const i32 id : ids)
      cxt.remove_job(id);
    return 0;
  }

  if (names.count() <= 1) {
    let const job = cxt.most_recent_job();
    if (job == nullptr) throw Error{"There is no such job"};
    cxt.remove_job(job->id);
    return 0;
  }

  for (usize i = 1; i < names.count(); i++) {
    StringView spec = StringView{names[i]};
    if (!spec.is_empty() && spec[0] == '%') spec = spec.substring(1);

    let const parsed = utils::parse_decimal_integer(spec);
    if (parsed.is_error() || !cxt.remove_job(static_cast<i32>(parsed.value())))
      throw Error{"'" + names[i] + "' is not a valid job"};
  }

  return 0;
}

} // namespace shit
