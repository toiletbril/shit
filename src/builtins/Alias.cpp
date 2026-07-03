#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../NameValueArg.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[name[=value] ...]");

HELP_DESCRIPTION_DECL("The alias builtin defines and prints command aliases.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Alias);

namespace shit {

Alias::Alias() = default;

pure fn Alias::kind() const wontthrow -> Builtin::Kind { return Kind::Alias; }

fn Alias::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  if (args.count() == 1) {
    let out = String{cxt.scratch_allocator()};
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
      String message{cxt.scratch_allocator(), "alias "};
      message += arg;
      message += "='";

      for (usize value_position = 0; value_position < value->count();
           value_position++)
      {
        let const character = (*value)[value_position];

        if (character == '\'')
          message += "'\\''";
        else
          message += character;
      }

      message += "'\n";
      ec.print_to_stdout(message);
    } else {
      /* A missing alias reports on stderr so it does not mix into a captured
         alias listing. */
      String not_found{cxt.scratch_allocator(), "alias: "};
      not_found += arg;
      not_found += ": not found\n";
      ec.print_to_stderr(not_found);
      status = 1;
    }
  }

  return status;
}

} // namespace shit
