#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* time runs a command once and prints how long it took, the real elapsed time
   from a monotonic clock plus the user and system time the children consumed.
   The command runs through the shell so a builtin, a function, or an external
   program all work, and the report goes to standard error so a redirection on
   the command still steers the command's own output. The leading time keyword
   handles a command or a compound directly, so this builtin is the form reached
   through command time or builtin time. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("command [argument ...]");

HELP_DESCRIPTION_DECL(
    "The time builtin runs the given command once and prints to standard error "
    "how long it took, the real elapsed time plus the user and system time its "
    "children consumed. It returns the status of the timed command.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

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

  LOG(verbosity::Debug, "time running command '%s' under the clock",
      command.c_str());

  double user_before = 0, system_before = 0;
  os::children_cpu_seconds(user_before, system_before);

  /* The timed command must run to completion and return here so the report can
     print. When time is the shell's final command the tail-exec optimization
     would replace the shell process with the command and the report would never
     run, so the flag is cleared around the run and restored after. */
  const bool saved_terminal_exec = cxt.terminal_exec_allowed();
  cxt.set_terminal_exec_allowed(false);
  defer { cxt.set_terminal_exec_allowed(saved_terminal_exec); };

  const u64 start_nanos = os::monotonic_nanos();

  const i32 status = cxt.run_source(command, "time", false,
                                    ec.source_location(), StringView{"time"});

  const u64 elapsed_nanos = os::monotonic_nanos() - start_nanos;

  double user_after = 0, system_after = 0;
  os::children_cpu_seconds(user_after, system_after);

  const double real_seconds = static_cast<double>(elapsed_nanos) / 1000000000.0;
  shit::print_error(utils::format_time_report_pretty(
      real_seconds, user_after - user_before, system_after - system_before));
  shit::flush();

  return status;
}

} /* namespace shit */
