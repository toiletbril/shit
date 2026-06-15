#include "../Shitbox.hpp"

#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* The shitbox builtin hosts the busybox-style coreutils. `shitbox ls` runs the
   ls utility, and when the bare names resolve as commands the resolver routes
   ls straight here with the name already at the front. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[utility] [arg ...]");

HELP_DESCRIPTION_DECL(
    "Run a bundled coreutility. With the --enable-shitbox flag or set -o "
    "shitbox the utility names resolve directly as commands without the "
    "prefix, "
    "and a name that is already a builtin runs that builtin.");

FLAG(HELP, Bool, '\0', "help", "Display help and list the utilities.");
FLAG(SHITBOX_LIST, Bool, '\0', "list", "List the utility names, one per line.");

REGISTER_BUILTIN_FLAGS(Shitbox);

namespace shit {

Shitbox::Shitbox() = default;

pure Builtin::Kind Shitbox::kind() const wontthrow { return Kind::Shitbox; }

i32 Shitbox::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  /* A bare-name invocation arrives with the utility name already at args[0],
     so it dispatches with the name in place. The resolver sets this up when the
     shitbox option is on. */
  if (shitbox::find_util(ec.args()[0].view()).has_value())
    return shitbox::dispatch(ec, cxt, 0);

  /* The sorted utility names, shared by the --list output and the help
     listing. */
  let sorted_names = ArrayList<String>{};
  for (const String &name : shitbox::util_names())
    sorted_names.push(name.clone());
  shitbox::sort_string_list(sorted_names);

  /* --list prints the utility names one per line and nothing else, so a script
     can read the set without parsing the help text. */
  if (ec.args().count() >= 2 && ec.args()[1] == "--list") {
    let names_output = String{};
    for (const String &name : sorted_names) {
      names_output += name.view();
      names_output += '\n';
    }
    ec.print_to_stdout(names_output);
    return 0;
  }

  if (ec.args().count() < 2 || ec.args()[1] == "--help") {
    /* The help reads as three sections, the description, the two synopsis
       lines, and the utilities as a comma-separated block wrapped under the
       standard indent. */
    let listing = String{};
    listing += "DESCRIPTION\n";
    listing += wrap_text(HELP_DESCRIPTION, HELP_INDENT, HELP_WRAP_WIDTH);
    listing += "\n\nSYNOPSIS\n";
    listing += "  shitbox [utility] [arg ...]\n";
    listing += "  shitbox --list\n";
    listing += "\nUTILITIES\n";

    let joined_names = String{};
    for (const String &name : sorted_names) {
      if (!joined_names.is_empty()) joined_names += ", ";
      joined_names += name.view();
    }
    listing += wrap_text(joined_names.view(), HELP_INDENT, HELP_WRAP_WIDTH);
    listing += '\n';

    ec.print_to_stdout(listing);
    return 0;
  }

  return shitbox::dispatch(ec, cxt, 1);
}

} /* namespace shit */
