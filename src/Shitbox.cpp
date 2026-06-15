#include "Shitbox.hpp"

#include "Builtin.hpp"
#include "Cli.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "ResolvedCommand.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <algorithm>

namespace shit {

namespace shitbox {

/* The bare-name resolution toggle, mirrored from the set -o shitbox option and
   the --enable-shitbox flag so the command resolver reads it with no context in
   scope. */
static bool g_shitbox_names_enabled = false;

fn shitbox_names_enabled() wontthrow -> bool { return g_shitbox_names_enabled; }

fn set_shitbox_names_enabled(bool enabled) wontthrow -> void
{
  g_shitbox_names_enabled = enabled;
}

flatten fn find_util(StringView name) throws -> Maybe<Util>
{
  return SHITBOX_UTILS.find(name);
}

/* The per-utility flag lists, a zero-initialized table immune to static-init
   order, filled by each utility file's registrar after its FLAG_LIST is built,
   since both sit in the same translation unit in order. */
static const ArrayList<Flag *> *SHITBOX_UTIL_FLAG_LISTS[SHITBOX_UTIL_COUNT] = {};

fn register_shitbox_util_flags(Util chosen,
                               const ArrayList<Flag *> *flags) wontthrow -> void
{
  SHITBOX_UTIL_FLAG_LISTS[static_cast<usize>(chosen)] = flags;
}

fn shitbox_util_flag_list(Util chosen) wontthrow -> const ArrayList<Flag *> *
{
  return SHITBOX_UTIL_FLAG_LISTS[static_cast<usize>(chosen)];
}

fn util_names() throws -> const ArrayList<String> &
{
  static ArrayList<String> names = [] throws {
    let collected = ArrayList<String>{};
    for (const StaticStringMap<Util>::entry &entry : SHITBOX_ENTRIES)
      collected.push(entry.key.to_string());
    return collected;
  }();
  return names;
}

fn run_util(Util chosen, const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32
{
  LOG(Debug, "dispatching shitbox utility %d with %zu arguments", ENUM(chosen),
      args.count());
  switch (chosen) {
  case Util::Ls: return util_ls(ec, cxt, args);
  case Util::Ln: return util_ln(ec, cxt, args);
  case Util::Rm: return util_rm(ec, cxt, args);
  case Util::Mkdir: return util_mkdir(ec, cxt, args);
  case Util::Rmdir: return util_rmdir(ec, cxt, args);
  case Util::Cp: return util_cp(ec, cxt, args);
  case Util::Mv: return util_mv(ec, cxt, args);
  case Util::Cat: return util_cat(ec, cxt, args);
  case Util::Tee: return util_tee(ec, cxt, args);
  case Util::Touch: return util_touch(ec, cxt, args);
  case Util::Basename: return util_basename(ec, cxt, args);
  case Util::Dirname: return util_dirname(ec, cxt, args);
  case Util::Realpath: return util_realpath(ec, cxt, args);
  case Util::Du: return util_du(ec, cxt, args);
  case Util::Head: return util_head(ec, cxt, args);
  case Util::Tail: return util_tail(ec, cxt, args);
  case Util::Wc: return util_wc(ec, cxt, args);
  case Util::Seq: return util_seq(ec, cxt, args);
  case Util::Tr: return util_tr(ec, cxt, args);
  case Util::Grep: return util_grep(ec, cxt, args);
  case Util::Sort: return util_sort(ec, cxt, args);
  case Util::Uniq: return util_uniq(ec, cxt, args);
  case Util::Sleep: return util_sleep(ec, cxt, args);
  case Util::Env: return util_env(ec, cxt, args);
  case Util::Yes: return util_yes(ec, cxt, args);
  case Util::Pkill: return util_pkill(ec, cxt, args);
  case Util::Killall: return util_killall(ec, cxt, args);
  case Util::Kill: return util_kill(ec, cxt, args);
  case Util::Ps: return util_ps(ec, cxt, args);
  case Util::Make: return util_make(ec, cxt, args);
  }
  unreachable("unhandled shitbox utility of kind %d", ENUM(chosen));
}

fn dispatch(const ExecContext &ec, EvalContext &cxt, usize name_index) throws
    -> i32
{
  ASSERT(name_index < ec.args().count());
  let const name = ec.args()[name_index].view();

  /* The utility reads itself as the first argument, so a slice from the name
     onward is copied once and handed over. The copy is small and lets the
     utility parse flags with the name as the program word the way a builtin
     reads ec.args(). */
  ArrayList<String> shifted{};
  shifted.reserve(ec.args().count() - name_index);
  for (usize i = name_index; i < ec.args().count(); i++)
    shifted.push(ec.args()[i].clone());

  if (let const chosen = find_util(name); chosen.has_value()) {
    /* A utility error is rendered as a located error in every mood, so the bash
       mood shows the same caret the default mood does rather than the soft line
       the builtin dispatch would print. The shitbox utilities are a shit
       feature, so they read best with the located form the way the builtins do
       in the default mood. */
    try {
      return run_util(*chosen, ec, cxt, shifted);
    } catch (const Error &error) {
      throw ErrorWithLocation{ec.source_location(), error.message()};
    }
  }

  /* A name that is not a shitbox utility but is a shell builtin, such as echo
     or printf, routes to that builtin rather than getting a second
     implementation, so `shitbox echo hi` runs the echo builtin. The routed
     context carries the same descriptors the outer command was placed on, so
     its output follows a redirection or a pipe. */
  if (let const builtin_kind = search_builtin(name); builtin_kind.has_value()) {
    let routed = ExecContext::from_resolved(
        ec.source_location(), ResolvedCommand::from_builtin(*builtin_kind),
        steal(shifted));
    return execute_builtin(steal(routed), cxt);
  }

  throw ErrorWithLocation{ec.source_location(),
                          "shitbox has no utility named '" + String{name} +
                              "'"};
}

fn run_as_multicall(StringView util_name, ArrayList<String> operands,
                    EvalContext &cxt) throws -> i32
{
  let const chosen = find_util(util_name);
  ASSERT(chosen.has_value());

  ArrayList<String> args{};
  args.reserve(operands.count() + 1);
  args.push(String{util_name});
  for (String &operand : operands)
    args.push(steal(operand));

  let ec = ExecContext::from_resolved(
      SourceLocation{}, ResolvedCommand::from_builtin(Builtin::Kind::Shitbox),
      steal(args));

  try {
    return run_util(*chosen, ec, cxt, ec.args());
  } catch (const Error &e) {
    print_error("shit: " + String{util_name} + ": " + e.message() + "\n");
    return 1;
  }
}

fn parse_util_operands(const ArrayList<Flag *> &flags,
                       const ArrayList<String> &args) throws
    -> ArrayList<String>
{
  ArrayList<String> operands = parse_flags_vec(flags, args, 0);
  /* The first operand is the utility name, the program word parse_flags_vec
     never reads as a flag, so it is dropped to leave the real arguments. */
  if (!operands.is_empty()) operands.remove(0);
  return operands;
}

fn print_util_help(const ExecContext &ec, StringView name, StringView synopsis,
                   StringView description,
                   const ArrayList<Flag *> &flags) throws -> void
{
  let help_text = String{};
  if (!description.is_empty()) {
    help_text += "DESCRIPTION\n";
    help_text += wrap_text(description, HELP_INDENT, HELP_WRAP_WIDTH);
    help_text += "\n\n";
  }
  ArrayList<StringView> synopsis_lines{};
  synopsis_lines.push(synopsis);
  help_text += make_synopsis(name, synopsis_lines);
  help_text += '\n';
  help_text += make_flag_help(flags);
  help_text += '\n';
  ec.print_to_stdout(help_text);
}

fn read_fd_to_string(os::descriptor fd) throws -> String
{
  String contents{};
  char buffer[4096];
  for (;;) {
    let const read_count = os::read_fd(fd, buffer, sizeof(buffer));
    if (!read_count.has_value() || *read_count == 0) break;
    contents.append(StringView{buffer, *read_count});
  }
  return contents;
}

fn read_named_or_stdin(const ExecContext &ec, StringView path) throws
    -> Maybe<String>
{
  if (path == "-") return read_fd_to_string(ec.in_fd.value_or(SHIT_STDIN));

  let const fd = os::open_file_descriptor(path, os::file_open_mode::Read);
  if (!fd.has_value()) return None;
  defer { os::close_fd(*fd); };
  return read_fd_to_string(*fd);
}

fn split_keep_newlines(StringView text) throws -> ArrayList<StringView>
{
  ArrayList<StringView> lines{};
  usize start = 0;
  for (usize i = 0; i < text.length; i++) {
    if (text[i] == '\n') {
      lines.push(text.substring_of_length(start, i - start + 1));
      start = i + 1;
    }
  }
  if (start < text.length)
    lines.push(text.substring_of_length(start, text.length - start));
  return lines;
}

fn sort_string_list(ArrayList<String> &items) wontthrow -> void
{
  std::sort(items.begin(), items.end(),
            [](const String &a, const String &b) { return a < b; });
}

fn format_human_size(u64 bytes) throws -> String
{
  /* A value below 1024 prints as the plain byte count with no suffix, the way
     ls -h and du -h show a small size. */
  if (bytes < 1024) return utils::uint_to_text(bytes);

  static const char units[] = {'K', 'M', 'G', 'T', 'P'};
  double value = static_cast<double>(bytes);
  usize unit = 0;
  /* The condition reads unit, not unit + 1, so the last unit P is reachable
     rather than the scan stopping a unit early. */
  while (value >= 1024.0 && unit < sizeof(units)) {
    value /= 1024.0;
    unit++;
  }

  /* Rounding the scaled value can reach 1024, which belongs in the next unit, so
     a value that would render as 1024K crosses over to 1.0M when a larger unit
     is available. */
  if (value >= 1023.5 && unit < sizeof(units)) {
    value /= 1024.0;
    unit++;
  }

  String out{};
  /* A scaled value below ten keeps one decimal, the way coreutils renders 1.5K,
     while a larger value rounds to a whole number such as 23K. A one-decimal
     value that rounds up to ten drops the decimal so it reads 10K, not 10.0K. */
  let const tenths = static_cast<u64>(value * 10.0 + 0.5);
  if (value < 10.0 && tenths < 100) {
    out += utils::uint_to_text(tenths / 10);
    out += '.';
    out += utils::uint_to_text(tenths % 10);
  } else {
    out += utils::uint_to_text(static_cast<u64>(value + 0.5));
  }
  out.push(units[unit - 1]);
  return out;
}

fn report_soft_shitbox_error(const ExecContext &ec, EvalContext &cxt,
                             StringView message) throws -> void
{
  /* A keep-going utility error renders the located caret in every mood, the
     same form a thrown utility error gets, so a bad operand in a list reads the
     same whether the mood is the default or the bash one. The fallback line is
     for the rare case with no source to caret against, such as the multicall
     entry. */
  const ErrorWithLocation located{ec.source_location(), message};
  if (const String *source = cxt.current_source(); source != nullptr)
    show_message(located.to_string(source->view()));
  else
    print_error("shit: " + String{message} + "\n");
}

} /* namespace shitbox */

} /* namespace shit */
