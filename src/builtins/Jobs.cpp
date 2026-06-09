#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Colors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* jobs lists the background jobs and the state each one is in, then forgets the
   ones that have finished, the same as a shell prompt would. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

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

} /* namespace */

Jobs::Jobs() = default;

pure fn Jobs::kind() const wontthrow -> Builtin::Kind { return Kind::Jobs; }

fn Jobs::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  cxt.update_jobs();

  let const may_color = may_color_jobs(cxt);
  const ArrayList<job> &jobs = cxt.jobs();

  let out = String{};
  for (usize i = 0; i < jobs.count(); i++) {
    const job &job = jobs[i];

    out += "[" + utils::int_to_text(job.id) + "]";
    out.push(job_marker(jobs, i));
    out += " ";

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
  return 0;
}

} /* namespace shit */
