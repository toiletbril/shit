#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[%job]");

HELP_DESCRIPTION_DECL(
    "The bg builtin resumes a stopped job in the background.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Bg);

namespace shit {

Bg::Bg() = default;

pure fn Bg::kind() const wontthrow -> Builtin::Kind { return Kind::Bg; }

fn Bg::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  job *job = nullptr;
  if (args.count() > 1 && !args[1].is_empty() && args[1][0] == '%') {
    let const parsed_value = StringView{args[1]}.substring(1).to<i64>();
    if (parsed_value.is_error())
      throw ErrorWithDetails{"'" + args[1] + "' is not a valid job",
                             "Use a job spec like `%1` or `%+`"};
    job = cxt.find_job(static_cast<int>(parsed_value.value()));
  } else {
    job = cxt.most_recent_job();
  }

  if (job == nullptr)
    throw ErrorWithDetails{"There is no such job", "List jobs with `jobs`"};
  ASSERT(job != nullptr);

  LOG(Info, "bg resuming job %d in the background", job->id);

  if (const Maybe<i32> cont = os::signal_number_from_name("CONT"))
    os::signal_process(job->pid, *cont);
  job->state = job::State::Running;

  ec.print_to_stdout("[" + String::from(job->id, cxt.scratch_allocator()) +
                     "] " + job->command + " &\n");

  return 0;
}

} // namespace shit
