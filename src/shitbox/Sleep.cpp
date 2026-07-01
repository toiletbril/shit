#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

#include <cmath>
#include <cstdlib>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("duration ...");

HELP_DESCRIPTION_DECL(
    "The sleep utility pauses for the sum of the given durations.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Sleep);

namespace shit {

namespace shitbox {

Sleep::Sleep() = default;

pure fn Sleep::kind() const wontthrow -> Utility::Kind { return Kind::Sleep; }

fn Sleep::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  double total_seconds = 0.0;
  bool should_sleep_forever = false;
  for (const String &operand : operands) {
    let const duration = operand.view();
    if (duration == "inf" || duration == "infinity") {
      should_sleep_forever = true;
      continue;
    }

    let const number = String{cxt.scratch_allocator(), duration};
    let const start = number.c_str();
    char *end = nullptr;
    let const seconds_value = std::strtod(start, &end);

    /* A nan compares false against zero, so the explicit finite check is
       needed, not the sign test alone. */
    let digits = start;
    if (*digits == '+' || *digits == '-') digits++;
    const bool is_hex_prefix =
        digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
    if (end == start || is_hex_prefix || !std::isfinite(seconds_value) ||
        seconds_value < 0.0)
      throw Error{"sleep: invalid duration '" + number + "'"};

    double unit_multiplier = 1.0;
    if (*end != '\0' && *(end + 1) == '\0') {
      switch (*end) {
      case 's': unit_multiplier = 1.0; break;
      case 'm': unit_multiplier = 60.0; break;
      case 'h': unit_multiplier = 60.0 * 60.0; break;
      case 'd': unit_multiplier = 60.0 * 60.0 * 24.0; break;
      default: throw Error{"sleep: invalid duration '" + number + "'"};
      }
    } else if (*end != '\0') {
      throw Error{"sleep: invalid duration '" + number + "'"};
    }

    total_seconds += seconds_value * unit_multiplier;
  }

  if (should_sleep_forever) {
    while (!os::INTERRUPT_REQUESTED)
      os::sleep_for_seconds(60.0 * 60.0 * 24.0);
    return 130;
  }

  os::sleep_for_seconds(total_seconds);

  return os::INTERRUPT_REQUESTED ? 130 : 0;
}

} // namespace shitbox

} // namespace shit
