#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[NAME[=VALUE] ...]");
HELP_DESCRIPTION_DECL(
    "The export builtin moves each named variable into the environment so "
    "later "
    "commands and child processes inherit it, and it assigns a new value when "
    "NAME=VALUE is given. A read-only variable rejects a new value, and "
    "exporting PATH refreshes command resolution.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(EXPORT_PRINT, Bool, 'p', "",
     "List the exported variables in a reusable form.");
FLAG(EXPORT_UNMARK, Bool, 'n', "",
     "Remove the export attribute, keeping the variable in the shell.");

REGISTER_BUILTIN_FLAGS(Export);

namespace shit {

Export::Export() = default;

pure fn Export::kind() const wontthrow -> Builtin::Kind { return Kind::Export; }

fn Export::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  /* A bare export, and the -p form, list every exported variable. The bash mood
     prints the declare -x form bash reloads, while the default and sh moods
     print the POSIX export form dash reloads. The names are sorted so the
     listing is stable. */
  if (FLAG_EXPORT_PRINT.is_enabled() || args.count() == 1) {
    let const is_declare_form = cxt.is_bash_compatible();
    let names = os::environment_names();
    names.sort();

    let out = String{cxt.scratch_allocator()};
    for (let const &name : names) {
      let const value = os::get_environment_variable(name).value_or(
          String{cxt.scratch_allocator()});
      out += is_declare_form ? "declare -x " : "export ";
      out += name;
      if (is_declare_form) {
        out += "=\"";
        out += quote_for_declare(value.view());
        out += "\"";
      } else {
        out += "='";
        out += value.view();
        out += "'";
      }
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  /* -n removes the export attribute and keeps the variable as a plain shell
     variable holding the value it had in the environment, the reverse of the
     move export makes below. */
  if (FLAG_EXPORT_UNMARK.is_enabled()) {
    for (usize i = 1; i < args.count(); i++) {
      /* A NAME=VALUE operand assigns the new value and then unexports NAME,
         while a bare name keeps the value it held in the environment, the way
         bash reads export -n. */
      let const parts = NameValueArg::from(args[i]);
      let const name = String{cxt.scratch_allocator(), parts.get_name()};
      let const value =
          parts.get_value().has_value()
              ? Maybe<String>{String{cxt.scratch_allocator(),
                                     *parts.get_value()}}
              : os::get_environment_variable(name);

      LOG(All, "export removing '%s' from the environment", name.c_str());
      cxt.record_environment_change(name);
      os::unset_environment_variable(name);
      cxt.unmark_exported(name);
      if (value.has_value()) cxt.set_shell_variable(name, value->view());
    }

    return 0;
  }

  let has_error = false;
  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];
    let const parts = NameValueArg::from(arg);

    let name = String{cxt.scratch_allocator()};
    let value = String{cxt.scratch_allocator()};
    let const has_new_value = parts.get_value().has_value();
    if (!has_new_value) {
      name = arg;
      value =
          cxt.get_variable_value(arg).value_or(String{cxt.scratch_allocator()});
    } else {
      name = String{cxt.scratch_allocator(), parts.get_name()};
      value = String{cxt.scratch_allocator(), *parts.get_value()};
    }

    /* A read-only variable rejects a new value the way an ordinary assignment
       does, and a bare re-export of a read-only variable leaves its value
       untouched. */
    if (cxt.is_readonly(name)) {
      if (has_new_value) {
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + name + "' is read-only");
        has_error = true;
      }
      continue;
    }

    /* An integer name evaluates its new value as arithmetic the way the
       variable store does, so the environment receives the decimal result
       rather than the raw expression text. */
    let const is_integer_name = cxt.is_integer_variable(name.view());
    if (has_new_value && is_integer_name) {
      char result_text[24];
      value = String{
          cxt.scratch_allocator(),
          utils::int_to_text_into(
              value.is_empty() ? 0 : cxt.evaluate_arithmetic(value.view()),
              result_text, sizeof(result_text))};
    }

    /* The variable moves into the environment, so the bare shell copy is
       removed and child processes inherit it. The unset is this move, not a
       user unset, so the integer mark it clears is put back. */
    LOG(All, "export moving '%s' into the environment", name.c_str());
    cxt.unset_shell_variable(name);
    if (is_integer_name) cxt.mark_integer(name.view());
    cxt.record_environment_change(name);
    os::set_environment_variable(name, value);
    cxt.mark_exported(name);
    /* The unset above pointed the resolver at the now-removed environment PATH,
       so an export PATH=... refreshes it to the value just placed in the
       environment. */
    if (name == "PATH") utils::set_path_for_resolution(String{value.view()});
  }

  return has_error ? 2 : 0;
}

} // namespace shit
