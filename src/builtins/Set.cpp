#include "../Builtin.hpp"
#include "../Eval.hpp"

/* No flag parsing through the generic machinery, since set treats -- and the
   operands specially. The option letters -e and -x take effect, the rest are
   accepted but not yet enforced. */

namespace shit {

Set::Set() = default;

Builtin::Kind
Set::kind() const
{
  return Kind::Set;
}

i32
Set::execute(ExecContext &ec, EvalContext &cxt) const
{
  const std::vector<std::string> &args = ec.args();

  /* set with no arguments lists the shell variables. */
  if (args.size() == 1) {
    std::string out{};
    for (const std::string &assignment : cxt.sorted_variable_assignments())
      out += assignment + "\n";
    ec.print_to_stdout(out);
    return 0;
  }

  usize i = 1;
  bool saw_end_of_options = false;
  while (i < args.size()) {
    const std::string &arg = args[i];
    if (arg == "--") {
      saw_end_of_options = true;
      i++;
      break;
    }
    /* A -letters group turns options on, a +letters group turns them off. */
    if (arg.length() > 1 && (arg[0] == '-' || arg[0] == '+')) {
      bool enable = arg[0] == '-';
      for (usize c = 1; c < arg.length(); c++) {
        if (arg[c] == 'e')
          cxt.set_error_exit(enable);
        else if (arg[c] == 'x')
          cxt.set_echo_expanded(enable);
      }
      i++;
      continue;
    }
    break;
  }

  /* Only rebind the positional parameters when operands or -- were given, so a
     bare set -e leaves them alone. */
  if (saw_end_of_options || i < args.size()) {
    std::vector<std::string> params{
        args.begin() + static_cast<std::ptrdiff_t>(i), args.end()};
    cxt.set_positional_params(std::move(params));
  }

  return 0;
}

} /* namespace shit */
