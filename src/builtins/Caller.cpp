/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the caller builtin. The caller
 * builtin prints the calling context of a function or a sourced file, and
 * returns zero in a call and one at the top level.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Eval.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[expr]");

HELP_DESCRIPTION_DECL(
    "The caller builtin prints the calling context of a function or a "
    "sourced file, and returns zero in a call and one at the top level.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Caller);

namespace koshka {

Caller::Caller() = default;

pure fn Caller::kind() const wontthrow -> Builtin::Kind { return Kind::Caller; }

fn Caller::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let const has_frame_operand = args.count() > 1;
  let const source_count = cxt.bash_source_frame_count();

  if (source_count == 0) return 1;

  usize frame_index = 0;

  if (has_frame_operand) {
    let const parsed = args[1].to<i64>();
    if (parsed.is_error()) {
      report_soft_builtin_error(
          ec, cxt, operand_locations[1],
          StringView{"'"} + args[1] + "' is not a whole number",
          "The frame index must be a whole number such as `caller 0`");

      return 2;
    }

    if (parsed.value() < 0) return 1;

    frame_index = static_cast<usize>(parsed.value());

    if (frame_index + 1 >= cxt.funcname_frame_count()) return 1;
  }

  let out = String{cxt.scratch_allocator()};
  out +=
      String::from(cxt.funcname_line_at(frame_index), cxt.scratch_allocator());
  out += ' ';

  if (has_frame_operand) {
    out.append(cxt.funcname_frame_at(frame_index + 1));
    out += ' ';
    out.append(cxt.bash_source_frame_at(frame_index + 1));
  } else if (source_count > 1) {
    out.append(cxt.bash_source_frame_at(1));
  } else {
    out.append(StringView{"NULL"});
  }

  out += '\n';
  ec.print_to_stdout(out);

  return 0;
}

} /* namespace koshka */
