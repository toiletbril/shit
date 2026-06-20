#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-l] [-signal] %job|pid [...]");
HELP_DESCRIPTION_DECL(
    "The kill builtin sends a signal to each named job or process. The signal "
    "defaults to TERM and is named with a leading minus, such as -KILL, -9, or "
    "-SIGKILL. A target with a leading percent names a job, otherwise it is a "
    "process id. With -l it lists the signal names and exits.");

FLAG(KILL_LIST, Bool, 'l', "list", "List the signal names and exit.");
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

  /* A leading -l or --list prints the signal names and exits, checked before
     the signal parsing below so the -l is not read as a signal named l. */
  if (args.count() > 1 && (args[1] == "-l" || args[1] == "--list")) {
    ec.print_to_stdout(shitbox::format_signal_list());
    return 0;
  }

  usize first_target = 1;
  let signal_number = os::signal_number_from_name("TERM").value_or(15);

  /* A leading -name or -number names the signal to send. A numeric form such as
     -9 names the number directly, while a name such as -KILL or -SIGTERM
     resolves through the platform table. */
  if (args.count() > 1 && args[1].length() > 1 && args[1][0] == '-') {
    let const name = String{args[1].substring(1)};
    if (let const parsed_signal = utils::parse_decimal_integer(name.view());
        !parsed_signal.is_error() && !name.is_empty() && name[0] >= '0' &&
        name[0] <= '9')
    {
      /* The all-digits guard rejects a doubled minus such as --9, which would
         otherwise parse as the negative signal -9 and reach kill with an
         invalid number. */
      signal_number = static_cast<i32>(parsed_signal.value());
    } else if (let const resolved = os::signal_number_from_name(name);
               resolved.has_value())
    {
      LOG(Debug, "kill resolved signal '%s' to number %d", name.c_str(),
          *resolved);
      signal_number = *resolved;
    } else {
      throw Error{"'" + name + "' is not a valid signal"};
    }
    first_target = 2;
  }

  if (first_target >= args.count())
    return report_usage_error(ec, cxt, ec.program());

  i32 status = 0;
  for (usize i = first_target; i < args.count(); i++) {
    const String &target = args[i];
    const String target_text = target.clone();

    let pid = os::process{};
    if (!target.is_empty() && target[0] == '%') {
      const ErrorOr<i64> parsed_value =
          utils::parse_decimal_integer(StringView{target}.substring(1));
      if (parsed_value.is_error()) {
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + target_text +
                                      "' is not a valid job or process id");
        status = 1;
        continue;
      }
      job *const job = cxt.find_job(static_cast<int>(parsed_value.value()));
      if (job == nullptr) {
        report_soft_builtin_error(
            ec, cxt, StringView{"'"} + target_text + "' is not a known job");
        status = 1;
        continue;
      }
      ASSERT(job != nullptr);
      pid = job->pid;
    } else {
      const ErrorOr<i64> parsed_value = utils::parse_decimal_integer(target);
      if (parsed_value.is_error()) {
        /* A non-numeric target must not fall through to kill(0), which would
           signal the whole process group including this shell. */
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + target_text +
                                      "' is not a valid job or process id");
        status = 1;
        continue;
      }
      pid = os::process_from_pid(parsed_value.value());
    }

    LOG(Debug, "kill sending signal %d to target '%s'", signal_number,
        target_text.c_str());
    if (!os::signal_process(pid, signal_number)) {
      report_soft_builtin_error(ec, cxt,
                                StringView{"Cannot signal '"} + target_text +
                                    "' because " +
                                    os::last_system_error_message());
      status = 1;
    }
  }

  return status;
}

} // namespace shit
