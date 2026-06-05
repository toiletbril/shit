#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* fg brings a background job to the foreground and waits for it, resuming it
   first if it was stopped. With no operand it acts on the most recent job. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("fg [%job]");

namespace shit {

Fg::Fg() = default;

Builtin::Kind Fg::kind() const { return Kind::Fg; }

i32 Fg::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> &args = ec.args();

  Job *job = nullptr;
  if (args.size() > 1 && !args[1].empty() && args[1][0] == '%') {
    ErrorOr<i64> parsed =
        utils::parse_decimal_integer(StringView{args[1]}.substring(1));
    if (parsed.is_error())
      throw Error{"fg: '" + args[1] + "' is not a valid job"};
    job = cxt.find_job(static_cast<int>(parsed.value()));
  } else
    job = cxt.most_recent_job();

  if (job == nullptr) throw Error{"fg: there is no such job"};

  /* A job already reaped by a prior poll has its status recorded, so report it
     instead of waiting on a pid that no longer exists. */
  if (job->state == Job::State::Done) {
    i32 done_status = job->last_status;
    cxt.forget_done_jobs();
    return done_status;
  }

  /* Resume a stopped job before waiting, so fg works after a Ctrl-Z. */
  if (job->state == Job::State::Stopped) {
    if (Maybe<i32> cont = os::signal_number_from_name("CONT"))
      os::signal_process(job->pid, *cont);
  }

  ec.print_to_stdout(job->command + "\n");

  i32 status = os::wait_and_monitor_process(job->pid);
  job->state = Job::State::Done;
  job->last_status = status;
  cxt.forget_done_jobs();

  return status;
}

} /* namespace shit */
