#include "../Builtin.hpp"
#include "../Eval.hpp"

/* No flag parsing through the generic machinery, since set treats -- and the
   operands specially. The option letters are accepted but not yet enforced. */

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

  /* set with no arguments lists the shell variables. That listing is not
     implemented yet, so it is a no-op rather than an error. */
  if (args.size() == 1) return 0;

  usize i = 1;
  bool saw_end_of_options = false;
  while (i < args.size()) {
    const std::string &arg = args[i];
    if (arg == "--") {
      saw_end_of_options = true;
      i++;
      break;
    }
    /* An option group like -e or +x is accepted and skipped. A lone - or a
       plain word begins the operands. */
    if (arg.length() > 1 && (arg[0] == '-' || arg[0] == '+')) {
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
