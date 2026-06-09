#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

/* Removes shell variables, or with -f shell functions. The flag parser rejects
   an unknown option. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-f] [-v] name ...");

FLAG(UNSET_FUNCTION, Bool, 'f', "", "Remove functions instead of variables.");
FLAG(UNSET_VARIABLE, Bool, 'v', "", "Remove variables, which is the default.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Unset::Unset() = default;

pure Builtin::Kind Unset::kind() const wontthrow { return Kind::Unset; }

i32 Unset::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const names = parse_flags_vec(FLAG_LIST, ec.args());
  defer { reset_flags(FLAG_LIST); };

  ASSERT(!names.is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const unset_function = FLAG_UNSET_FUNCTION.is_enabled();
  let had_error = false;
  for (usize i = 1; i < names.count(); i++) {
    let const &name = names[i];
    if (unset_function) {
      cxt.unset_function(name);
    } else {
      /* A read-only variable makes unset_shell_variable throw. The remaining
         names are still unset and the builtin reports a non-zero status, the
         way dash continues past a read-only name. */
      try {
        cxt.unset_shell_variable(name);
      } catch (const Error &e) {
        shit::print_error(StringView{"unset: "} + name + ": is read only\n");
        had_error = true;
      }
    }
  }

  return had_error ? 2 : 0;
}

} /* namespace shit */
