#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../NameValueArg.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[name[=value] ...]");

HELP_DESCRIPTION_DECL(
    "The readonly builtin marks each named variable read-only.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(READONLY_PRINT, Bool, 'p', "",
     "List the read-only variables in a reusable form.");

REGISTER_BUILTIN_FLAGS(Readonly);

namespace shit {

Readonly::Readonly() = default;

pure fn Readonly::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Readonly;
}

static pure fn name_is_valid_identifier(StringView name) wontthrow -> bool
{
  if (name.is_empty()) return false;

  let const do_is_name_start = [](char c) wontthrow -> bool {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };

  if (!do_is_name_start(name[0])) return false;

  for (usize position = 1; position < name.length; position++) {
    let const c = name[position];
    if (!do_is_name_start(c) && !(c >= '0' && c <= '9')) return false;
  }

  return true;
}

fn Readonly::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  /* A bare readonly lists every read-only variable, in the declare -r form
     under the bash mood and the POSIX readonly form otherwise. */
  if (args.count() == 1) {
    let const is_declare_form = cxt.is_bash_compatible();
    let out = String{cxt.scratch_allocator()};
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
          for (usize k = 0; k < value->count(); k++) {
            if ((*value)[k] == '\'')
              out += "'\\''";
            else
              out.push((*value)[k]);
          }
          out += "'";
        }
      }
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  let has_error = false;
  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];
    let const parts = NameValueArg::from(arg);

    if (!name_is_valid_identifier(parts.get_name())) {
      report_soft_builtin_error(
          ec, cxt, StringView{"'"} + arg + "' is not a valid identifier");
      has_error = true;
      continue;
    }

    LOG(All, "readonly marking '%.*s' against later assignment",
        static_cast<int>(parts.get_name().length), parts.get_name().data);

    if (parts.get_value().has_value()) {
      cxt.set_shell_variable(parts.get_name(), *parts.get_value());
      cxt.mark_readonly(parts.get_name());
    } else {
      cxt.mark_readonly(arg);
    }
  }

  return has_error ? 1 : 0;
}

} // namespace shit
