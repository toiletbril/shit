#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

/* alias defines a command word replacement, or with no operand lists the
   replacements already defined. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("alias [name[=value] ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Alias::Alias() = default;

Builtin::Kind
Alias::kind() const
{
  return Kind::Alias;
}

i32
Alias::execute(ExecContext &ec, EvalContext &cxt) const
{
  std::vector<std::string> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* alias with no operand lists every definition. */
  if (args.size() == 1) {
    std::string out{};
    for (const std::string &definition : cxt.alias_definitions())
      out += "alias " + definition + "\n";
    ec.print_to_stdout(out);
    return 0;
  }

  i32 status = 0;
  for (usize i = 1; i < args.size(); i++) {
    const std::string &arg = args[i];
    usize equals_position = arg.find('=');

    if (equals_position != std::string::npos) {
      cxt.set_alias(arg.substr(0, equals_position),
                    arg.substr(equals_position + 1));
    } else if (Maybe<std::string> value = cxt.get_alias(arg)) {
      ec.print_to_stdout("alias " + arg + "='" + *value + "'\n");
    } else {
      ec.print_to_stdout(arg + ": not found\n");
      status = 1;
    }
  }

  return status;
}

} /* namespace shit */
