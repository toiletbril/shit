#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("file");

HELP_DESCRIPTION_DECL(
    "The unlink utility removes the single named file through one unlink call. "
    "It does not remove a directory.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Unlink);

namespace shit {

namespace shitbox {

Unlink::Unlink() = default;

pure Utility::Kind Unlink::kind() const wontthrow { return Kind::Unlink; }

fn Unlink::execute(const ExecContext &ec, EvalContext &cxt,
                   const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  /* unlink takes exactly one operand, the way GNU unlink does. */
  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() > 1) {
    report_soft_shitbox_error(ec, cxt,
                              "unlink: extra operand '" + operands[1] + "'");
    return 1;
  }

  /* unlink removes the link itself, so a symlink to a directory is fine and only
     a real directory is refused, the way unlink(2) and GNU unlink behave. */
  let const &target = operands[0];
  let const target_path = Path{target.view()};
  if (target_path.is_directory() && !target_path.is_symbolic_link()) {
    report_soft_shitbox_error(ec, cxt,
                              "unlink: cannot unlink '" + target +
                                  "': it is a directory");
    return 1;
  }

  /* The single-file removal is the shared rm path with recursion off. */
  if (!remove_path(target.view(), false)) {
    report_soft_shitbox_error(ec, cxt,
                              "unlink: cannot unlink '" + target + "': " +
                                  os::last_system_error_message());
    return 1;
  }
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
