#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[string ...]");

HELP_DESCRIPTION_DECL(
    "The yes utility writes the given string, or y by default, on its own line "
    "over and over until its output is closed.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

fn util_yes(const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  String line{};
  if (operands.is_empty())
    line += "y";
  else
    for (usize i = 0; i < operands.count(); i++) {
      if (i > 0) line += ' ';
      line += operands[i].view();
    }
  line += '\n';

  /* The bytes go straight to the command's output, so a reader such as head
     that closes its end breaks the write and ends the loop rather than the
     utility spinning forever. */
  let const out = ec.out_fd.value_or(SHIT_STDOUT);
  for (;;) {
    let const written = os::write_fd(out, line.view().data, line.count());
    if (!written.has_value() || *written == 0) break;
  }
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
