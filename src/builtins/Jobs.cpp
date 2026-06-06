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

} /* namespace */

Jobs::Jobs() = default;

pure fn Jobs::kind() const wontthrow -> Builtin::Kind { return Kind::Jobs; }

fn Jobs::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(ec);

  cxt.update_jobs();

  let out = String{};
  for (let const &job : cxt.jobs()) {
    out += "[" + utils::int_to_text(job.id) + "] " + state_word(job.state) +
           "\t" + job.command.c_str() + "\n";
  }
  ec.print_to_stdout(out);

  cxt.forget_done_jobs();
  return 0;
}

} /* namespace shit */
