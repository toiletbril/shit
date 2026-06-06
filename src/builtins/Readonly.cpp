#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

/* readonly marks a variable so a later assignment to it fails, or with no
   operand lists the variables already marked. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("readonly [name[=value] ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Readonly::Readonly() = default;

Builtin::Kind
Readonly::kind() const
{
  return Kind::Readonly;
}

i32
Readonly::execute(ExecContext &ec, EvalContext &cxt) const
{
  std::vector<std::string> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* readonly with no operand lists the read-only variables and their values. */
  if (args.size() == 1) {
    std::string out{};
    for (const String &name : cxt.readonly_names()) {
      std::string name_text{name.c_str(), name.size()};
      out += "readonly " + name_text;
      if (Maybe<std::string> value = cxt.get_variable_value(name_text))
        out += "='" + *value + "'";
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  for (usize i = 1; i < args.size(); i++) {
    const std::string &arg = args[i];
    usize equals_position = arg.find('=');

    /* An operand with an equals sign assigns the value first, then marks the
       name. A bare name marks whatever it currently holds. */
    if (equals_position != std::string::npos) {
      std::string name = arg.substr(0, equals_position);
      std::string value = arg.substr(equals_position + 1);
      cxt.set_shell_variable(name, value);
      cxt.mark_readonly(name);
    } else {
      cxt.mark_readonly(arg);
    }
  }

  return 0;
}

} /* namespace shit */
