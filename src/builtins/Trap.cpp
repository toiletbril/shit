#include "../Builtin.hpp"
#include "../Eval.hpp"

#include <cctype>

/* trap lists the set traps with no argument, sets an action for one or more
   conditions, and removes them with a leading dash. The EXIT action runs when
   the shell ends. Other conditions are stored but not yet delivered
   asynchronously. */

namespace shit {

Trap::Trap() = default;

pure Builtin::Kind Trap::kind() const wontthrow { return Kind::Trap; }

namespace {

/* Normalize a condition name to its bare upper-case form, so SIGINT, sigint,
   int, and the number 2 all name the same condition, and 0 names EXIT. */
String normalize_condition(StringView raw) throws
{
  String name{};
  for (usize i = 0; i < raw.size(); i++)
    name.push(static_cast<char>(toupper(static_cast<unsigned char>(raw[i]))));
  if (name.starts_with("SIG") && name.size() > 3)
    name = String{name.substring(3)};
  if (name == "0") name = "EXIT";
  return name;
}

} /* namespace */

i32 Trap::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.empty());

  if (args.size() == 1) {
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

  ASSERT(args.size() > 1);
  let const &action = args[1];
  /* A lone dash resets the named conditions to their defaults. */
  let const is_reset = action == "-";

  for (usize i = 2; i < args.size(); i++) {
    let const condition = normalize_condition(args[i]);
    if (is_reset)
      cxt.remove_trap(condition);
    else
      cxt.set_trap(condition, action);
  }

  return 0;
}

} /* namespace shit */
