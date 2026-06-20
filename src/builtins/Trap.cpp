#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#include <cctype>

/* trap lists the set traps with no argument, sets an action for one or more
   conditions, and removes them with a leading dash. The EXIT action runs when
   the shell ends. Other conditions are stored but not yet delivered
   asynchronously. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[action condition ...]");

HELP_DESCRIPTION_DECL(
    "The trap builtin sets the action to run for each named condition, resets "
    "those conditions to their defaults when the action is a lone dash, and "
    "resets a single named condition given with no action. With no argument it "
    "lists the set traps, and the EXIT action runs when the shell ends.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Trap);

namespace shit {

Trap::Trap() = default;

pure Builtin::Kind Trap::kind() const wontthrow { return Kind::Trap; }

namespace {

/* Normalize a condition name to its bare upper-case form, so SIGINT, sigint,
   int, and the number 2 all name the same condition, and 0 names EXIT. A trap
   set by name and cleared or listed by number must resolve to one key, so a
   bare number maps through the os signal name table the way dash lists it. */
String normalize_condition(StringView raw) throws
{
  let name = String{};
  for (usize i = 0; i < raw.count(); i++)
    name.push(static_cast<char>(toupper(static_cast<unsigned char>(raw[i]))));
  if (name.starts_with("SIG") && name.count() > 3) {
    name = String{name.substring(3)};
  }
  if (name == "0") return String{"EXIT"};

  /* A condition written as a bare number names a signal, so it folds to the
     same name the name form yields. The number 0 already became EXIT above. */
  if (name.view().is_all_decimal_digits()) {
    let const parsed = utils::parse_decimal_integer(name.view());
    if (!parsed.is_error()) {
      if (let const signal_name =
              os::signal_name_from_number(static_cast<i32>(parsed.value())))
        return *signal_name;
    }
  }
  return name;
}

} /* namespace */

i32 Trap::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() == 1) {
    let out = String{};
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
    LOG(Info, "trap resetting condition '%s' to its default",
        condition.c_str());
    cxt.remove_trap(condition);
    return 0;
  }

  let const &action = args[1];
  /* A lone dash as the action resets the named conditions to their defaults. */
  let const is_reset = action == "-";

  for (usize i = 2; i < args.count(); i++) {
    let const condition = normalize_condition(args[i]);
    LOG(Info, "trap %s action for signal '%s'",
        is_reset ? "resetting the" : "setting", condition.c_str());
    if (is_reset)
      cxt.remove_trap(condition);
    else
      cxt.set_trap(condition, action);
  }

  return 0;
}

} /* namespace shit */
