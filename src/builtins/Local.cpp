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

Builtin::Kind
Local::kind() const
{
  return Kind::Local;
}

i32
Local::execute(ExecContext &ec, EvalContext &cxt) const
{
  std::vector<std::string> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (!cxt.in_function_scope())
    throw Error{"'local' can only be used inside a function"};

  for (usize i = 1; i < args.size(); i++) {
    const std::string &arg = args[i];
    usize equals_position = arg.find('=');

    /* Record the shadowed binding before overwriting it, so leaving the
       function restores it. A bare name shadows with an empty value. */
    std::string name = equals_position != std::string::npos
                           ? arg.substr(0, equals_position)
                           : arg;
    cxt.declare_local(name);

    if (equals_position != std::string::npos)
      cxt.set_shell_variable(name, arg.substr(equals_position + 1));
    else
      cxt.set_shell_variable(name, "");
  }

  return 0;
}

} /* namespace shit */
