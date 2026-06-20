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
   the same user reads the passwd file once per distinct uid rather than once
   per process. */
struct uid_name_cache_entry
{
  u32 uid{0};
  String name{};
};

static fn owner_name_for_uid(u32 uid,
                             ArrayList<uid_name_cache_entry> &cache) throws
    -> String
{
  for (const uid_name_cache_entry &entry : cache)
    if (entry.uid == uid) return entry.name.clone();

  let const looked_up = os::uid_to_username(uid);
  String name = looked_up.has_value() ? *looked_up : utils::uint_to_text(uid);
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
                     String &output) throws -> void
{
  ArrayList<uid_name_cache_entry> uid_cache{};
  ArrayList<String> owners{};
  owners.reserve(processes.count());

  usize user_width = 4;
  usize pid_width = 3;
  usize vsz_width = 3;
  usize rss_width = 3;

  for (const os::process_entry &process : processes) {
    String owner = owner_name_for_uid(process.owner_id, uid_cache);
    if (owner.count() > user_width) user_width = owner.count();
    if (utils::int_to_text(process.pid).count() > pid_width)
      pid_width = utils::int_to_text(process.pid).count();
    if (utils::uint_to_text(process.virtual_kib).count() > vsz_width)
      vsz_width = utils::uint_to_text(process.virtual_kib).count();
    if (utils::uint_to_text(process.resident_kib).count() > rss_width)
      rss_width = utils::uint_to_text(process.resident_kib).count();
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
    append_right(output, utils::int_to_text(process.pid).view(), pid_width);
    output += ' ';
    append_right(output, utils::uint_to_text(process.virtual_kib).view(),
                 vsz_width);
    output += ' ';
    append_right(output, utils::uint_to_text(process.resident_kib).view(),
                 rss_width);
    output += ' ';
    /* The state is one letter padded to the four-wide STAT field plus a
       trailing separator space. */
    output += process.state;
    output += "    ";
    output += process.command_line.is_empty() ? process.name.view()
                                              : process.command_line.view();
    output += '\n';
  }
}

Ps::Ps() = default;

pure Utility::Kind Ps::kind() const wontthrow { return Kind::Ps; }

fn Ps::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);

  /* The -aux spelling is the classic ps form rather than a bundle of single
     letter flags, so it is recognized before flag parsing rather than rejected
     as an unknown flag. */
  ArrayList<String> flag_args{};
  flag_args.reserve(args.count());
  bool should_show_aux = false;
  for (usize i = 0; i < args.count(); i++) {
    let const argument = args[i].view();
    /* A dashed bundle of only a, u, x, and w is the classic ps spelling rather
       than this shell's single-letter flags, so it is recognized here. The a,
       u, and x select the aux view, and the w widths are accepted since the
       command line already prints in full. */
    if (i > 0 && argument.length > 1 && argument[0] == '-') {
      bool only_ps_options = true;
      for (usize k = 1; k < argument.length; k++)
        if (argument[k] != 'a' && argument[k] != 'u' && argument[k] != 'x' &&
            argument[k] != 'w')
        {
          only_ps_options = false;
          break;
        }
      if (only_ps_options) {
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

  let output = String{};
  if (should_show_aux) {
    render_aux(processes, output);
    ec.print_to_stdout(output);
    return 0;
  }

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

} // namespace shitbox

} // namespace shit
