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
  ArrayList<String> args = parse_flags_vec(FLAG_LIST, ec.args());
  SHIT_DEFER { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  for (usize i = 1; i < args.size(); i++) {
    const String &arg = args[i];
    Maybe<usize> equals_position = arg.find_character('=');

    std::string name{};
    std::string value{};
    if (!equals_position.has_value()) {
      /* Export an existing variable by its current value. */
      name = std::string{arg.c_str(), arg.size()};
      value = cxt.get_variable_value(arg).value_or("");
    } else {
      StringView name_view = arg.substring_of_length(0, *equals_position);
      StringView value_view = arg.substring(*equals_position + 1);
      name = std::string{name_view.data, name_view.length};
      value = std::string{value_view.data, value_view.length};
    }

    /* The variable moves into the environment, so the bare shell copy is
       removed and child processes inherit it. */
    cxt.unset_shell_variable(name);
    os::set_environment_variable(name, value);
  }

  return 0;
}

} /* namespace shit */
