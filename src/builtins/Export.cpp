#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[NAME[=VALUE] ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Export::Export() = default;

Builtin::Kind
Export::kind() const
{
  return Kind::Export;
}

i32
Export::execute(ExecContext &ec, EvalContext &cxt) const
{
  std::vector<std::string> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  for (usize i = 1; i < args.size(); i++) {
    const std::string &arg = args[i];
    usize equals_position = arg.find('=');

    std::string name{};
    std::string value{};
    if (equals_position == std::string::npos) {
      /* Export an existing variable by its current value. */
      name = arg;
      value = cxt.get_variable_value(name).value_or("");
    } else {
      name = arg.substr(0, equals_position);
      value = arg.substr(equals_position + 1);
    }

    /* The variable moves into the environment, so the bare shell copy is
       removed and child processes inherit it. */
    cxt.unset_shell_variable(name);
    os::set_environment_variable(name, value);
  }

  return 0;
}

} /* namespace shit */
