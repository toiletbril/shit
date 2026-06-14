#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* readonly marks a variable so a later assignment to it fails, or with no
   operand lists the variables already marked. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[name[=value] ...]");

HELP_DESCRIPTION_DECL(
    "The readonly builtin marks each named variable so a later assignment to "
    "it "
    "fails, assigning the value first when an operand carries an equals sign. "
    "With no operand it lists every read-only variable and its value.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Readonly);

namespace shit {

Readonly::Readonly() = default;

pure Builtin::Kind Readonly::kind() const wontthrow { return Kind::Readonly; }

i32 Readonly::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  /* readonly with no operand lists the read-only variables and their values. */
  if (args.count() == 1) {
    let out = String{};
    for (let const &name : cxt.readonly_names()) {
      out += "readonly ";
      out += name;
      if (let const value = cxt.get_variable_value(name)) {
        out += "='";
        out += value->view();
        out += "'";
      }
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];
    let const parts = utils::split_name_value_arg(arg);

    LOG(All, "readonly marking '%.*s' against later assignment",
        static_cast<int>(parts.name.length), parts.name.data);

    /* An operand with an equals sign assigns the value first, then marks the
       name. A bare name marks whatever it currently holds. */
    if (parts.value.has_value()) {
      cxt.set_shell_variable(parts.name, *parts.value);
      cxt.mark_readonly(parts.name);
    } else {
      cxt.mark_readonly(arg);
    }
  }

  return 0;
}

} /* namespace shit */
