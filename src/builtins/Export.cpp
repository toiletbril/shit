#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
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

namespace shit {

Export::Export() = default;

pure fn Export::kind() const wontthrow -> Builtin::Kind { return Kind::Export; }

fn Export::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = parse_flags_vec(FLAG_LIST, ec.args());
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let had_error = false;
  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];
    let const parts = utils::split_name_value_arg(arg);

    let name = String{};
    let value = String{};
    let const has_new_value = parts.value.has_value();
    if (!has_new_value) {
      /* Export an existing variable by its current value. */
      name = arg;
      value = cxt.get_variable_value(arg).value_or(String{});
    } else {
      name = String{parts.name};
      value = String{*parts.value};
    }

    /* A read-only variable rejects a new value the way an ordinary assignment
       does, and a bare re-export of a read-only variable leaves its value
       untouched. */
    if (cxt.is_readonly(name)) {
      if (has_new_value) {
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + name + "' is read-only");
        had_error = true;
      }
      continue;
    }

    /* The variable moves into the environment, so the bare shell copy is
       removed and child processes inherit it. Inside a subshell the prior
       environment value is logged so the export does not leak past it. */
    cxt.unset_shell_variable(name);
    cxt.record_environment_change(name);
    os::set_environment_variable(name, value);
    cxt.mark_exported(name);
    /* The unset above pointed the resolver at the now-removed environment PATH,
       so an export PATH=... refreshes it to the value just placed in the
       environment. */
    if (name == "PATH") utils::set_path_for_resolution(String{value.view()});
  }

  return had_error ? 2 : 0;
}

} /* namespace shit */
