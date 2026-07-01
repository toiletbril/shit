#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

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

REGISTER_BUILTIN_FLAGS(Unset);

namespace shit {

Unset::Unset() = default;

pure fn Unset::kind() const wontthrow -> Builtin::Kind { return Kind::Unset; }

fn Unset::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const names = PARSE_BUILTIN_ARGS(ec);

  ASSERT(!names.is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const should_unset_function = FLAG_UNSET_FUNCTION.is_enabled();
  let has_error = false;
  for (usize i = 1; i < names.count(); i++) {
    let const &name = names[i];
    if (should_unset_function) {
      LOG(All, "unset removing function '%s'", name.c_str());
      cxt.unset_function(name);
    } else if (let const bracket = name.view().find_character('[');
               bracket.has_value() && name.count() > 0 &&
               name.view()[name.count() - 1] == ']')
    {
      const StringView array_name =
          name.view().substring_of_length(0, *bracket);
      const StringView subscript = name.view().substring_of_length(
          *bracket + 1, name.count() - *bracket - 2);
      try {
        cxt.unset_array_element(array_name, subscript);
      } catch (const Error &error) {
        LOG(All, "unset swallowed an array element error: %s",
            error.message().c_str());
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + name + "' is read-only");
        has_error = true;
      }
    } else {
      /* A read-only name throws, the rest are still unset, matching dash. */
      LOG(All, "unset removing variable '%s'", name.c_str());
      try {
        cxt.unset_shell_variable(name);
      } catch (const Error &error) {
        LOG(All, "unset swallowed a read-only variable error: %s",
            error.message().c_str());
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + name + "' is read-only");
        has_error = true;
      }
    }
  }

  return has_error ? 2 : 0;
}

} // namespace shit
