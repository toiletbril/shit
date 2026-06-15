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

HELP_SYNOPSIS_DECL("utility [argument ...]");

HELP_DESCRIPTION_DECL(
    "The shitbox builtin runs a small bundled coreutility such as ls, mkdir, "
    "or "
    "cp, so a system with only a compiler can run a configure and a make under "
    "shit. With the --enable-shitbox flag or set -o shitbox the utility names "
    "resolve directly as commands without the shitbox prefix.");

FLAG(HELP, Bool, '\0', "help", "Display help and list the utilities.");

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

  if (ec.args().count() < 2 || ec.args()[1] == "--help") {
    /* The listing follows the busybox multi-call form, a banner, the two usage
       lines, and the defined utilities as a comma-separated block indented with
       a tab and wrapped, sorted so it reads the same on every run. */
    let listing = String{};
    listing += "shitbox multi-call binary.\n\n";
    listing += "Usage: shitbox UTILITY [ARGUMENT]...\n";
    listing += "   or: UTILITY [ARGUMENT]...\n\n";
    listing += "Run a bundled coreutility. With the --enable-shitbox flag or "
               "set -o shitbox\nthe utility names resolve directly as commands "
               "without the prefix, and a\nname that is already a builtin runs "
               "that builtin.\n\n";
    listing += "Currently defined utilities:\n";

    ArrayList<String> names{};
    for (const String &name : shitbox::util_names())
      names.push(name.clone());
    shitbox::sort_string_list(names);

    String line{"\t"};
    for (usize i = 0; i < names.count(); i++) {
      let piece = names[i].clone();
      if (i + 1 < names.count()) piece += ",";
      /* A new line starts once the running one would pass the wrap width, with
         every line carrying the leading tab. */
      if (line.count() > 1 && line.count() + piece.count() + 1 > 70) {
        listing += line.view();
        listing += '\n';
        line = "\t";
      }
      if (line.count() > 1) line += ' ';
      line += piece.view();
    }
    listing += line.view();
    listing += '\n';

    ec.print_to_stdout(listing);
    return 0;
  }

  return shitbox::dispatch(ec, cxt, 1);
}

} /* namespace shit */
