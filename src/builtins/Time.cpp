#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("command [argument ...]");

HELP_DESCRIPTION_DECL(
    "The time builtin runs the given command once and prints to standard error "
    "how long it took, the real elapsed time plus the user and system time its "
    "children consumed. It returns the status of the timed command.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Time);

namespace shit {

Time::Time() = default;

pure fn Time::kind() const wontthrow -> Builtin::Kind { return Kind::Time; }

cold fn Time::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* time with no command prints nothing and succeeds, the way an empty timed
     command would. */
  if (ec.args().count() < 2) return 0;

  /* The arguments past the builtin name are joined and run through the shell,
     so the timed command resolves the same way an eval body does. The arguments
     arrive already expanded and split, so a word that carried embedded spaces
     through a quote is re-split here, the same caveat eval carries. */
  let command = String{};
  for (usize i = 1; i < ec.args().count(); i++) {
    if (i > 1) command.push(' ');
    command.append(ec.args()[i].view());
  }

  LOG(Debug, "time running command '%s' under the clock", command.c_str());

  double user_before = 0, system_before = 0;
  os::children_cpu_seconds(user_before, system_before);

  /* The timed command must run to completion and return here so the report can
     print. When time is the shell's final command the tail-exec optimization
     would replace the shell process with the command and the report would never
     run, so the flag is cleared around the run and restored after. */
  let const saved_terminal_exec = cxt.terminal_exec_allowed();
  cxt.set_terminal_exec_allowed(false);
  defer { cxt.set_terminal_exec_allowed(saved_terminal_exec); };

  let const start_nanos = os::monotonic_nanos();

  let const status = cxt.run_source(command, "time", false,
                                    ec.source_location(), StringView{"time"});

  let const elapsed_nanos = os::monotonic_nanos() - start_nanos;

  double user_after = 0, system_after = 0;
  os::children_cpu_seconds(user_after, system_after);

  let const real_seconds = static_cast<double>(elapsed_nanos) / 1000000000.0;
  const double user_cpu = user_after - user_before;
  const double system_cpu = system_after - system_before;

  /* A set TIMEFORMAT drives the format, an empty value prints nothing, and an
     unset value keeps the pretty default, matching the time keyword. */
  String report;
  if (let const time_format = cxt.get_variable_value("TIMEFORMAT");
      time_format.has_value())
  {
    if (!time_format->is_empty())
      report = utils::format_time_report_custom(
          time_format->view(), real_seconds, user_cpu, system_cpu);
  } else {
    report =
        utils::format_time_report_pretty(real_seconds, user_cpu, system_cpu);
  }

  if (!report.is_empty()) {
    shit::print_error(report);
    shit::flush();
  }

  return status;
}

} // namespace shit
