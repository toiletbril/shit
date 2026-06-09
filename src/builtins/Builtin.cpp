#include "../Builtin.hpp"

#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../ResolvedCommand.hpp"
#include "../Utils.hpp"

/* builtin runs the named shell builtin with its arguments, bypassing a shell
   function of the same name and the PATH. A bare builtin is a no-op success,
   and a name that is not a registered builtin is an error. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("name [argument ...]");

HELP_DESCRIPTION_DECL(
    "The builtin builtin runs name with its arguments as a shell builtin, "
    "ignoring a shell function of the same name and never searching the PATH. "
    "A bare builtin succeeds without running anything, and a name that is not "
    "a "
    "shell builtin is an error.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

BuiltinBuiltin::BuiltinBuiltin() = default;

pure fn BuiltinBuiltin::kind() const wontthrow -> Builtin::Kind
{
  return Kind::BuiltinBuiltin;
}

fn BuiltinBuiltin::execute(ExecContext &ec, EvalContext &cxt) const throws
    -> i32
{
  /* The flags are not parsed generically, since every argument after the name
     belongs to the target builtin and must pass through untouched. Only the
     bare --help on the builtin word itself is intercepted. */
  if (ec.args().count() < 2) return 0;

  let const &name = ec.args()[1];
  if (name == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* -l, or its long form --list, prints every registered builtin, sorted and
     laid out in columns that fit a usual terminal width, so a reader can see at
     a glance what shit carries. */
  if (name == "-l" || name == "--list") {
    let sorted = ArrayList<String>{};
    for (const String &builtin_name : builtin_names())
      sorted.push_managed(builtin_name);
    utils::sort_ascending(sorted);

    usize longest = 0;
    for (const String &builtin_name : sorted)
      if (builtin_name.length() > longest) longest = builtin_name.length();
    let const column_width = longest + 2;
    let const columns = column_width >= 78 ? usize{1} : 78 / column_width;

    let out = String{};
    out += "shit has ";
    out += utils::int_to_text(static_cast<i64>(sorted.count()), heap_allocator());
    out += " builtins:\n\n";
    for (usize i = 0; i < sorted.count(); i++) {
      if (i % columns == 0) out += "  ";
      out += sorted[i].view();
      let const last_in_row =
          i % columns == columns - 1 || i + 1 == sorted.count();
      if (last_in_row) {
        out += "\n";
      } else {
        for (usize pad = sorted[i].length(); pad < column_width; pad++)
          out += " ";
      }
    }
    ec.print_to_stdout(out.view());
    return 0;
  }

  let const target = search_builtin(name.view());
  if (!target.has_value()) {
    report_soft_builtin_error(
        ec, cxt, StringView{"'"} + name + "' is not a shell builtin");
    return 1;
  }

  /* The name and its arguments are forwarded to a fresh context resolved
     directly to the target builtin, so the function table and the PATH are
     both skipped. */
  let forwarded = ArrayList<String>{};
  for (usize i = 1; i < ec.args().count(); i++)
    forwarded.push_managed(ec.args()[i]);
  let sub = ExecContext::from_resolved(ec.source_location(),
                                       ResolvedCommand::from_builtin(*target),
                                       steal(forwarded));
  return execute_builtin(steal(sub), cxt);
}

} /* namespace shit */
