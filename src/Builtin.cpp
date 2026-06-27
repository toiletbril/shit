#include "Builtin.hpp"

#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

cold fn show_builtin_help_impl(const ExecContext &ec, StringView description,
                               const ArrayList<StringView> &synopsis_lines,
                               const ArrayList<Flag *> &flags,
                               StringView extra_sections) throws -> void
{
  ASSERT(!ec.args().is_empty());

  let help_text = String{};
  if (!description.is_empty()) {
    help_text += "DESCRIPTION\n";
    help_text += wrap_text(description, HELP_INDENT, HELP_WRAP_WIDTH);
    help_text += "\n\n";
  }
  help_text += make_synopsis(ec.args()[0].view(), synopsis_lines);
  help_text += '\n';
  help_text += make_flag_help(flags);
  help_text += '\n';
  /* The per-builtin generated text, the OPTION SWITCHES table of set and the
     OPTION NAMES list of shopt, lands after the flag sections. */
  if (!extra_sections.is_empty()) {
    help_text += extra_sections;
    help_text += '\n';
  }
  ec.print_to_stdout(help_text);
}

flatten fn search_builtin(StringView builtin_name) throws
    -> Maybe<Builtin::Kind>
{
  return BUILTINS.find(builtin_name);
}

/* The per-kind flag lists, a zero-initialized table immune to static-init
   order, filled by each builtin file's registrar after its FLAG_LIST is
   built, since both sit in the same translation unit in order. */
static const ArrayList<Flag *> *BUILTIN_FLAG_LISTS[BUILTIN_KIND_COUNT] = {};

fn register_builtin_flag_list(Builtin::Kind kind,
                              const ArrayList<Flag *> *flags) wontthrow -> void
{
  BUILTIN_FLAG_LISTS[static_cast<usize>(kind)] = flags;
}

fn builtin_flag_list(Builtin::Kind kind) wontthrow -> const ArrayList<Flag *> *
{
  return BUILTIN_FLAG_LISTS[static_cast<usize>(kind)];
}

fn is_special_builtin_name(StringView name) wontthrow -> bool
{
  /* The POSIX special builtin set, matched by name. The colon and the dot are
     special while their plain-word siblings true and source-as-a-program are
     not, so the kind cannot decide this. */
  static constexpr StaticStringMap<bool>::entry SPECIAL_BUILTIN_ENTRIES[] = {
      {SSK(":"),        true},
      {SSK("."),        true},
      {SSK("break"),    true},
      {SSK("continue"), true},
      {SSK("eval"),     true},
      {SSK("exec"),     true},
      {SSK("exit"),     true},
      {SSK("export"),   true},
      {SSK("readonly"), true},
      {SSK("return"),   true},
      {SSK("set"),      true},
      {SSK("shift"),    true},
      {SSK("times"),    true},
      {SSK("trap"),     true},
      {SSK("unset"),    true},
  };
  static constexpr StaticStringMap<bool> SPECIAL_BUILTINS{
      SPECIAL_BUILTIN_ENTRIES, countof(SPECIAL_BUILTIN_ENTRIES)};
  return SPECIAL_BUILTINS.find(name).has_value();
}

fn builtin_names() throws -> const ArrayList<String> &
{
  static ArrayList<String> names = [] throws {
    let collected = ArrayList<String>{};
    for (const StaticStringMap<Builtin::Kind>::entry &entry : BUILTIN_ENTRIES)
      collected.push(entry.key.to_string());
    return collected;
  }();
  return names;
}

fn execute_builtin(ExecContext &&ec, EvalContext &cxt) throws -> i32
{
  ASSERT(!ec.args().is_empty());

  /* A builtin runs inside the shell process, so it keeps the shell's own signal
     handlers. Resetting them to the default here would let a Ctrl-C during a
     builtin terminate the whole shell, and would cost two extra syscalls on
     every builtin command. */
  defer { ec.close_fds(); };

  /* A builtin stage of a pipeline carries the pipe ends in its context. A
     builtin that runs a sub-command, such as eval, command, or the dot source,
     evaluates a fresh command that builds its own context from the shell's real
     descriptors and never sees these pipe ends. The pipe descriptors are placed
     on the real shell fd 0, 1, and 2 for the duration of the builtin so any
     sub-command it spawns inherits them, and the originals are restored after.
     A single builtin that is not a pipeline stage carries no pipe fds, so it
     pays for none of this. */
  const bool has_pipe_descriptors =
      ec.in_fd.has_value() || ec.out_fd.has_value() || ec.err_fd.has_value();
  /* A bare 2>&1 or 1>&2 on a builtin carries no file descriptor, only a routing
     flag, so the placement runs whenever either a descriptor or a cross-route
     is present. Otherwise `cd /bad 2>&1` would leave the builtin's stderr on
     the terminal instead of following the standard output. */
  const bool has_dup_routing = ec.dup_err_to_out || ec.dup_out_to_err;

  let saved_descriptors = ArrayList<os::saved_descriptor>{};
  if (has_pipe_descriptors || has_dup_routing) {
    if (ec.in_fd)
      saved_descriptors.push(os::save_and_replace_descriptor(0, *ec.in_fd));
    if (ec.out_fd)
      saved_descriptors.push(os::save_and_replace_descriptor(1, *ec.out_fd));
    if (ec.err_fd)
      saved_descriptors.push(os::save_and_replace_descriptor(2, *ec.err_fd));
    /* The cross-route runs after the files are placed on 0, 1, and 2, so 2>&1
       copies the descriptor the standard output now points at. The saves push
       onto the same stack after the file saves, so the restore unwinds the
       route before the files, the reverse of the apply order. The same
       came-last ordering the spawn paths use decides a mixed 2>&1 1>&2. */
    ec.apply_dup_routing(
        [&]() {
          saved_descriptors.push(os::save_and_replace_descriptor(
              2, os::descriptor_for_shell_fd(1)));
        },
        [&]() {
          saved_descriptors.push(os::save_and_replace_descriptor(
              1, os::descriptor_for_shell_fd(2)));
        });
  }
  defer
  {
    for (usize i = saved_descriptors.count(); i > 0; i--)
      os::restore_descriptor(saved_descriptors[i - 1]);
  };

  /* Each builtin is a stateless dispatch object, so its case constructs it on
     the stack and runs it, which avoids a heap allocation on every builtin
     command. */
  LOG(Debug, "dispatching builtin '%s' with %zu arguments",
      ec.program().c_str(), ec.args().count());
  try {
    switch (ec.builtin_kind()) {
      BUILTIN_SWITCH_CASES();
    default:
      unreachable("Unhandled builtin of kind %d", ENUM(ec.builtin_kind()));
    }
  } catch (const Error &e) {
    /* The bash-compatible mood reports a builtin error the way bash does, a
       soft failure printed to the command's stderr, which a 2>... on the
       command still redirects since fd 2 is replaced above this try, and a
       non-zero status so the surrounding list keeps running rather than
       aborting. The default and posix moods keep the located throw that stops
       the run up front. */
    if (cxt.is_bash_compatible()) {
      print_error(StringView{"shit: "} + ec.program() + ": " + e.message() +
                  "\n");
      return 1;
    }
    throw ErrorWithLocation{ec.source_location(), StringView{"Builtin '"} +
                                                      ec.program() +
                                                      "': " + e.message()};
  }
  unreachable("execute_builtin reached the end without dispatching");
}

fn report_soft_builtin_error(const ExecContext &ec, EvalContext &cxt,
                             StringView message) throws -> void
{
  /* The bash mood prints the same soft unlocated line the dispatch gives a
     thrown error, while the default and posix moods render the located caret in
     place rather than throwing, so the loop that called this keeps processing
     the rest of its names. */
  if (cxt.is_bash_compatible()) {
    print_error(StringView{"shit: "} + ec.program() + ": " + message + "\n");
    return;
  }
  const ErrorWithLocation located{ec.source_location(),
                                  StringView{"Builtin '"} + ec.program() +
                                      "': " + message};
  if (const String *source = cxt.current_source(); source != nullptr)
    show_message(located.to_string(source->view()));
  else
    print_error(StringView{"shit: Builtin '"} + ec.program() + "': " + message +
                "\n");
}

fn report_usage_error(const ExecContext &ec, EvalContext &cxt,
                      StringView program_name) throws -> i32
{
  /* A missing required argument reads the same located caret in every mood, the
     shit feature form, rather than the soft unlocated line the bash mood gives
     a thrown builtin error. The trailing note points the reader at the
     per-command help the way a compiler points past an error at a hint. The
     fallback line is for the rare case with no source to caret against, such as
     the multicall entry. */
  const ErrorWithLocation located{
      ec.source_location(), String{program_name} + ": Not enough arguments"};
  if (const String *source = cxt.current_source(); source != nullptr)
    show_message(located.to_string(source->view()));
  else
    print_error(String{"shit: "} + program_name + ": Not enough arguments.\n");
  show_message(Note{String{"Try `"} + program_name + " --help` for more info"}
                   .to_string());
  return 2;
}

fn quote_for_declare(StringView value) throws -> String
{
  let quoted = String{};
  for (usize i = 0; i < value.length; i++) {
    const char c = value[i];
    if (c == '"' || c == '\\' || c == '$' || c == '`') quoted += '\\';
    quoted += c;
  }
  return quoted;
}

fn parse_optional_integer_arg(const ExecContext &ec, i64 default_value) throws
    -> i64
{
  if (ec.args().count() <= 1) return default_value;
  let const parsed_value = utils::parse_decimal_integer(ec.args()[1]);
  if (parsed_value.is_error()) throw parsed_value.error();
  return parsed_value.value();
}

Builtin::Builtin() = default;

} /* namespace shit */
