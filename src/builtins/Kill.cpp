#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* kill sends a signal to a job or a process. The signal defaults to TERM and is
   named with a leading minus, such as -KILL, -9, or -SIGKILL. A target with a
   leading percent names a job, otherwise it is a process id. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-signal] %job|pid [...]");
HELP_DESCRIPTION_DECL(
    "The kill builtin sends a signal to each named job or process. The signal "
    "defaults to TERM and is named with a leading minus, such as -KILL, -9, or "
    "-SIGKILL. A target with a leading percent names a job, otherwise it is a "
    "process id.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Kill);

namespace shit {

Kill::Kill() = default;

pure fn Kill::kind() const wontthrow -> Builtin::Kind { return Kind::Kill; }

fn Kill::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  usize first_target = 1;
  let signal_number = os::signal_number_from_name("TERM").value_or(15);

  /* A leading -name or -number names the signal to send. */
  if (args.count() > 1 && args[1].length() > 1 && args[1][0] == '-') {
    let const name = String{args[1].substring(1)};
    let const resolved = os::signal_number_from_name(name);
    if (!resolved) throw Error{"'" + name + "' is not a valid signal"};
    LOG(verbosity::Debug, "kill resolved signal '%s' to number %d",
        name.c_str(), *resolved);
    signal_number = *resolved;
    first_target = 2;
  }

  if (first_target >= args.count())
    throw Error{
        "Unable to send the signal because a job or a process id is required"};

  i32 status = 0;
  for (usize i = first_target; i < args.count(); i++) {
    const String &target = args[i];
    const String target_text = target.clone();

    let pid = os::process{};
    if (!target.is_empty() && target[0] == '%') {
      const ErrorOr<i64> parsed =
          utils::parse_decimal_integer(StringView{target}.substring(1));
      if (parsed.is_error()) {
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + target_text +
                                      "' is not a valid job or process id");
        status = 1;
        continue;
      }
      job *const job = cxt.find_job(static_cast<int>(parsed.value()));
      if (job == nullptr) {
        report_soft_builtin_error(
            ec, cxt, StringView{"'"} + target_text + "' is not a known job");
        status = 1;
        continue;
      }
      ASSERT(job != nullptr);
      pid = job->pid;
    } else {
      const ErrorOr<i64> parsed = utils::parse_decimal_integer(target);
      if (parsed.is_error()) {
        /* A non-numeric target must not fall through to kill(0), which would
           signal the whole process group including this shell. */
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + target_text +
                                      "' is not a valid job or process id");
        status = 1;
        continue;
      }
      pid = os::process_from_pid(parsed.value());
    }

    LOG(verbosity::Debug, "kill sending signal %d to target '%s'",
        signal_number, target_text.c_str());
    if (!os::signal_process(pid, signal_number)) {
      report_soft_builtin_error(ec, cxt,
                                StringView{"cannot signal '"} + target_text +
                                    "' because " +
                                    os::last_system_error_message());
      status = 1;
    }
  }

  return status;
}

} /* namespace shit */
