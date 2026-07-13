#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../ResolvedCommand.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[option ...] duration command [argument ...]");

HELP_DESCRIPTION_DECL("The timeout utility runs a command with a time limit.");

FLAG(TIMEOUT_SIGNAL, String, 's', "signal",
     "Send this signal when the time limit expires.");
FLAG(TIMEOUT_KILL_AFTER, String, 'k', "kill-after",
     "Send KILL when the command survives this additional duration.");
FLAG(TIMEOUT_PRESERVE_STATUS, Bool, 'p', "preserve-status",
     "Return the command status after the time limit expires.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Timeout);

namespace shit {

namespace shitbox {

Timeout::Timeout() = default;

pure fn Timeout::kind() const wontthrow -> Utility::Kind
{
  return Kind::Timeout;
}

static fn duration_to_nanos(f64 seconds) wontthrow -> u64
{
  if (__builtin_isinf(seconds)) return UINT64_MAX;

  constexpr f64 NANOS_PER_SECOND = 1000000000.0;
  constexpr f64 MAX_SECONDS = static_cast<f64>(UINT64_MAX) / NANOS_PER_SECOND;
  if (seconds >= MAX_SECONDS) return UINT64_MAX;

  return static_cast<u64>(seconds * NANOS_PER_SECOND);
}

static fn wait_for_process_until(os::process child, u64 timeout_nanos,
                                 i32 &status) throws -> bool
{
  if (timeout_nanos == 0 || timeout_nanos == UINT64_MAX) {
    status = os::wait_and_monitor_process(child);
    return true;
  }

  let const started_at_nanos = os::monotonic_nanos();
  loop
  {
    let const state = os::poll_process(child, status);
    if (state == os::process_state::Exited) return true;

    let const elapsed_nanos = os::monotonic_nanos() - started_at_nanos;
    if (elapsed_nanos >= timeout_nanos) return false;

    let const remaining_nanos = timeout_nanos - elapsed_nanos;
    let const sleep_nanos =
        remaining_nanos < 1000000 ? remaining_nanos : 1000000;
    os::sleep_for_seconds(static_cast<f64>(sleep_nanos) / 1000000000.0);
  }
}

static fn signal_supervised_process(os::process child,
                                    i32 signal_number) wontthrow -> bool
{
  let const process_group = os::process_group_of(child);
  if (os::signal_process(process_group, signal_number)) return true;
  return os::signal_process(child, signal_number);
}

static fn resolve_timeout_program(StringView program_name) throws -> Maybe<Path>
{
  for (usize byte_index = 0; byte_index < program_name.length; byte_index++)
    if (os::is_directory_separator(program_name[byte_index]))
      return Path::canonicalize(program_name);

  let const matches = utils::search_program_path(program_name);
  if (matches.is_empty()) return None;
  return matches[0];
}

fn Timeout::execute(const ExecContext &ec, EvalContext &cxt,
                    const ArrayList<String> &args,
                    const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const operands =
      parse_util_operands(FLAG_LIST, args, &arg_locations, &operand_locations);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  let const timeout_seconds = parse_shitbox_duration_seconds(
      operands[0].view(), StringView{"timeout"}, cxt.scratch_allocator());
  let const timeout_nanos = duration_to_nanos(timeout_seconds);

  u64 kill_after_nanos = 0;
  if (FLAG_TIMEOUT_KILL_AFTER.is_set()) {
    let const kill_after_seconds = parse_shitbox_duration_seconds(
        FLAG_TIMEOUT_KILL_AFTER.value(), StringView{"timeout"},
        cxt.scratch_allocator());
    kill_after_nanos = duration_to_nanos(kill_after_seconds);
  }

  let const timeout_signal = resolve_shitbox_signal(
      FLAG_TIMEOUT_SIGNAL.is_set() ? FLAG_TIMEOUT_SIGNAL.value()
                                   : StringView{"TERM"},
      cxt.scratch_allocator());
  if (!os::is_process_signal_supported(timeout_signal))
    throw Error{"timeout cannot deliver signal " +
                String::from(timeout_signal, cxt.scratch_allocator()) +
                " on this platform"};

  let const program_path = resolve_timeout_program(operands[1].view());
  if (!program_path.has_value()) {
    ec.print_to_stderr("timeout: command '" + operands[1] +
                       "' was not found\n");
    return 127;
  }

  let command_args = ArrayList<String>{cxt.scratch_allocator()};
  let command_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  command_args.reserve(operands.count() - 1);
  command_locations.reserve(operands.count() - 1);
  for (usize operand_index = 1; operand_index < operands.count();
       operand_index++)
  {
    command_args.push_managed(operands[operand_index]);
    command_locations.push(operand_locations[operand_index]);
  }

  let command = ExecContext::from_resolved(
      ec.source_location(), ResolvedCommand::from_program(*program_path),
      steal(command_args), steal(command_locations));
  os::process child = SHIT_INVALID_PROCESS;
  try {
    child = os::execute_program(steal(command), true, true);
  } catch (const ExecFormatError &) {
    let const shell_path = os::current_executable_path();
    if (!shell_path.has_value()) throw;

    let fallback_args = ArrayList<String>{cxt.scratch_allocator()};
    let fallback_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
    fallback_args.reserve(operands.count());
    fallback_locations.reserve(operands.count());
    fallback_args.push(String{cxt.scratch_allocator(), shell_path->view()});
    fallback_locations.push(ec.source_location());
    for (usize operand_index = 1; operand_index < operands.count();
         operand_index++)
    {
      fallback_args.push_managed(operands[operand_index]);
      fallback_locations.push(operand_locations[operand_index]);
    }

    let fallback = ExecContext::from_resolved(
        ec.source_location(),
        ResolvedCommand::from_program(Path{shell_path->view()}),
        steal(fallback_args), steal(fallback_locations));
    child = os::execute_program(steal(fallback), false, true);
  }

  i32 status = 0;
  if (wait_for_process_until(child, timeout_nanos, status)) return status;

  if (!signal_supervised_process(child, timeout_signal)) {
    if (os::poll_process(child, status) == os::process_state::Exited)
      return status;
    signal_supervised_process(child, 9);
    os::reap_process_quietly(child);
    return 125;
  }

  if (let const continue_signal = os::signal_number_from_name("CONT");
      continue_signal.has_value() &&
      os::is_process_signal_supported(*continue_signal))
  {
    signal_supervised_process(child, *continue_signal);
  }

  if (kill_after_nanos != 0 &&
      !wait_for_process_until(child, kill_after_nanos, status))
  {
    if (!signal_supervised_process(child, 9)) {
      if (os::poll_process(child, status) == os::process_state::Exited)
        return FLAG_TIMEOUT_PRESERVE_STATUS.is_enabled() ? status : 124;
      return 125;
    }
    os::reap_process_quietly(child);
    return 137;
  }

  if (kill_after_nanos == 0) status = os::reap_process_quietly(child);

  return FLAG_TIMEOUT_PRESERVE_STATUS.is_enabled() ? status : 124;
}

} /* namespace shitbox */

} /* namespace shit */
