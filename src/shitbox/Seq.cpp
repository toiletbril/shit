#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[first [increment]] last");

HELP_DESCRIPTION_DECL(
    "The seq utility prints a sequence of integers from first to last, one per "
    "line. first defaults to one and increment to one.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

static fn parse_integer(StringView text) throws -> i64
{
  let const parsed = utils::parse_decimal_integer(text);
  if (parsed.is_error())
    throw Error{"seq: invalid integer argument '" + String{text} + "'"};
  return parsed.value();
}

fn util_seq(const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  i64 first = 1;
  i64 increment = 1;
  i64 last = 0;
  if (operands.count() == 1) {
    last = parse_integer(operands[0].view());
  } else if (operands.count() == 2) {
    first = parse_integer(operands[0].view());
    last = parse_integer(operands[1].view());
  } else if (operands.count() == 3) {
    first = parse_integer(operands[0].view());
    increment = parse_integer(operands[1].view());
    last = parse_integer(operands[2].view());
  } else {
    throw Error{"seq expects one to three integer operands"};
  }

  if (increment == 0) throw Error{"seq: the increment must not be zero"};

  let output = String{};
  if (increment > 0)
    for (i64 value = first; value <= last; value += increment) {
      output += utils::int_to_text(value, heap_allocator()).view();
      output += '\n';
    }
  else
    for (i64 value = first; value >= last; value += increment) {
      output += utils::int_to_text(value, heap_allocator()).view();
      output += '\n';
    }

  ec.print_to_stdout(output);
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
