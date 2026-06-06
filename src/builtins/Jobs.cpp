#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

/* jobs lists the background jobs and the state each one is in, then forgets the
   ones that have finished, the same as a shell prompt would. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("jobs");

namespace shit {

namespace {

const char *
state_word(Job::State state)
{
  switch (state) {
  case Job::State::Running: return "Running";
  case Job::State::Stopped: return "Stopped";
  case Job::State::Done: return "Done";
  }
  return "Unknown";
}

} /* namespace */

Jobs::Jobs() = default;

Builtin::Kind
Jobs::kind() const
{
  return Kind::Jobs;
}

i32
Jobs::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(ec);

  cxt.update_jobs();

  std::string out{};
  for (const Job &job : cxt.jobs()) {
    out += "[" + std::to_string(job.id) + "] " + state_word(job.state) + "\t" +
           job.command.c_str() + "\n";
  }
  ec.print_to_stdout(out);

  cxt.forget_done_jobs();
  return 0;
}

} /* namespace shit */
