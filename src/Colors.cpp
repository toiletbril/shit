#include "Colors.hpp"

#include "Platform.hpp"

namespace shit {

namespace colors {

/* NO_COLOR set and non-empty, or TERM equal to dumb, turns color off for every
   stream regardless of the terminal. */
static fn color_is_suppressed_by_environment() throws -> bool
{
  if (let const no_color = os::get_environment_variable("NO_COLOR");
      no_color.has_value() && !no_color->is_empty())
    return true;

  if (let const term = os::get_environment_variable("TERM");
      term.has_value() && term->view() == StringView{"dumb"})
    return true;

  return false;
}

fn stdout_wants_color() throws -> bool
{
  return os::is_stdout_a_tty() && !color_is_suppressed_by_environment();
}

fn stderr_wants_color() throws -> bool
{
  return os::is_stderr_a_tty() && !color_is_suppressed_by_environment();
}

} /* namespace colors */

} /* namespace shit */
