#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-sf] target link");

HELP_DESCRIPTION_DECL(
    "The ln utility creates a symbolic link to the target.");

FLAG(LN_SYMBOLIC, Bool, 's', "", "Create a symbolic link.");
FLAG(LN_FORCE, Bool, 'f', "", "Remove an existing destination first.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Ln);

namespace shit {

namespace shitbox {

Ln::Ln() = default;

pure fn Ln::kind() const wontthrow -> Utility::Kind { return Kind::Ln; }

fn Ln::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  let const target = operands[0].view();
  let const link = operands[1].view();

  if (!FLAG_LN_SYMBOLIC.is_enabled())
    throw Error{"ln supports only symbolic links, pass -s"};

  if (FLAG_LN_FORCE.is_enabled()) os::remove_file(link);

  if (!os::create_symlink(target, link)) {
    throw Error{
        "ln: cannot create symbolic link '" +
        String{cxt.scratch_allocator(), link}
        +
        "': " + os::last_system_error_message()
    };
  }

  return 0;
}

} // namespace shitbox

} // namespace shit
