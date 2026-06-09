#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* local declares a variable local to the current function, so the value it had
   in the caller returns when the function ends. It is an error outside a
   function. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("name[=value] ...");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Local::Local() = default;

pure Builtin::Kind Local::kind() const wontthrow { return Kind::Local; }

i32 Local::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  if (!cxt.in_function_scope())
    throw Error{"'local' can only be used inside a function"};

  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];
    let const equals_position = arg.find_character('=');

    /* Record the shadowed binding before overwriting it, so leaving the
       function restores it. A bare name declares the local without touching the
       value, so the currently-visible binding from the caller stays readable
       until the body assigns the name, matching dash. */
    let const name = equals_position.has_value()
                         ? arg.substring_of_length(0, *equals_position)
                         : arg.view();
    cxt.declare_local(name);

    if (equals_position.has_value())
      cxt.set_shell_variable(name, arg.substring(*equals_position + 1));
  }

  return 0;
}

} /* namespace shit */
