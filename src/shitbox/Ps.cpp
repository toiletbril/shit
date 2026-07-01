#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[aux]");

HELP_DESCRIPTION_DECL(
    "The ps utility lists the running processes.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Ps);

namespace shit {

namespace shitbox {

struct uid_name_cache_entry
{
  uid_name_cache_entry(u32 uid, String name) : uid(uid), name(steal(name)) {}
  u32 uid;
  String name;
};

static fn owner_name_for_uid(u32 uid, ArrayList<uid_name_cache_entry> &cache,
                             Allocator allocator) throws -> String
{
  for (const uid_name_cache_entry &entry : cache)
    if (entry.uid == uid) return entry.name.clone();

  let const looked_up = os::uid_to_username(uid);
  String name = looked_up.has_value() ? String{allocator, looked_up->view()}
                                      : String::from(uid, allocator);
  cache.push(uid_name_cache_entry{uid, name.clone()});
  return name;
}

static fn append_left(String &output, StringView text, usize width) throws
    -> void
{
  output += text;
  for (usize i = text.length; i < width; i++)
    output += ' ';
}

static fn append_right(String &output, StringView text, usize width) throws
    -> void
{
  for (usize i = text.length; i < width; i++)
    output += ' ';
  output += text;
}

/* The %CPU, %MEM, TTY, and START columns of a full ps are omitted, since they
   need a sampling pass and a controlling-terminal map this listing does not
   gather. */
static fn render_aux(const ArrayList<os::process_entry> &processes,
                     String &output, Allocator allocator) throws -> void
{
  ArrayList<uid_name_cache_entry> uid_cache{allocator};
  ArrayList<String> owners{allocator};
  owners.reserve(processes.count());

  usize user_width = 4;
  usize pid_width = 3;
  usize vsz_width = 3;
  usize rss_width = 3;

  for (const os::process_entry &process : processes) {
    String owner = owner_name_for_uid(process.owner_id, uid_cache, allocator);
    if (owner.count() > user_width) user_width = owner.count();
    let const pid_text = String::from(process.pid, allocator);
    if (pid_text.count() > pid_width) pid_width = pid_text.count();
    let const vsz_text = String::from(process.virtual_kib, allocator);
    if (vsz_text.count() > vsz_width) vsz_width = vsz_text.count();
    let const rss_text = String::from(process.resident_kib, allocator);
    if (rss_text.count() > rss_width) rss_width = rss_text.count();
    owners.push(steal(owner));
  }

  append_left(output, "USER", user_width);
  output += ' ';
  append_right(output, "PID", pid_width);
  output += ' ';
  append_right(output, "VSZ", vsz_width);
  output += ' ';
  append_right(output, "RSS", rss_width);
  output += " STAT COMMAND\n";

  for (usize i = 0; i < processes.count(); i++) {
    const os::process_entry &process = processes[i];
    append_left(output, owners[i].view(), user_width);
    output += ' ';
    append_right(output, String::from(process.pid, allocator).view(),
                 pid_width);
    output += ' ';
    append_right(output, String::from(process.virtual_kib, allocator).view(),
                 vsz_width);
    output += ' ';
    append_right(output, String::from(process.resident_kib, allocator).view(),
                 rss_width);
    output += ' ';
    /* The state is padded to the four-wide STAT field plus a separator space.
     */
    output += process.state;
    output += "    ";
    output += process.command_line.is_empty() ? process.name.view()
                                              : process.command_line.view();
    output += '\n';
  }
}

Ps::Ps() = default;

pure fn Ps::kind() const wontthrow -> Utility::Kind { return Kind::Ps; }

fn Ps::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  /* The -aux spelling is recognized before flag parsing. */
  ArrayList<String> flag_args{cxt.scratch_allocator()};
  flag_args.reserve(args.count());
  bool should_show_aux = false;
  for (usize i = 0; i < args.count(); i++) {
    let const argument = args[i].view();
    /* A dashed bundle of only a, u, x, and w is the classic ps spelling, the
       a, u, and x select the aux view. */
    if (i > 0 && argument.length > 1 && argument[0] == '-') {
      let is_only_ps_options = true;
      for (usize k = 1; k < argument.length; k++)
        if (argument[k] != 'a' && argument[k] != 'u' && argument[k] != 'x' &&
            argument[k] != 'w')
        {
          is_only_ps_options = false;
          break;
        }
      if (is_only_ps_options) {
        for (usize k = 1; k < argument.length; k++)
          if (argument[k] == 'a' || argument[k] == 'u' || argument[k] == 'x')
            should_show_aux = true;
        continue;
      }
    }
    flag_args.push(args[i].clone());
  }

  let const operands = parse_util_operands(FLAG_LIST, flag_args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  for (const String &operand : operands)
    if (operand.view() == "aux") should_show_aux = true;

  let const processes = os::enumerate_processes(should_show_aux);

  let output = String{cxt.scratch_allocator()};
  if (should_show_aux) {
    render_aux(processes, output, cxt.scratch_allocator());
    ec.print_to_stdout(output);
    return 0;
  }

  output += "  PID CMD\n";
  for (const os::process_entry &process : processes) {
    let const pid = String::from(process.pid, cxt.scratch_allocator());
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

} // namespace shitbox

} // namespace shit
