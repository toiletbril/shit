#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[%job]");
HELP_DESCRIPTION_DECL("The fg builtin brings a background job to the "
                      "foreground and waits for it, "
                      "resuming the job first when it was stopped. With no "
                      "operand it acts on the "
                      "most recent job.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Fg);

namespace shit {

Fg::Fg() = default;

pure fn Fg::kind() const wontthrow -> Builtin::Kind { return Kind::Fg; }

fn Fg::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  job *job = nullptr;
  if (args.count() > 1 && !args[1].is_empty() && args[1][0] == '%') {
    let const parsed_value =
        utils::parse_decimal_integer(StringView{args[1]}.substring(1));
    if (parsed_value.is_error())
      throw Error{"'" + args[1] + "' is not a valid job"};
    job = cxt.find_job(static_cast<int>(parsed_value.value()));
  } else {
    job = cxt.most_recent_job();
  }

  if (job == nullptr) throw Error{"There is no such job"};
  ASSERT(job != nullptr);

  LOG(Info, "fg bringing job %d to the foreground", job->id);

  /* A job already reaped by a prior poll has its status recorded, so report it
     instead of waiting on a pid that no longer exists. */
  if (job->state == job::State::Done) {
    let const done_status = job->last_status;
    cxt.forget_done_jobs();
    return done_status;
  }

  /* Resume a stopped job before waiting, so fg works after a Ctrl-Z. */
  if (job->state == job::State::Stopped) {
    if (const Maybe<i32> cont = os::signal_number_from_name("CONT"))
      os::signal_process(job->pid, *cont);
  }

  ec.print_to_stdout(job->command + "\n");

  const bool reclaim =
      cxt.shell_is_interactive() && os::shell_has_controlling_terminal();
  if (reclaim) os::give_controlling_terminal_to(job->pid);
  let was_stopped = false;
  let const status = os::wait_and_monitor_process(job->pid, &was_stopped);
  if (reclaim) os::reclaim_controlling_terminal();

  if (was_stopped) {
    job->state = job::State::Stopped;
    job->last_status = status;
    cxt.notify_stopped_job(job->id, job->command.view());
    return status;
  }

  job->state = job::State::Done;
  job->last_status = status;
  cxt.forget_done_jobs();

  return status;
}

} // namespace shit
