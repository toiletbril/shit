#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

#include <cstdlib>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("seconds");

HELP_DESCRIPTION_DECL(
    "The sleep utility pauses for the given number of seconds, which may carry "
    "a fractional part.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_sleep(const ExecContext &ec, EvalContext &cxt,
              const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.count() != 1)
    throw Error{"sleep expects one duration in seconds"};

  const String number{operands[0].view()};
  char *end = nullptr;
  let const seconds = std::strtod(number.c_str(), &end);
  if (end == number.c_str() || *end != '\0' || seconds < 0.0)
    throw Error{"sleep: invalid duration '" + number + "'"};

  os::sleep_for_seconds(seconds);
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
