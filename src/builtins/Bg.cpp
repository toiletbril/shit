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

Builtin::Kind
Bg::kind() const
{
  return Kind::Bg;
}

i32
Bg::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> &args = ec.args();

  Job *job = nullptr;
  if (args.size() > 1 && !args[1].empty() && args[1][0] == '%') {
    ErrorOr<i64> parsed =
        utils::parse_decimal_integer(StringView{args[1]}.substring(1));
    if (parsed.is_error())
      throw Error{"bg: '" + args[1] + "' is not a valid job"};
    job = cxt.find_job(static_cast<int>(parsed.value()));
  } else
    job = cxt.most_recent_job();

  if (job == nullptr)
    throw Error{"bg: there is no such job"};

  if (Maybe<i32> cont = os::signal_number_from_name("CONT"))
    os::signal_process(job->pid, *cont);
  job->state = Job::State::Running;

  ec.print_to_stdout("[" + std::to_string(job->id) + "] " + job->command +
                     " &\n");

  return 0;
}

} /* namespace shit */
