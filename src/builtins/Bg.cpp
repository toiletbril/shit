#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* bg resumes a stopped job in the background. With no operand it acts on the
   most recent job. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("bg [%job]");

namespace shit {

Bg::Bg() = default;

pure fn Bg::kind() const wontthrow -> Builtin::Kind { return Kind::Bg; }

fn Bg::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  job *job = nullptr;
  if (args.count() > 1 && !args[1].is_empty() && args[1][0] == '%') {
    let const parsed =
        utils::parse_decimal_integer(StringView{args[1]}.substring(1));
    if (parsed.is_error())
      throw Error{"bg: '" + args[1] + "' is not a valid job"};
    job = cxt.find_job(static_cast<int>(parsed.value()));
  } else
    job = cxt.most_recent_job();

  if (job == nullptr) throw Error{"bg: there is no such job"};
  ASSERT(job != nullptr);

  if (const Maybe<i32> cont = os::signal_number_from_name("CONT"))
    os::signal_process(job->pid, *cont);
  job->state = job::State::Running;

  ec.print_to_stdout("[" + std::to_string(job->id) + "] " + job->command +
                     " &\n");

  return 0;
}

} /* namespace shit */
