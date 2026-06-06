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

fn Alias::kind() const -> Builtin::Kind { return Kind::Alias; }

fn Alias::execute(ExecContext &ec, EvalContext &cxt) const -> i32
{
  let const args = parse_flags_vec(FLAG_LIST, ec.args());
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* alias with no operand lists every definition. */
  if (args.size() == 1) {
    let out = String{};
    for (let const &definition : cxt.alias_definitions()) {
      out += "alias ";
      out += definition;
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  i32 status = 0;
  for (usize i = 1; i < args.size(); i++) {
    let const &arg = args[i];
    let const equals_position = arg.find_character('=');

    if (equals_position.has_value()) {
      cxt.set_alias(arg.substring_of_length(0, *equals_position),
                    arg.substring(*equals_position + 1));
    } else if (Maybe<String> value = cxt.get_alias(arg)) {
      String message = "alias ";
      message += arg;
      message += "='";
      message += *value;
      message += "'\n";
      ec.print_to_stdout(message);
    } else {
      ec.print_to_stdout(arg + ": not found\n");
      status = 1;
    }
  }

  return status;
}

} /* namespace shit */
