/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the link utility. It validates two operands and creates
 * one hard link through the platform filesystem interface.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("file1 file2");

HELP_DESCRIPTION_DECL("The link utility creates a hard link to a file.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Link);

namespace koshka::koshkit {

Link::Link() = default;

pure fn Link::kind() const wontthrow -> Utility::Kind { return Kind::Link; }

fn Link::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const operands =
      PARSE_KOSHKIT_ARGS_WITH_LOCATIONS(args, arg_locations, operand_locations);

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() > 2) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[2],
                            "extra operand '" + operands[2] + "'");
    return 1;
  }

  if (os::create_hard_link(operands[0].view(), operands[1].view())) return 0;
  KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                          "cannot create '" + operands[1] +
                              "': " + os::last_system_error_message());
  return 1;
}

} // namespace koshka::koshkit
