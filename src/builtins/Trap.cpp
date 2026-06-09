#include "../Builtin.hpp"
#include "../Eval.hpp"

#include <cctype>

/* trap lists the set traps with no argument, sets an action for one or more
   conditions, and removes them with a leading dash. The EXIT action runs when
   the shell ends. Other conditions are stored but not yet delivered
   asynchronously. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[action condition ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Trap::Trap() = default;

pure Builtin::Kind Trap::kind() const wontthrow { return Kind::Trap; }

namespace {

/* Normalize a condition name to its bare upper-case form, so SIGINT, sigint,
   int, and the number 2 all name the same condition, and 0 names EXIT. */
String normalize_condition(StringView raw) throws
{
  String name{};
  for (usize i = 0; i < raw.count(); i++)
    name.push(static_cast<char>(toupper(static_cast<unsigned char>(raw[i]))));
  if (name.starts_with("SIG") && name.count() > 3)
    name = String{name.substring(3)};
  if (name == "0") name = "EXIT";
  return name;
}

} /* namespace */

i32 Trap::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() == 1) {
    String out{};
    cxt.traps().for_each([&](StringView condition, const String &action) {
      out += "trap -- '";
      out += action;
      out += "' ";
      out += condition;
      out += '\n';
    });
    ec.print_to_stdout(out);
    return 0;
  }

  ASSERT(args.count() > 1);

  /* With a single operand there is no action, so the operand names a condition
     to reset to its default, as POSIX specifies for 'trap SIG' and 'trap NUM'.
     With two or more operands the first is the action and the rest are the
     conditions it applies to. */
  if (args.count() == 2) {
    let const condition = normalize_condition(args[1]);
    cxt.remove_trap(condition);
    return 0;
  }

  let const &action = args[1];
  /* A lone dash as the action resets the named conditions to their defaults. */
  let const is_reset = action == "-";

  for (usize i = 2; i < args.count(); i++) {
    let const condition = normalize_condition(args[i]);
    if (is_reset)
      cxt.remove_trap(condition);
    else
      cxt.set_trap(condition, action);
  }

  return 0;
}

} /* namespace shit */
