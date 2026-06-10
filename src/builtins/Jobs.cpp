#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Colors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* jobs lists the background jobs and the state each one is in, then forgets the
   ones that have finished, the same as a shell prompt would. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-lnprs] [jobspec ...]");
HELP_DESCRIPTION_DECL(
    "The jobs builtin lists the background jobs and the state each one is in, "
    "then forgets the jobs that have finished. A jobspec argument restricts "
    "the "
    "listing to that job, where %N or N names job N, %+ the current job, and "
    "%- "
    "the previous one.");

FLAG(JOBS_LONG, Bool, 'l', "",
     "List the process id in addition to the normal information.");
FLAG(JOBS_PIDS, Bool, 'p', "", "List process ids only.");
FLAG(JOBS_RUNNING, Bool, 'r', "", "Restrict the output to running jobs.");
FLAG(JOBS_STOPPED, Bool, 's', "", "Restrict the output to stopped jobs.");
FLAG(JOBS_CHANGED, Bool, 'n', "",
     "List only jobs that changed state since the last notification.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace {

pure fn state_word(job::State state) wontthrow -> const char *
{
  switch (state) {
  case job::State::Running: return "Running";
  case job::State::Stopped: return "Stopped";
  case job::State::Done: return "Done";
  }
  return "Unknown";
}

/* The bash current-job marker, '+' for the most recent job, '-' for the one
   before it, and a space otherwise. The two markers track the last and the
   second-to-last entries in the table, the order register_job appends them. */
pure fn job_marker(const ArrayList<job> &jobs, usize index) wontthrow -> char
{
  if (jobs.is_empty()) return ' ';
  if (index == jobs.count() - 1) return '+';
  if (jobs.count() >= 2 && index == jobs.count() - 2) return '-';
  return ' ';
}

/* The SGR escape coloring a state word, or an empty view when color is off. The
   completion-style gate keeps a pipe or a script plain, since color is on only
   at an interactive prompt whose stdout is a terminal with NO_COLOR unset and
   TERM not dumb. */
fn state_color(job::State state, bool may_color) throws -> StringView
{
  if (!may_color) return StringView{};
  switch (state) {
  case job::State::Running: return colors::ansi::BOLD_GREEN;
  case job::State::Stopped: return colors::ansi::BOLD_YELLOW;
  case job::State::Done: return colors::ansi::DIM;
  }
  return StringView{};
}

/* Whether the jobs listing may carry color. The shell colors only at an
   interactive prompt whose stdout is a terminal, with NO_COLOR unset or empty
   and TERM not dumb, so a script or a pipe gets plain text and stays
   sh-compatible. */
fn may_color_jobs(EvalContext &cxt) throws -> bool
{
  return cxt.shell_is_interactive() && colors::stdout_wants_color();
}

/* Resolve a jobspec to the index in the table, where %N or a bare N names job
   N, %+ or %% the current job, and %- the previous one. None when no job
   matches, which the caller reports as a no-such-job error. */
fn resolve_jobspec(const ArrayList<job> &jobs, StringView spec) throws
    -> Maybe<usize>
{
  if (jobs.is_empty()) return shit::None;
  StringView body = spec;
  if (!body.is_empty() && body[0] == '%') body = body.substring(1);
  if (body.is_empty() || body == "+" || body == "%") return jobs.count() - 1;
  if (body == "-")
    return jobs.count() >= 2 ? jobs.count() - 2 : jobs.count() - 1;

  if (let const parsed = utils::parse_decimal_integer(body); !parsed.is_error())
  {
    for (usize i = 0; i < jobs.count(); i++)
      if (static_cast<i64>(jobs[i].id) == parsed.value()) return i;
  }
  return shit::None;
}

} /* namespace */

Jobs::Jobs() = default;

pure fn Jobs::kind() const wontthrow -> Builtin::Kind { return Kind::Jobs; }

fn Jobs::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const names = parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  cxt.update_jobs();

  let const may_color = may_color_jobs(cxt);
  const ArrayList<job> &jobs = cxt.jobs();

  /* A jobspec argument restricts the listing to the named jobs, otherwise every
     job is shown. An unresolved jobspec is a no-such-job error and the exit
     status turns non-zero while the remaining specs still list. */
  let selected = ArrayList<usize>{};
  i32 status = 0;
  if (names.count() > 1) {
    for (usize a = 1; a < names.count(); a++) {
      if (let const index = resolve_jobspec(jobs, names[a].view());
          index.has_value())
      {
        selected.push(*index);
      } else {
        show_message("Unable to list the job '" + names[a] +
                     "' because no such job exists");
        status = 1;
      }
    }
  } else {
    for (usize i = 0; i < jobs.count(); i++)
      selected.push(i);
  }

  let out = String{};
  for (usize index : selected) {
    const job &job = jobs[index];

    if (FLAG_JOBS_RUNNING.is_enabled() && job.state != job::State::Running)
      continue;
    if (FLAG_JOBS_STOPPED.is_enabled() && job.state != job::State::Stopped)
      continue;

    /* -p prints the process id alone, the form a script feeds to kill. */
    if (FLAG_JOBS_PIDS.is_enabled()) {
      out += utils::int_to_text(os::process_id_of(job.pid));
      out.push('\n');
      continue;
    }

    out += "[" + utils::int_to_text(job.id) + "]";
    out.push(job_marker(jobs, index));
    out += " ";

    /* -l inserts the process id between the job number and the state word. */
    if (FLAG_JOBS_LONG.is_enabled()) {
      out += utils::int_to_text(os::process_id_of(job.pid));
      out += " ";
    }

    out.append(state_color(job.state, may_color));
    /* The state word is padded to the width of the longest word so the command
       column lines up the way bash aligns the listing. */
    StringView state = StringView{state_word(job.state)};
    out.append(state);
    for (usize pad = state.length; pad < 7; pad++)
      out.push(' ');
    if (may_color) out += colors::ansi::RESET;

    out += "  ";
    out += job.command.c_str();
    out.push('\n');
  }
  ec.print_to_stdout(out);

  cxt.forget_done_jobs();
  return status;
}

} /* namespace shit */
