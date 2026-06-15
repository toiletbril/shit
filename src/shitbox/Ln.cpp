#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-sf] target link");

HELP_DESCRIPTION_DECL(
    "The ln utility creates a link named link to the target. Only symbolic "
    "links are supported, through the -s flag.");

FLAG(LN_SYMBOLIC, Bool, 's', "", "Create a symbolic link.");
FLAG(LN_FORCE, Bool, 'f', "", "Remove an existing destination first.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_ln(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  if (operands.count() < 2) throw Error{"ln expects a target and a link name"};

  let const target = operands[0].view();
  let const link = operands[1].view();

  if (!FLAG_LN_SYMBOLIC.is_enabled())
    throw Error{"ln supports only symbolic links, pass -s"};

  if (FLAG_LN_FORCE.is_enabled()) os::remove_file(link);

  if (!os::create_symlink(target, link))
    throw Error{"ln: cannot create symbolic link '" + String{link} +
                "': " + os::last_system_error_message()};

  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
