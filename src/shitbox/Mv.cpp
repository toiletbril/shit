#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-fv] source ... destination");

HELP_DESCRIPTION_DECL(
    "The mv utility renames each source to the destination. When more than one "
    "source is given the destination must be a directory. With -v it names "
    "each "
    "move as it happens.");

FLAG(MV_FORCE, Bool, 'f', "", "Overwrite an existing destination.");
FLAG(MV_VERBOSE, Bool, 'v', "", "Print the name of each move as it happens.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Mv);

namespace shit {

namespace shitbox {

Mv::Mv() = default;

pure fn Mv::kind() const wontthrow -> Utility::Kind { return Kind::Mv; }

fn Mv::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  let const destination = operands[operands.count() - 1].view();
  let const destination_is_directory = Path{destination}.is_directory();

  if (operands.count() > 2 && !destination_is_directory) {
    throw Error{
        "mv: the destination '" + String{cxt.scratch_allocator(), destination}
          +
        "' is not a directory, so it cannot hold several sources"
    };
  }

  let output = String{cxt.scratch_allocator()};
  i32 status = 0;
  for (usize i = 0; i + 1 < operands.count(); i++) {
    let const source = operands[i].view();
    let target = String{cxt.scratch_allocator(), destination};
    if (destination_is_directory)
      target = PathBuilder{destination}
                   .append(Path{source}.filename())
                   .build()
                   .text();

    if (!os::rename_path(source, target.view())) {
      report_soft_shitbox_error(ec, cxt,
                                "mv: unable to move '" +
                                    String{cxt.scratch_allocator(), source} +
                                    "' to '" + target + "' because " +
                                    os::last_system_error_message());
      status = 1;
      continue;
    }
    if (FLAG_MV_VERBOSE.is_enabled())
      output += "renamed '" + String{cxt.scratch_allocator(), source} +
                "' -> '" + target + "'\n";
  }
  ec.print_to_stdout(output);
  return status;
}

} // namespace shitbox

} // namespace shit
