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
  ArrayList<String> names = parse_flags_vec(FLAG_LIST, ec.args());
  SHIT_DEFER { reset_flags(FLAG_LIST); };

  bool unset_function = FLAG_UNSET_FUNCTION.is_enabled();
  for (usize i = 1; i < names.size(); i++) {
    const String &name = names[i];
    std::string name_string{name.c_str(), name.size()};
    if (unset_function)
      cxt.unset_function(name_string);
    else
      cxt.unset_shell_variable(name_string);
  }

  return 0;
}

} /* namespace shit */
