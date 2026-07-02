#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[first [increment]] last");

HELP_DESCRIPTION_DECL(
    "The seq utility prints a sequence of integers from first to last.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Seq);

namespace shit {

namespace shitbox {

static fn parse_integer(StringView text, Allocator allocator) throws -> i64
{
  let const parsed = text.to<i64>();
  if (parsed.is_error())
    throw Error{
        "seq: invalid integer argument '" + String{allocator, text}
          + "'"
    };
  return parsed.value();
}

Seq::Seq() = default;

pure fn Seq::kind() const wontthrow -> Utility::Kind { return Kind::Seq; }

fn Seq::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  i64 first = 1;
  i64 increment = 1;
  i64 last = 0;
  let const allocator = cxt.scratch_allocator();
  if (operands.count() == 1) {
    last = parse_integer(operands[0].view(), allocator);
  } else if (operands.count() == 2) {
    first = parse_integer(operands[0].view(), allocator);
    last = parse_integer(operands[1].view(), allocator);
  } else if (operands.count() == 3) {
    first = parse_integer(operands[0].view(), allocator);
    increment = parse_integer(operands[1].view(), allocator);
    last = parse_integer(operands[2].view(), allocator);
  } else {
    throw ErrorWithDetails{
        "seq expects one to three integer operands",
        "Use `seq LAST`, `seq FIRST LAST`, or `seq FIRST STEP LAST`"};
  }

  if (increment == 0)
    throw ErrorWithDetails{"seq: the increment must not be zero",
                           "Give a non-zero step, e.g. `seq 1 2 10`"};

  let output = String{cxt.scratch_allocator()};
  /* The step is guarded against signed overflow before it is taken, so a range
     reaching the integer bounds ends rather than wrapping. */
  if (increment > 0)
    for (i64 value = first; value <= last; value += increment) {
      output += String::from(value, cxt.scratch_allocator()).view();
      output += '\n';
      if (value > INT64_MAX - increment) break;
    }
  else
    for (i64 value = first; value >= last; value += increment) {
      output += String::from(value, cxt.scratch_allocator()).view();
      output += '\n';
      if (value < INT64_MIN - increment) break;
    }

  ec.print_to_stdout(output);
  return 0;
}

} // namespace shitbox

} // namespace shit
