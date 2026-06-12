#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#if SHIT_PLATFORM_IS POSIX
#include <sys/times.h>
#include <unistd.h>
#endif

/* times prints the user and system time the shell and its children have used,
   two lines of two values each. The values come from the operating system, so
   a platform without process accounting prints zeros. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

HELP_DESCRIPTION_DECL(
    "The times builtin prints the user and system time the shell and its "
    "children have used, one line for the shell and one for its children. A "
    "platform without process accounting prints zeros.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Times);

namespace shit {

Times::Times() = default;

pure Builtin::Kind Times::kind() const wontthrow { return Kind::Times; }

cold i32 Times::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(verbosity::Debug,
      "times printing the shell and child process accounting");

  double self_user = 0, self_system = 0, child_user = 0, child_system = 0;

#if SHIT_PLATFORM_IS POSIX
  struct tms accounting{};
  if (times(&accounting) != static_cast<clock_t>(-1)) {
    let const ticks = static_cast<double>(sysconf(_SC_CLK_TCK));
    if (ticks > 0) {
      self_user = static_cast<double>(accounting.tms_utime) / ticks;
      self_system = static_cast<double>(accounting.tms_stime) / ticks;
      child_user = static_cast<double>(accounting.tms_cutime) / ticks;
      child_system = static_cast<double>(accounting.tms_cstime) / ticks;
    }
  }
#endif

  let out = String{};
  out += utils::format_minutes_seconds(self_user) + " " +
         utils::format_minutes_seconds(self_system) + "\n";
  out += utils::format_minutes_seconds(child_user) + " " +
         utils::format_minutes_seconds(child_system) + "\n";
  ec.print_to_stdout(out);

  return 0;
}

} /* namespace shit */
