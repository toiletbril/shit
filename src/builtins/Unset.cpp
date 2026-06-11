#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* Removes shell variables, or with -f shell functions. The flag parser rejects
   an unknown option. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-f] [-v] name ...");

HELP_DESCRIPTION_DECL(
    "The unset builtin removes each named shell variable, or with -f removes "
    "each named shell function. A read only variable is left in place and the "
    "builtin reports a non-zero status while still unsetting the remaining "
    "names.");

FLAG(UNSET_FUNCTION, Bool, 'f', "", "Remove functions instead of variables.");
FLAG(UNSET_VARIABLE, Bool, 'v', "", "Remove variables, which is the default.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Unset::Unset() = default;

pure Builtin::Kind Unset::kind() const wontthrow { return Kind::Unset; }

i32 Unset::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const names =
      parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position);
  defer { reset_flags(FLAG_LIST); };

  ASSERT(!names.is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const unset_function = FLAG_UNSET_FUNCTION.is_enabled();
  let had_error = false;
  for (usize i = 1; i < names.count(); i++) {
    let const &name = names[i];
    if (unset_function) {
      LOG(verbosity::Debug, "unset removing function '%s'", name.c_str());
      cxt.unset_function(name);
    } else if (let const bracket = name.view().find_character('[');
               bracket.has_value() && name.count() > 0 &&
               name.view()[name.count() - 1] == ']')
    {
      /* A name[subscript] operand removes one array element or key rather than
         the whole variable. */
      const StringView array_name =
          name.view().substring_of_length(0, *bracket);
      const StringView subscript = name.view().substring_of_length(
          *bracket + 1, name.count() - *bracket - 2);
      try {
        cxt.unset_array_element(array_name, subscript);
      } catch (const Error &error) {
        LOG(verbosity::Debug,
            "unset swallowed an array element error: %s",
            error.message().c_str());
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + name + "' is read-only");
        had_error = true;
      }
    } else {
      /* A read-only variable makes unset_shell_variable throw. The remaining
         names are still unset and the builtin reports a non-zero status, the
         way dash continues past a read-only name. */
      LOG(verbosity::Debug, "unset removing variable '%s'", name.c_str());
      try {
        cxt.unset_shell_variable(name);
      } catch (const Error &error) {
        LOG(verbosity::Debug,
            "unset swallowed a read-only variable error: %s",
            error.message().c_str());
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + name + "' is read-only");
        had_error = true;
      }
    }
  }

  return had_error ? 2 : 0;
}

} /* namespace shit */
