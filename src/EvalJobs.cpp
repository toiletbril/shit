#include "Common.hpp"
#include "Debug.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

fn EvalContext::set_last_background_pid(i64 pid) wontthrow -> void
{
  m_last_background_pid = pid;
}

fn EvalContext::register_job(os::process pid, StringView command) throws -> i32
{
  let new_job = job{};
  new_job.id = m_next_job_id++;
  new_job.pid = pid;
  new_job.process_id = os::process_id_of(pid);
  new_job.command = command;
  new_job.state = job::State::Running;
  m_jobs.push(steal(new_job));
  ASSERT(!m_jobs.is_empty());
  LOG(Info, "registered job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn EvalContext::register_pipeline_job(const ArrayList<os::process> &processes,
                                      os::process primary_process,
                                      StringView command) throws -> i32
{
  let new_job = job{};
  new_job.id = m_next_job_id++;
  new_job.pid = primary_process;
  new_job.process_id = os::process_id_of(primary_process);
  new_job.command = command;
  bool did_skip_primary = false;

  for (let const process : processes) {
    if (!did_skip_primary && process == primary_process) {
      did_skip_primary = true;
      continue;
    }
    new_job.earlier_pipeline_processes.push(process);
  }

  m_jobs.push(steal(new_job));
  ASSERT(!m_jobs.is_empty());
  LOG(Info, "registered pipeline job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn EvalContext::register_stopped_job(os::process pid, StringView command,
                                     i32 status) throws -> i32
{
  let const id = register_job(pid, command);
  job &registered = m_jobs.back();
  registered.state = job::State::Stopped;
  registered.last_status = status;
  return id;
}

fn EvalContext::notify_stopped_job(i32 id, StringView command) throws -> void
{
  print_error("[" + String::from(id, heap_allocator()) + "]+ Stopped  " +
              String{command} + "\n");
}

static fn poll_owned_processes(ArrayList<os::process> &processes) wontthrow
    -> void
{
  for (usize process_position = processes.count(); process_position > 0;
       process_position--)
  {
    i32 status = 0;
    let const state = os::poll_process(processes[process_position - 1], status);
    if (state == os::process_state::Exited)
      processes.remove(process_position - 1);
  }
}

fn EvalContext::update_jobs() throws -> void
{
  poll_owned_processes(m_detached_job_processes);

  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;

    poll_owned_processes(job.earlier_pipeline_processes);

    if (job.is_primary_process_active) {
      i32 status = 0;
      let const state = os::poll_process(job.pid, status);
      switch (state) {
      case os::process_state::Exited:
        job.is_primary_process_active = false;
        job.last_status = status;
        break;
      case os::process_state::Stopped:
        if (job.state != job::State::Stopped)
          job.has_unreported_state_change = true;
        job.state = job::State::Stopped;
        break;
      case os::process_state::Running:
        if (job.state != job::State::Running)
          job.has_unreported_state_change = true;
        job.state = job::State::Running;
        break;
      case os::process_state::Unchanged: break;
      }
    }

    if (!job.is_primary_process_active &&
        job.earlier_pipeline_processes.is_empty())
    {
      LOG(Info, "job %d finished with status %d", job.id, job.last_status);
      job.state = job::State::Done;
      job.has_unreported_state_change = true;
    }
  }
}

fn EvalContext::wait_for_job_processes(job &job, bool *was_stopped) throws
    -> i32
{
  bool primary_was_stopped = false;
  if (job.is_primary_process_active) {
    job.last_status =
        os::wait_and_monitor_process(job.pid, &primary_was_stopped);
    if (!primary_was_stopped) job.is_primary_process_active = false;
  }

  bool earlier_process_was_stopped = false;
  for (usize process_position = job.earlier_pipeline_processes.count();
       process_position > 0; process_position--)
  {
    bool process_was_stopped = false;
    unused(os::wait_and_monitor_process(
        job.earlier_pipeline_processes[process_position - 1],
        &process_was_stopped));
    if (process_was_stopped) {
      earlier_process_was_stopped = true;
    } else {
      job.earlier_pipeline_processes.remove(process_position - 1);
    }
  }

  let const any_process_was_stopped =
      primary_was_stopped || earlier_process_was_stopped;
  if (was_stopped != nullptr) *was_stopped = any_process_was_stopped;
  job.state = any_process_was_stopped ? job::State::Stopped : job::State::Done;
  return job.last_status;
}

fn EvalContext::jobs() wontthrow -> ArrayList<job> & { return m_jobs; }

fn EvalContext::find_job(i32 id) wontthrow -> job *
{
  for (job &job : m_jobs)
    if (job.id == id) return &job;
  return nullptr;
}

fn EvalContext::find_job_index_by_spec(StringView spec) throws -> Maybe<usize>
{
  if (m_jobs.is_empty()) return shit::None;

  StringView body = spec;
  if (!body.is_empty() && body[0] == '%') body = body.substring(1);

  if (body.is_empty() || body == "+" || body == "%") {
    return m_jobs.count() - 1;
  }
  if (body == "-")
    return m_jobs.count() >= 2 ? m_jobs.count() - 2 : m_jobs.count() - 1;

  if (let const parsed_value = body.to<i64>(); !parsed_value.is_error()) {
    for (usize i = 0; i < m_jobs.count(); i++)
      if (static_cast<i64>(m_jobs[i].id) == parsed_value.value()) return i;

    return shit::None;
  }

  let const wants_substring_match = body[0] == '?';
  if (wants_substring_match) body = body.substring(1);

  if (body.is_empty()) return shit::None;

  for (usize i = 0; i < m_jobs.count(); i++) {
    if (wants_substring_match) {
      if (m_jobs[i].command.find_substring(body).has_value()) return i;
    } else if (m_jobs[i].command.starts_with(body)) {
      return i;
    }
  }

  return shit::None;
}

fn EvalContext::find_job_by_spec(StringView spec) throws -> job *
{
  if (let const index = find_job_index_by_spec(spec); index.has_value())
    return &m_jobs[*index];
  return nullptr;
}

fn EvalContext::most_recent_job() wontthrow -> job *
{
  /* Skip a finished job, so a bare fg or bg acts on a running or stopped job
     rather than a dead pid. */
  for (usize i = m_jobs.count(); i > 0; i--) {
    ASSERT(i - 1 < m_jobs.count());
    if (m_jobs[i - 1].state != job::State::Done) return &m_jobs[i - 1];
  }
  return nullptr;
}

fn EvalContext::forget_done_jobs() throws -> void
{
  let kept = ArrayList<job>{heap_allocator()};
  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;
    kept.push(steal(job));
  }
  LOG(Debug, "dropping finished jobs, keeping %zu of %zu", kept.count(),
      m_jobs.count());
  m_jobs = steal(kept);
}

fn EvalContext::remove_job(i32 id) throws -> bool
{
  let kept = ArrayList<job>{heap_allocator()};
  let did_remove = false;
  for (job &job : m_jobs) {
    if (job.id == id) {
      if (job.is_primary_process_active) m_detached_job_processes.push(job.pid);
      for (let const process : job.earlier_pipeline_processes)
        m_detached_job_processes.push(process);
      did_remove = true;
      continue;
    }
    kept.push(steal(job));
  }
  m_jobs = steal(kept);
  return did_remove;
}

fn EvalContext::format_done_job_notifications(StringView line_ending) throws
    -> String
{
  update_jobs();

  let out = String{heap_allocator()};
  for (usize i = 0; i < m_jobs.count(); i++) {
    let const &job = m_jobs[i];
    if (job.state != job::State::Done) continue;

    char marker = ' ';
    if (i == m_jobs.count() - 1) {
      marker = '+';
    } else if (i == m_jobs.count() - 2) {
      marker = '-';
    }

    out += "[" + String::from(job.id, heap_allocator()) + "]";
    out.push(marker);
    out += " Done  ";
    out += job.command.c_str();
    out += line_ending;
  }

  forget_done_jobs();
  return out;
}

fn EvalContext::notify_done_jobs() throws -> void
{
  let const lines = format_done_job_notifications("\n");
  if (!lines.is_empty()) print_error(lines);
}

fn EvalContext::set_monitor(bool enabled) wontthrow -> void
{
  LOG(Info, "the monitor option flips to %s", enabled ? "on" : "off");
  m_runtime.set_option(shell_option_id::Monitor, enabled);
}

pure fn EvalContext::monitor() const wontthrow -> bool
{
  return m_runtime.option_is_enabled(shell_option_id::Monitor);
}

fn EvalContext::set_notify(bool enabled) wontthrow -> void
{
  LOG(Info, "the notify option flips to %s", enabled ? "on" : "off");
  m_runtime.set_option(shell_option_id::Notify, enabled);
}

pure fn EvalContext::notify() const wontthrow -> bool
{
  return m_runtime.option_is_enabled(shell_option_id::Notify);
}

} // namespace shit
