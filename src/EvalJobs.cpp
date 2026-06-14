#include "Eval.hpp"

#include "Common.hpp"
#include "Debug.hpp"
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
  LOG(verbosity::Info, "registered job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn EvalContext::update_jobs() throws -> void
{
  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;

    i32 status = 0;
    let const state = os::poll_process(job.pid, status);
    switch (state) {
    case os::process_state::Exited:
      LOG(verbosity::Info, "job %d finished with status %d", job.id, status);
      job.state = job::State::Done;
      job.last_status = status;
      break;
    case os::process_state::Stopped: job.state = job::State::Stopped; break;
    default: job.state = job::State::Running; break;
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
  /* Skip a finished job, so a bare fg or bg acts on a job that is still
     running or stopped rather than a dead pid. */
  for (usize i = m_jobs.count(); i > 0; i--) {
    ASSERT(i - 1 < m_jobs.count());
    if (m_jobs[i - 1].state != job::State::Done) return &m_jobs[i - 1];
  }
  return nullptr;
}

fn EvalContext::forget_done_jobs() throws -> void
{
  let kept = ArrayList<job>{};
  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;
    kept.push(steal(job));
  }
  LOG(verbosity::Debug, "dropping finished jobs, keeping %zu of %zu",
      kept.count(), m_jobs.count());
  m_jobs = steal(kept);
}

fn EvalContext::format_done_job_notifications(StringView line_ending) throws
    -> String
{
  update_jobs();

  let out = String{};
  for (usize i = 0; i < m_jobs.count(); i++) {
    const job &job = m_jobs[i];
    if (job.state != job::State::Done) continue;

    /* The bash current-job marker, '+' for the last entry and '-' for the one
       before it, otherwise a space. */
    char marker = ' ';
    if (i == m_jobs.count() - 1)
      marker = '+';
    else if (m_jobs.count() >= 2 && i == m_jobs.count() - 2)
      marker = '-';

    out += "[" + utils::int_to_text(job.id) + "]";
    out.push(marker);
    out += " Done  ";
    out += job.command.c_str();
    /* The caller picks the ending, \n at the prompt boundary and \r\n when
       the terminal sits in raw mode under the editor. */
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
  LOG(verbosity::Info, "the monitor option flips to %s",
      enabled ? "on" : "off");
  m_monitor = enabled;
}

pure fn EvalContext::monitor() const wontthrow -> bool { return m_monitor; }

fn EvalContext::set_notify(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the notify option flips to %s", enabled ? "on" : "off");
  m_notify = enabled;
}

pure fn EvalContext::notify() const wontthrow -> bool { return m_notify; }

} /* namespace shit */
