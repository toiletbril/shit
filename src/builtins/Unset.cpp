#include "../Builtin.hpp"
#include "../Eval.hpp"

/* Removes shell variables, or with -f shell functions. */

namespace shit {

Unset::Unset() = default;

Builtin::Kind
Unset::kind() const
{
  return Kind::Unset;
}

i32
Unset::execute(ExecContext &ec, EvalContext &cxt) const
{
  const std::vector<std::string> &args = ec.args();

  usize i = 1;
  bool unset_function = false;
  if (i < args.size() && (args[i] == "-f" || args[i] == "-v")) {
    unset_function = (args[i] == "-f");
    i++;
  }

  for (; i < args.size(); i++) {
    if (unset_function)
      cxt.unset_function(args[i]);
    else
      cxt.unset_shell_variable(args[i]);
  }

  return 0;
}

} /* namespace shit */
