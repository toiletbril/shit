#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* local declares a variable local to the current function, so the value it had
   in the caller returns when the function ends. It is an error outside a
   function. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("local name[=value] ...");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Local::Local() = default;

Builtin::Kind Local::kind() const { return Kind::Local; }

i32 Local::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (!cxt.in_function_scope())
    throw Error{"'local' can only be used inside a function"};

  for (usize i = 1; i < args.size(); i++) {
    const String &arg = args[i];
    const Maybe<usize> equals_position = arg.find_character('=');

    /* Record the shadowed binding before overwriting it, so leaving the
       function restores it. A bare name shadows with an empty value. */
    const StringView name = equals_position.has_value()
                          ? arg.substring_of_length(0, *equals_position)
                          : arg.view();
    cxt.declare_local(name);

    if (equals_position.has_value())
      cxt.set_shell_variable(name, arg.substring(*equals_position + 1));
    else
      cxt.set_shell_variable(name, "");
  }

  return 0;
}

} /* namespace shit */
