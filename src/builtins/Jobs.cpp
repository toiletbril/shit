#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* jobs lists the background jobs and the state each one is in, then forgets the
   ones that have finished, the same as a shell prompt would. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("jobs");

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
  case job::State::Running: return StringView{"\x1b[1;32m"};
  case job::State::Stopped: return StringView{"\x1b[1;33m"};
  case job::State::Done: return StringView{"\x1b[2m"};
  }
  return StringView{};
}

/* Whether the jobs listing may carry color. The shell colors only at an
   interactive prompt whose stdout is a terminal, with NO_COLOR unset or empty
   and TERM not dumb, so a script or a pipe gets plain text and stays
   sh-compatible. */
fn may_color_jobs(EvalContext &cxt) throws -> bool
{
  if (!cxt.shell_is_interactive()) return false;
  if (!os::is_stdout_a_tty()) return false;

  if (let const no_color = os::get_environment_variable("NO_COLOR");
      no_color.has_value() && !no_color->is_empty())
    return false;

  if (let const term = os::get_environment_variable("TERM");
      term.has_value() && term->view() == StringView{"dumb"})
    return false;

  return true;
}

} /* namespace */

Jobs::Jobs() = default;

pure fn Jobs::kind() const wontthrow -> Builtin::Kind { return Kind::Jobs; }

fn Jobs::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(ec);

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
    if (may_color) out += "\x1b[0m";

    out += "  ";
    out += job.command.c_str();
    out.push('\n');
  }
  ec.print_to_stdout(out);

  cxt.forget_done_jobs();
  return 0;
}

} /* namespace shit */
