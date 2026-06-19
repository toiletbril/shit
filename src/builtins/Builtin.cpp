#include "../Builtin.hpp"

#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../ResolvedCommand.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* builtin runs the named shell builtin with its arguments, bypassing a shell
   function of the same name and the PATH. A bare builtin in the shit mood
   surveys every builtin in columns, and a name that is not a registered builtin
   is an error. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[--list] [name [argument ...]]");

HELP_DESCRIPTION_DECL(
    "The builtin builtin runs name with its arguments as a shell builtin, "
    "ignoring a shell function of the same name and never searching the PATH. "
    "With --list it prints every builtin one per line. A bare builtin in the "
    "shit mood prints the builtins in columns, and in the other moods it "
    "succeeds without running anything. A name that is not a shell builtin is "
    "an "
    "error.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(BUILTIN_LIST, Bool, '\0', "list", "List every builtin one per line.");

REGISTER_BUILTIN_FLAGS(BuiltinBuiltin);

namespace shit {

BuiltinBuiltin::BuiltinBuiltin() = default;

pure fn BuiltinBuiltin::kind() const wontthrow -> Builtin::Kind
{
  return Kind::BuiltinBuiltin;
}

/* The registered builtin names, sorted, so both listing forms read the same
   set. */
static fn sorted_builtin_names() throws -> ArrayList<String>
{
  let names = ArrayList<String>{};
  for (let const &builtin_name : builtin_names())
    names.push_managed(builtin_name);
  names.sort();
  return names;
}

/* The builtins laid out in columns that fit a usual terminal width, the survey
   a bare builtin prints in the shit mood. */
static fn print_builtin_columns(ExecContext &ec) throws -> void
{
  let const sorted = sorted_builtin_names();

  usize longest = 0;
  for (let const &builtin_name : sorted)
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
    let const is_last_in_row =
        i % columns == columns - 1 || i + 1 == sorted.count();
    if (is_last_in_row) {
      out += "\n";
    } else {
      for (usize pad = sorted[i].length(); pad < column_width; pad++)
        out += " ";
    }
  }
  ec.print_to_stdout(out.view());
}

fn BuiltinBuiltin::execute(ExecContext &ec, EvalContext &cxt) const throws
    -> i32
{
  /* A bare builtin surveys the builtins in columns in the shit mood, and is the
     POSIX no-op success in the other moods. */
  if (ec.args().count() < 2) {
    if (cxt.mood() == mimic_mood::Default) print_builtin_columns(ec);
    return 0;
  }

  /* The flags are not parsed generically, since every argument after the name
     belongs to the target builtin and must pass through untouched. Only the
     bare
     --help and --list on the builtin word itself are intercepted. */
  let const &name = ec.args()[1];
  if (name == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (name == "--list") {
    let const sorted = sorted_builtin_names();
    let out = String{};
    for (let const &builtin_name : sorted) {
      out += builtin_name.view();
      out += "\n";
    }
    ec.print_to_stdout(out.view());
    return 0;
  }

  LOG(Debug, "builtin forwarding to '%s' past functions and PATH",
      name.c_str());

  let const target = search_builtin(name.view());
  if (!target.has_value()) {
    report_soft_builtin_error(
        ec, cxt, StringView{"'"} + name + "' is not a shell builtin");
    return 1;
  }

  /* The name and its arguments are forwarded to a fresh context resolved
     directly to the target builtin, so the function table and the PATH are both
     skipped. */
  let forwarded = ArrayList<String>{};
  for (usize i = 1; i < ec.args().count(); i++)
    forwarded.push_managed(ec.args()[i]);
  let sub = ExecContext::from_resolved(ec.source_location(),
                                       ResolvedCommand::from_builtin(*target),
                                       steal(forwarded));
  return execute_builtin(steal(sub), cxt);
}

} /* namespace shit */
