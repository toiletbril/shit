#include "Shitbox.hpp"

#include "Builtin.hpp"
#include "Cli.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "ResolvedCommand.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

namespace shitbox {

Utility::Utility() = default;

flatten fn find_util(StringView name) throws -> Maybe<Utility::Kind>
{
  return SHITBOX_UTILS.find(name);
}

/* Zero-initialized so it is immune to static-init order, filled by each
   utility's registrar. */
static const ArrayList<Flag *> *SHITBOX_UTIL_FLAG_LISTS[SHITBOX_UTIL_COUNT] =
    {};

fn register_shitbox_util_flags(Utility::Kind chosen,
                               const ArrayList<Flag *> *flags) wontthrow -> void
{
  SHITBOX_UTIL_FLAG_LISTS[static_cast<usize>(chosen)] = flags;
}

fn shitbox_util_flag_list(Utility::Kind chosen) wontthrow
    -> const ArrayList<Flag *> *
{
  return SHITBOX_UTIL_FLAG_LISTS[static_cast<usize>(chosen)];
}

fn util_names() throws -> const ArrayList<String> &
{
  static ArrayList<String> names = [] throws {
    let collected = ArrayList<String>{heap_allocator()};
    for (const StaticStringMap<Utility::Kind>::entry &entry : SHITBOX_ENTRIES)
      collected.push(entry.key.to_string());
    return collected;
  }();
  return names;
}

fn run_util(Utility::Kind chosen, const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32
{
  LOG(Debug, "dispatching shitbox utility %d with %zu arguments", ENUM(chosen),
      args.count());
  switch (chosen) {
    UTILITY_SWITCH_CASES();
  }
  unreachable("unhandled shitbox utility of kind %d", ENUM(chosen));
}

fn dispatch(const ExecContext &ec, EvalContext &cxt, usize name_index) throws
    -> i32
{
  ASSERT(name_index < ec.args().count());
  let const name = ec.args()[name_index].view();

  ArrayList<String> shifted{heap_allocator()};
  shifted.reserve(ec.args().count() - name_index);
  for (usize i = name_index; i < ec.args().count(); i++)
    shifted.push(ec.args()[i].clone());

  if (let const chosen = find_util(name); chosen.has_value()) {
    try {
      return run_util(*chosen, ec, cxt, shifted);
    } catch (const ErrorWithLocation &error) {
      /* The caret offsets into the utility's own argument vector, so it is
         re-pointed at the command location. */
      throw relocate_error(error, ec.source_location());
    } catch (const Error &error) {
      throw relocate_error(error, ec.source_location());
    }
  }

  /* A name that is not a utility but is a shell builtin routes to that builtin. */
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

  /* The scan stops at --, where a later --version is an operand. */
  for (const String &operand : operands) {
    if (operand == "--") break;
    if (operand == "--version") {
      show_version();
      return 0;
    }
  }

  ArrayList<String> args{heap_allocator()};
  args.reserve(operands.count() + 1);
  args.push(String{util_name});
  for (String &operand : operands)
    args.push(steal(operand));

  let ec = ExecContext::from_resolved(
      SourceLocation{}, ResolvedCommand::from_builtin(Builtin::Kind::Shitbox),
      steal(args));
  ec.is_multicall = true;

  try {
    return run_util(*chosen, ec, cxt, ec.args());
  } catch (const ErrorWithLocation &e) {
    show_message(e.to_string(utils::merge_args_to_string(ec.args())));
    return 1;
  } catch (const Error &e) {
    show_message(e.to_string());
    return 1;
  } catch (const std::exception &e) {
    show_message("shit: " + String{util_name} + ": " + e.what());
    return 1;
  } catch (...) {
    show_message("shit: " + String{util_name} + ": unexpected error");
    return 1;
  }
}

fn parse_util_operands(const ArrayList<Flag *> &flags,
                       const ArrayList<String> &args) throws
    -> ArrayList<String>
{
  ArrayList<String> operands = parse_flags_vec(flags, args, 0);
  /* The first operand is the utility name, dropped to leave the real arguments. */
  if (!operands.is_empty()) operands.remove(0);
  return operands;
}

fn print_util_help(const ExecContext &ec, StringView name, StringView synopsis,
                   StringView description,
                   const ArrayList<Flag *> &flags) throws -> void
{
  let help_text = String{heap_allocator()};

  if (ec.is_multicall) {
    help_text += "\n";
    help_text += wrap_text("This utility is bundled with the shit shell and "
                           "runs from the shit binary reached through a "
                           "symlink, not a system program of the same name.",
                           HELP_INDENT, HELP_WRAP_WIDTH);
    help_text += "\n\n";
  }

  if (!description.is_empty()) {
    help_text += "DESCRIPTION\n";
    help_text += wrap_text(description, HELP_INDENT, HELP_WRAP_WIDTH);
    help_text += "\n\n";
  }
  ArrayList<StringView> synopsis_lines{heap_allocator()};
  synopsis_lines.push(synopsis);
  help_text += make_synopsis(name, synopsis_lines);
  help_text += '\n';
  help_text += make_flag_help(flags);
  help_text += '\n';

  ec.print_to_stdout(help_text);
}

fn read_fd_to_string(os::descriptor fd) throws -> String
{
  String contents{heap_allocator()};
  char buffer[4096];
  loop
  {
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
  ArrayList<StringView> lines{heap_allocator()};
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
  items.sort([](const String &a, const String &b) { return a < b; });
}

fn sort_stringview_list(ArrayList<StringView> &items) wontthrow -> void
{
  items.sort([](StringView a, StringView b) {
    let const min_length = a.length < b.length ? a.length : b.length;
    for (usize i = 0; i < min_length; i++)
      if (a[i] != b[i])
        return static_cast<unsigned char>(a[i]) <
               static_cast<unsigned char>(b[i]);
    return a.length < b.length;
  });
}

fn format_signal_list() throws -> String
{
  let out = String{heap_allocator()};
  for (let const name : os::signal_names()) {
    if (let const number = os::signal_number_from_name(name);
        number.has_value())
    {
      out += String::from(*number, heap_allocator());
      out += ") SIG";
      out += name;
      out += '\n';
    }
  }
  return out;
}

fn format_human_size(u64 bytes) throws -> String
{
  if (bytes < 1024) return String::from(bytes, heap_allocator());

  static const char units[] = {'K', 'M', 'G', 'T', 'P'};
  double value = static_cast<double>(bytes);
  usize unit = 0;
  /* The condition reads unit, so the last unit P stays reachable. */
  while (value >= 1024.0 && unit < sizeof(units)) {
    value /= 1024.0;
    unit++;
  }

  /* A value that rounds up to 1024 crosses over to the next unit. */
  if (value >= 1023.5 && unit < sizeof(units)) {
    value /= 1024.0;
    unit++;
  }

  String out{heap_allocator()};
  /* A scaled value below ten keeps one decimal, otherwise it rounds whole. */
  let const tenths = static_cast<u64>(value * 10.0 + 0.5);
  if (value < 10.0 && tenths < 100) {
    out += String::from(tenths / 10, heap_allocator());
    out += '.';
    out += String::from(tenths % 10, heap_allocator());
  } else {
    out += String::from(static_cast<u64>(value + 0.5), heap_allocator());
  }
  out.push(units[unit - 1]);
  return out;
}

fn report_soft_shitbox_error(const ExecContext &ec, EvalContext &cxt,
                             StringView message) throws -> void
{
  /* The fallback line covers the rare case with no source to caret against. */
  const ErrorWithLocation located{ec.source_location(), message};
  if (const String *source = cxt.current_source(); source != nullptr)
    show_message(located.to_string(source->view()));
  else
    print_error("shit: " + String{message} + "\n");
}

} // namespace shitbox

} // namespace shit
