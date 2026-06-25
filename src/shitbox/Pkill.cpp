#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-l] [-s signal] pattern");

HELP_DESCRIPTION_DECL(
    "The pkill utility sends a signal to every process whose name contains the "
    "pattern. The signal defaults to TERM and may be a name or a number. The "
    "status is zero when a process matched and one when none did. With -l it "
    "lists the signal names and exits.");

FLAG(PKILL_SIGNAL, String, 's', "signal",
     "The signal to send, a name such as TERM or a number such as 15.");
FLAG(PKILL_LIST, Bool, 'l', "list", "List the signal names and exit.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Pkill);

namespace shit {

namespace shitbox {

/* Resolve the -s value to a signal number, a decimal number directly or a name
   such as TERM or SIGKILL through the platform table. Defaults to TERM. */
fn resolve_shitbox_signal(StringView spelled) throws -> i32
{
  if (spelled.is_empty()) return SIGTERM;
  let const parsed = utils::parse_decimal_integer(spelled);
  if (!parsed.is_error()) return static_cast<i32>(parsed.value());
  let const named = os::signal_number_from_name(spelled);
  if (!named.has_value())
    throw Error{"unknown signal '" + String{spelled} + "'"};
  return *named;
}

Pkill::Pkill() = default;

pure fn Pkill::kind() const wontthrow -> Utility::Kind { return Kind::Pkill; }

fn Pkill::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (FLAG_PKILL_LIST.is_enabled()) {
    ec.print_to_stdout(format_signal_list());
    return 0;
  }

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() != 1) throw Error{"pkill expects one pattern"};

  let const pattern = operands[0].view();
  let const signal_number = resolve_shitbox_signal(
      FLAG_PKILL_SIGNAL.is_set() ? FLAG_PKILL_SIGNAL.value() : StringView{});

  let const self_pid = os::get_shell_process_id();
  let const processes = os::enumerate_processes();
  bool did_signal_any = false;
  for (const os::process_entry &process : processes) {
    if (process.pid == self_pid) continue;
    if (process.name.find_substring(pattern, 0).has_value()) {
      if (os::signal_process(os::process_from_pid(process.pid), signal_number))
        did_signal_any = true;
    }
  }

  return did_signal_any ? 0 : 1;
}

} // namespace shitbox

} // namespace shit
