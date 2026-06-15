#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[aux]");

HELP_DESCRIPTION_DECL(
    "The ps utility lists the running processes. The plain form prints the "
    "process id and the command name. The aux or -aux form prints the owner, "
    "the process id, and the full command line of every process.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Ps);

namespace shit {

namespace shitbox {

/* One uid resolved to its name once, so a listing with many processes owned by
   the same user reads the passwd file once per distinct uid rather than once per
   process. */
struct uid_name_cache_entry
{
  u32 uid{0};
  String name{};
};

/* The owner name for a uid, served from the cache when seen before and read from
   the passwd file and remembered otherwise. A uid with no passwd entry caches
   and returns its numeric form. */
static fn owner_name_for_uid(u32 uid, ArrayList<uid_name_cache_entry> &cache) throws
    -> String
{
  for (const uid_name_cache_entry &entry : cache)
    if (entry.uid == uid) return entry.name.clone();

  let const looked_up = os::uid_to_username(uid);
  String name = looked_up.has_value() ? *looked_up : utils::uint_to_text(uid);
  cache.push(uid_name_cache_entry{uid, name.clone()});
  return name;
}

/* The wide aux listing, the owner and the pid and the full command line of every
   process in space-aligned columns. */
static fn render_aux(const ArrayList<os::process_entry> &processes,
                     String &output) throws -> void
{
  ArrayList<uid_name_cache_entry> uid_cache{};
  ArrayList<String> owners{};
  owners.reserve(processes.count());

  usize user_width = 4; /* the USER header */
  usize pid_width = 3;  /* the PID header */
  for (const os::process_entry &process : processes) {
    String owner = owner_name_for_uid(process.owner_id, uid_cache);
    if (owner.count() > user_width) user_width = owner.count();
    let const pid_text = utils::int_to_text(process.pid);
    if (pid_text.count() > pid_width) pid_width = pid_text.count();
    owners.push(steal(owner));
  }

  output += "USER";
  for (usize i = 4; i < user_width; i++)
    output += ' ';
  output += ' ';
  for (usize i = 3; i < pid_width; i++)
    output += ' ';
  output += "PID COMMAND\n";

  for (usize i = 0; i < processes.count(); i++) {
    const os::process_entry &process = processes[i];
    output += owners[i].view();
    for (usize p = owners[i].count(); p < user_width; p++)
      output += ' ';
    output += ' ';
    let const pid_text = utils::int_to_text(process.pid);
    for (usize p = pid_text.count(); p < pid_width; p++)
      output += ' ';
    output += pid_text.view();
    output += ' ';
    output += process.command_line.is_empty() ? process.name.view()
                                              : process.command_line.view();
    output += '\n';
  }
}

fn util_ps(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  unused(cxt);

  /* The -aux spelling is the classic ps form rather than a bundle of single
     letter flags, so it is recognized before flag parsing rather than rejected
     as an unknown flag. */
  ArrayList<String> flag_args{};
  flag_args.reserve(args.count());
  bool show_aux = false;
  for (usize i = 0; i < args.count(); i++) {
    if (i > 0 && args[i].view() == "-aux") {
      show_aux = true;
      continue;
    }
    flag_args.push(args[i].clone());
  }

  let const operands = parse_util_operands(FLAG_LIST, flag_args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  for (const String &operand : operands)
    if (operand.view() == "aux") show_aux = true;

  let const processes = os::enumerate_processes();

  let output = String{};
  if (show_aux) {
    render_aux(processes, output);
    ec.print_to_stdout(output);
    return 0;
  }

  /* The pid is right-justified in five columns under a PID CMD header, the way
     a small ps lays the two fields out. */
  output += "  PID CMD\n";
  for (const os::process_entry &process : processes) {
    let const pid = utils::int_to_text(process.pid);
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
