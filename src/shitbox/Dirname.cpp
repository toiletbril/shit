#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("path");

HELP_DESCRIPTION_DECL(
    "The dirname utility prints the path with its final component removed, the "
    "directory the path names a file in.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_dirname(const ExecContext &ec, EvalContext &cxt,
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

  if (operands.is_empty()) throw Error{"dirname expects a path"};

  let const parent = Path{operands[0].view()}.parent();
  /* A path with no separator names a file in the current directory, so the
     directory is the dot the way dirname reports. */
  let const text = parent.is_empty() ? StringView{"."} : parent.text().view();
  ec.print_to_stdout(String{text} + "\n");
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
