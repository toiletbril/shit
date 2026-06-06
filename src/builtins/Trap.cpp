#include "../Builtin.hpp"
#include "../Eval.hpp"

#include <algorithm>
#include <cctype>

/* trap lists the set traps with no argument, sets an action for one or more
   conditions, and removes them with a leading dash. The EXIT action runs when
   the shell ends. Other conditions are stored but not yet delivered
   asynchronously. */

namespace shit {

Trap::Trap() = default;

Builtin::Kind
Trap::kind() const
{
  return Kind::Trap;
}

namespace {

/* Normalize a condition name to its bare upper-case form, so SIGINT, sigint,
   int, and the number 2 all name the same condition, and 0 names EXIT. */
std::string
normalize_condition(const std::string &raw)
{
  std::string name = raw;
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(toupper(c)); });
  if (name.rfind("SIG", 0) == 0 && name.length() > 3) name = name.substr(3);
  if (name == "0") name = "EXIT";
  return name;
}

} /* namespace */

i32
Trap::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> &args = ec.args();

  if (args.size() == 1) {
    std::string out{};
    cxt.traps().for_each([&](StringView condition, const String &action) {
      out += "trap -- '";
      out.append(action.c_str(), action.size());
      out += "' ";
      out.append(condition.data, condition.length);
      out += '\n';
    });
    ec.print_to_stdout(out);
    return 0;
  }

  std::string action{args[1].c_str(), args[1].size()};
  /* A lone dash, or a first operand that is itself a condition, resets the
     conditions to their defaults. */
  bool is_reset = action == "-";

  for (usize i = 2; i < args.size(); i++) {
    std::string condition =
        normalize_condition(std::string{args[i].c_str(), args[i].size()});
    if (is_reset)
      cxt.remove_trap(condition);
    else
      cxt.set_trap(condition, action);
  }

  return 0;
}

} /* namespace shit */
