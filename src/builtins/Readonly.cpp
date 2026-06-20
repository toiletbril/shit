#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../NameValueArg.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[name[=value] ...]");

HELP_DESCRIPTION_DECL(
    "The readonly builtin marks each named variable read-only. A later "
    "assignment to it fails, and an operand with an equals sign assigns the "
    "value first. With no operand it lists every read-only variable and its "
    "value.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(READONLY_PRINT, Bool, 'p', "",
     "List the read-only variables in a reusable form.");

REGISTER_BUILTIN_FLAGS(Readonly);

namespace shit {

Readonly::Readonly() = default;

pure Builtin::Kind Readonly::kind() const wontthrow { return Kind::Readonly; }

i32 Readonly::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  /* A bare readonly, and the -p form which strips to the same bare argument
     vector, list every read-only variable. The bash mood prints the declare -r
     form bash reloads, while the default and sh moods print the POSIX readonly
     form dash reloads. */
  if (args.count() == 1) {
    let const is_declare_form = cxt.is_bash_compatible();
    let out = String{};
    for (let const &name : cxt.readonly_names()) {
      out += is_declare_form ? "declare -r " : "readonly ";
      out += name;
      if (let const value = cxt.get_variable_value(name)) {
        if (is_declare_form) {
          out += "=\"";
          out += quote_for_declare(value->view());
          out += "\"";
        } else {
          out += "='";
          out += value->view();
          out += "'";
        }
      }
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];
    let const parts = NameValueArg::from(arg);

    LOG(All, "readonly marking '%.*s' against later assignment",
        static_cast<int>(parts.get_name().length), parts.get_name().data);

    /* An operand with an equals sign assigns the value first, then marks the
       name. A bare name marks whatever it currently holds. */
    if (parts.get_value().has_value()) {
      cxt.set_shell_variable(parts.get_name(), *parts.get_value());
      cxt.mark_readonly(parts.get_name());
    } else {
      cxt.mark_readonly(arg);
    }
  }

  return 0;
}

} // namespace shit
