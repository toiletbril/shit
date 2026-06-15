#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

HELP_DESCRIPTION_DECL(
    "The ps utility lists the running processes, the process id and the command "
    "name of each.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Ps);

namespace shit {

namespace shitbox {

fn util_ps(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };
  unused(operands);

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const processes = os::enumerate_processes();
  /* The pid is right-justified in five columns under a PID CMD header, the way
     a small ps lays the two fields out. */
  let output = String{"  PID CMD\n"};
  for (const os::process_entry &process : processes) {
    let const pid = utils::int_to_text(process.pid, heap_allocator());
    for (usize i = pid.count(); i < 5; i++)
      output += ' ';
    output += pid.view();
    output += ' ';
    output += process.name.view();
    output += '\n';
  }

  ec.print_to_stdout(output);
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
