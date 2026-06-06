#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"

/* unalias removes a command alias, or with -a removes every alias. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("unalias [-a] name [name ...]");

FLAG(ALL, Bool, 'a', "", "Remove every alias.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Unalias::Unalias() = default;

pure Builtin::Kind Unalias::kind() const wontthrow { return Kind::Unalias; }

i32 Unalias::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.empty());

  if (FLAG_ALL.is_enabled()) {
    /* alias_definitions yields name='value', so the name ends at the equals. */
    for (let const &definition : cxt.alias_definitions()) {
      let const equals_position = definition.find_character('=');
      let const name_length =
          equals_position.has_value() ? *equals_position : definition.size();
      let const name = definition.substring_of_length(0, name_length);
      cxt.remove_alias(name);
    }
    return 0;
  }

  i32 status = 0;
  for (usize i = 1; i < args.size(); i++) {
    let const &name = args[i];
    if (!cxt.remove_alias(name)) {
      ec.print_to_stdout(name + ": not found\n");
      status = 1;
    }
  }

  return status;
}

} /* namespace shit */
