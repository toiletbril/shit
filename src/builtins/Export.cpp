#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[NAME[=VALUE] ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Export::Export() = default;

fn Export::kind() const -> Builtin::Kind { return Kind::Export; }

fn Export::execute(ExecContext &ec, EvalContext &cxt) const -> i32
{
  let const args = parse_flags_vec(FLAG_LIST, ec.args());
  SHIT_DEFER { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  for (usize i = 1; i < args.size(); i++) {
    let const &arg = args[i];
    let const equals_position = arg.find_character('=');

    let name = String{};
    let value = String{};
    if (!equals_position.has_value()) {
      /* Export an existing variable by its current value. */
      name = arg;
      value = cxt.get_variable_value(arg).value_or(String{});
    } else {
      name = String{arg.substring_of_length(0, *equals_position)};
      value = String{arg.substring(*equals_position + 1)};
    }

    /* The variable moves into the environment, so the bare shell copy is
       removed and child processes inherit it. */
    cxt.unset_shell_variable(name);
    os::set_environment_variable(name, value);
  }

  return 0;
}

} /* namespace shit */
