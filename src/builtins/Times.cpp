#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

#if SHIT_PLATFORM_IS POSIX
#include <sys/times.h>
#include <unistd.h>
#endif

/* times prints the user and system time the shell and its children have used,
   two lines of two values each. The values come from the operating system, so
   a platform without process accounting prints zeros. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("times");

namespace shit {

namespace {

/* Format a count of seconds as the minutes and seconds form times prints. */
String format_time(double seconds)
{
  long minutes = static_cast<long>(seconds) / 60;
  double remainder = seconds - static_cast<double>(minutes * 60);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%ldm%.3fs", minutes, remainder);
  return String{buffer};
}

} /* namespace */

Times::Times() = default;

Builtin::Kind Times::kind() const { return Kind::Times; }

i32 Times::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  double self_user = 0, self_system = 0, child_user = 0, child_system = 0;

#if SHIT_PLATFORM_IS POSIX
  struct tms accounting{};
  if (times(&accounting) != static_cast<clock_t>(-1)) {
    double ticks = static_cast<double>(sysconf(_SC_CLK_TCK));
    if (ticks > 0) {
      self_user = static_cast<double>(accounting.tms_utime) / ticks;
      self_system = static_cast<double>(accounting.tms_stime) / ticks;
      child_user = static_cast<double>(accounting.tms_cutime) / ticks;
      child_system = static_cast<double>(accounting.tms_cstime) / ticks;
    }
  }
#endif

  String out{};
  out += format_time(self_user) + " " + format_time(self_system) + "\n";
  out += format_time(child_user) + " " + format_time(child_system) + "\n";
  ec.print_to_stdout(out);

  return 0;
}

} /* namespace shit */
