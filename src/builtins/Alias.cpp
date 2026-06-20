#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../NameValueArg.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[name[=value] ...]");

HELP_DESCRIPTION_DECL(
    "The alias builtin defines a word replacement that the shell expands "
    "before "
    "it resolves a command. A name=value operand defines an alias, a bare name "
    "operand prints that alias, and with no operand the builtin lists every "
    "alias already defined.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Alias);

namespace shit {

Alias::Alias() = default;

pure fn Alias::kind() const wontthrow -> Builtin::Kind { return Kind::Alias; }

fn Alias::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args =
      parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  if (args.count() == 1) {
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
  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];
    let const parts = NameValueArg::from(arg);

    if (parts.get_value().has_value()) {
      LOG(All, "alias defining '%.*s'",
          static_cast<int>(parts.get_name().length), parts.get_name().data);
      cxt.set_alias(parts.get_name(), *parts.get_value());
    } else if (const Maybe<String> value = cxt.get_alias(arg)) {
      String message = "alias ";
      message += arg;
      message += "='";
      message += *value;
      message += "'\n";
      ec.print_to_stdout(message);
    } else {
      /* bash reports a missing alias on stderr, so a caller that captures the
         standard output of alias, as ble.sh does, does not see this line mixed
         into the captured definitions. */
      ec.print_to_stderr("alias: " + arg + ": not found\n");
      status = 1;
    }
  }

  return status;
}

} // namespace shit
