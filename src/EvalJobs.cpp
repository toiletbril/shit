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
  new_job.command = command;
  new_job.state = job::State::Running;
  m_jobs.push(steal(new_job));
  ASSERT(!m_jobs.is_empty());
  LOG(Info, "registered job %d", m_jobs.back().id);
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

fn EvalContext::update_jobs() throws -> void
{
  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;

    i32 status = 0;
    let const state = os::poll_process(job.pid, status);
    switch (state) {
    case os::process_state::Exited:
      LOG(Info, "job %d finished with status %d", job.id, status);
      job.state = job::State::Done;
      job.last_status = status;
      break;
    case os::process_state::Stopped: job.state = job::State::Stopped; break;
    case os::process_state::Running: job.state = job::State::Running; break;
    case os::process_state::Unchanged: break;
    }
  }
}

fn EvalContext::jobs() wontthrow -> ArrayList<job> & { return m_jobs; }

fn EvalContext::find_job(i32 id) wontthrow -> job *
{
  for (job &job : m_jobs)
    if (job.id == id) return &job;
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
    } else if (m_jobs.count() >= 2 && i == m_jobs.count() - 2) {
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
  m_monitor = enabled;
}

pure fn EvalContext::monitor() const wontthrow -> bool { return m_monitor; }

fn EvalContext::set_notify(bool enabled) wontthrow -> void
{
  LOG(Info, "the notify option flips to %s", enabled ? "on" : "off");
  m_notify = enabled;
}

pure fn EvalContext::notify() const wontthrow -> bool { return m_notify; }

} // namespace shit
