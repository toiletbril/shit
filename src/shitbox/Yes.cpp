#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[string ...]");

HELP_DESCRIPTION_DECL(
    "The yes utility writes the given string, or y by default, on its own line "
    "over and over until its output is closed.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Yes);

namespace shit {

namespace shitbox {

Yes::Yes() = default;

pure Utility::Kind Yes::kind() const wontthrow { return Kind::Yes; }

fn Yes::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args) const throws -> i32
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
  let const out_fd = ec.out_fd.value_or(SHIT_STDOUT);
  loop
  {
    /* A Ctrl-C at the terminal sets the shell's interrupt flag, so the loop
       checks it each pass and stops rather than spinning forever. */
    if (os::INTERRUPT_REQUESTED) return 130;

    let const written = os::write_fd(out_fd, line.view().data, line.count());
    if (!written.has_value() || *written == 0) break;
  }

  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
