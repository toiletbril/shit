#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-f] source ... destination");

HELP_DESCRIPTION_DECL(
    "The mv utility renames each source to the destination. When more than one "
    "source is given the destination must be a directory.");

FLAG(MV_FORCE, Bool, 'f', "", "Overwrite an existing destination.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_mv(const ExecContext &ec, EvalContext &cxt,
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

  if (operands.count() < 2)
    throw Error{"mv expects a source and a destination"};

  let const destination = operands[operands.count() - 1].view();
  let const destination_is_directory = Path{destination}.is_directory();

  if (operands.count() > 2 && !destination_is_directory)
    throw Error{"mv: target '" + String{destination} + "' is not a directory"};

  i32 status = 0;
  for (usize i = 0; i + 1 < operands.count(); i++) {
    let const source = operands[i].view();
    String target{destination};
    if (destination_is_directory)
      target = PathBuilder{destination}
                   .append(Path{source}.filename())
                   .build()
                   .text();

    if (!os::rename_path(source, target.view())) {
      report_soft_shitbox_error(ec, cxt,
                                "mv: cannot move '" + String{source} +
                                    "' to '" + target +
                                    "': " + os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
