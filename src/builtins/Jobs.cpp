#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Colors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-lnprs] [jobspec ...]");
HELP_DESCRIPTION_DECL(
    "The jobs builtin lists the background jobs and the state each one is in, "
    "then forgets the jobs that have finished. A jobspec argument restricts "
    "the listing to that job. The forms %N and N name job N, %+ names the "
    "current job, and %- names the previous one.");

FLAG(JOBS_LONG, Bool, 'l', "",
     "List the process id in addition to the normal information.");
FLAG(JOBS_PIDS, Bool, 'p', "", "List process ids only.");
FLAG(JOBS_RUNNING, Bool, 'r', "", "Restrict the output to running jobs.");
FLAG(JOBS_STOPPED, Bool, 's', "", "Restrict the output to stopped jobs.");
FLAG(JOBS_CHANGED, Bool, 'n', "",
     "List only jobs that changed state since the last notification.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Jobs);

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

pure fn job_marker(const ArrayList<job> &jobs, usize index) wontthrow -> char
{
  if (jobs.is_empty()) return ' ';
  if (index == jobs.count() - 1) return '+';
  if (jobs.count() >= 2 && index == jobs.count() - 2) return '-';
  return ' ';
}

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

fn may_color_jobs(EvalContext &cxt) throws -> bool
{
  return cxt.shell_is_interactive() && colors::stdout_wants_color();
}

fn resolve_jobspec(const ArrayList<job> &jobs, StringView spec) throws
    -> Maybe<usize>
{
  if (jobs.is_empty()) return shit::None;
  StringView body = spec;
  if (!body.is_empty() && body[0] == '%') {
    body = body.substring(1);
  }
  if (body.is_empty() || body == "+" || body == "%") return jobs.count() - 1;
  if (body == "-")
    return jobs.count() >= 2 ? jobs.count() - 2 : jobs.count() - 1;

  if (let const parsed_value = body.to<i64>(); !parsed_value.is_error()) {
    for (usize i = 0; i < jobs.count(); i++)
      if (static_cast<i64>(jobs[i].id) == parsed_value.value()) return i;
  }
  return shit::None;
}

} // namespace

Jobs::Jobs() = default;

pure fn Jobs::kind() const wontthrow -> Builtin::Kind { return Kind::Jobs; }

fn Jobs::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const names = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  cxt.update_jobs();

  let const may_color = may_color_jobs(cxt);
  let const &jobs = cxt.jobs();

  LOG(Debug, "jobs listing %zu registered jobs", jobs.count());

  let selected = ArrayList<usize>{cxt.scratch_allocator()};
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

  let out = String{cxt.scratch_allocator()};
  for (let index : selected) {
    let const &job = jobs[index];

    if (FLAG_JOBS_RUNNING.is_enabled() && job.state != job::State::Running) {
      continue;
    }
    if (FLAG_JOBS_STOPPED.is_enabled() && job.state != job::State::Stopped) {
      continue;
    }

    if (FLAG_JOBS_PIDS.is_enabled()) {
      out += String::from(os::process_id_of(job.pid), cxt.scratch_allocator());
      out.push('\n');
      continue;
    }

    out += "[" + String::from(job.id, cxt.scratch_allocator()) + "]";
    out.push(job_marker(jobs, index));
    out += " ";

    if (FLAG_JOBS_LONG.is_enabled()) {
      out += String::from(os::process_id_of(job.pid), cxt.scratch_allocator());
      out += " ";
    }

    out.append(state_color(job.state, may_color));
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

} // namespace shit
