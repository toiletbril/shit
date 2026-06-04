#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

/* Removes shell variables, or with -f shell functions. The flag parser rejects
   an unknown option. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("unset [-f] [-v] name ...");

FLAG(UNSET_FUNCTION, Bool, 'f', "", "Remove functions instead of variables.");
FLAG(UNSET_VARIABLE, Bool, 'v', "", "Remove variables, which is the default.");

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
  std::vector<std::string> names =
      parse_flags_vec(FLAG_LIST, {ec.args().begin() + 1, ec.args().end()});
  SHIT_DEFER { reset_flags(FLAG_LIST); };

  bool unset_function = FLAG_UNSET_FUNCTION.is_enabled();
  for (const std::string &name : names) {
    if (unset_function)
      cxt.unset_function(name);
    else
      cxt.unset_shell_variable(name);
  }

  return 0;
}

} /* namespace shit */
