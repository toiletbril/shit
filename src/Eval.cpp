#include "Eval.hpp"

#include "Arena.hpp"
#include "Cli.hpp"
#include "Colors.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "ResolvedCommand.hpp"
#include "Shitbox.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <exception>

/* POSIX regcomp and regexec back the [[ =~ operator, the release build drops
   libstdc++ so std::regex is unavailable. */
#if SHIT_PLATFORM_IS POSIX
#include <regex.h>
#endif

/* _get_osfhandle maps a shell fd number to its Windows handle for the -t test.
 */
#if SHIT_PLATFORM_IS WIN32
#include <io.h>
#endif

namespace shit {

EvalContext::EvalContext(bool should_disable_path_expansion, bool should_echo,
                         bool should_echo_expanded, bool shell_is_interactive,
                         bool should_error_exit, String shell_name,
                         ArrayList<String> positional_params)
    : m_shell_name(steal(shell_name)),
      m_positional_params(steal(positional_params)),
      m_enable_path_expansion(!should_disable_path_expansion),
      m_enable_echo(should_echo), m_enable_echo_expanded(should_echo_expanded),
      m_shell_is_interactive(shell_is_interactive),
      m_error_exit(should_error_exit)
{
  set_field_separators(m_field_separators.view());

  /* The $RANDOM seed is deferred to its first read, so a run that never reads
     it pays neither seed nor syscall. */
  m_shell_start_time = static_cast<i64>(std::time(nullptr));

  for (let const &name : os::environment_names())
    m_exported_names.add(name.view());
}

fn RuntimeState::capture(const EvalContext &context) wontthrow -> RuntimeState
{
  return context.m_runtime;
}

fn RuntimeState::restore(EvalContext &context) const wontthrow -> void
{
  context.m_runtime = *this;
}

fn EvalContext::add_evaluated_expression() wontthrow -> void
{
  if (!m_stats_enabled) return;
  m_expressions_executed_last++;
}

fn EvalContext::add_expansion() wontthrow -> void { m_expansions_last++; }

fn EvalContext::end_command() wontthrow -> void
{
  m_expansions_total += m_expansions_last;
  m_expressions_executed_total += m_expressions_executed_last;
  m_commands_evaluated++;

  if (AST_ARENA != nullptr) {
    const usize used = AST_ARENA->bytes_used();
    if (used > m_peak_ast_arena_bytes) m_peak_ast_arena_bytes = used;
  }

  m_expansions_last = m_expressions_executed_last = 0;
}

hot fn EvalContext::assign_variable(StringView name, StringView value) throws
    -> void
{
  LOG(All, "assigning variable '%.*s' to a value of %zu bytes",
      static_cast<int>(name.length), name.data, value.length);
  if (name == "IFS") set_field_separators(value);
  if (name == "PATH") utils::set_path_for_resolution(String{value});
  m_shell_variables.set(name, value);
  /* A child of `export FOO=1; FOO=2` must see 2. */
  if (m_exported_names.contains(name)) {
    if (m_subshell_depth > 0)
      m_environment_undo_log.push(environment_undo_entry{
          String{name}, os::get_environment_variable(name)});
    os::set_environment_variable(name, value);
  }
}

fn EvalContext::set_field_separators(StringView value) throws -> void
{
  LOG(Debug, "caching %zu field separator bytes", value.length);
  /* The table is built before m_field_separators is touched, since value may
     alias the buffer the assignment below rewrites. */
  for (usize i = 0; i < 256; i++)
    m_field_separator_table[i] = false;
  for (usize i = 0; i < value.length; i++)
    m_field_separator_table[static_cast<u8>(value.data[i])] = true;
  if (value.data != m_field_separators.data()) {
    m_field_separators.clear();
    m_field_separators.append(value);
  }
}

hot pure fn EvalContext::is_field_separator(char c) const wontthrow -> bool
{
  return m_field_separator_table[static_cast<u8>(c)];
}

hot fn EvalContext::set_shell_variable(StringView name, StringView value) throws
    -> void
{
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  /* An integer-marked name evaluates its value as arithmetic, an empty value
     storing zero. */
  if (is_integer_variable(name)) [[unlikely]] {
    let const result = value.length == 0 ? 0 : evaluate_arithmetic(value);
    char result_text[24];
    assign_variable(name, utils::int_to_text_into(result, result_text,
                                                  sizeof(result_text)));
    return;
  }

  assign_variable(name, value);
}

fn EvalContext::seed_shell_identity_variables(bool is_bash_identity) throws
    -> void
{
  if (is_bash_identity) {
    LOG(Info, "seeding the bash identity variables");
    set_shell_variable("BASH_VERSION", "5.2.0(1)-shit");
    let versinfo = ArrayList<String>{heap_allocator()};
    versinfo.push(String{"5"});
    versinfo.push(String{"2"});
    versinfo.push(String{"0"});
    versinfo.push(String{"1"});
    versinfo.push(String{"release"});
    versinfo.push(String{SHIT_OS_INFO});
    set_indexed_array("BASH_VERSINFO", steal(versinfo));
    set_shell_variable("BASH", m_shell_executable_path.view());
    /* bash-completion's word reassembly walks COMP_LINE against
       COMP_WORDBREAKS, a missing value collapses every word into one and kills
       completion. */
    if (!get_variable_value("COMP_WORDBREAKS").has_value())
      set_shell_variable("COMP_WORDBREAKS", StringView{" \t\n\"'><=;|&(:"});
    return;
  }
  LOG(Info, "clearing the bash identity variables for a non-bash mood");
  force_unset_shell_variable("BASH_VERSION");
  force_unset_shell_variable("BASH");
}

fn EvalContext::unset_shell_variable(StringView name) throws -> void
{
  if (is_readonly(name))
    throw Error{"Unable to unset '" + name + "' because it is read only"};

  /* A local a caller declared peels back to that caller's saved value, the bash
     upvar semantics. */
  if (peel_caller_local_binding(name)) return;

  force_unset_shell_variable(name);
  m_indexed_arrays.erase(name);
  clear_sparse_array(name);
  m_integer_names.remove(name);
}

fn EvalContext::peel_caller_local_binding(StringView name) throws -> bool
{
  if (m_local_scopes.count() < 2) return false;
  if (is_local_in_current_scope(name)) return false;

  /* The innermost caller frame holding a saved binding is the one bash's unset
     peels, so the scan walks from the nearest caller outward. */
  for (usize frame_index = m_local_scopes.count() - 1; frame_index-- > 0;) {
    ArrayList<local_binding> &frame = m_local_scopes[frame_index];
    for (usize i = frame.count(); i-- > 0;) {
      let &binding = frame[i];
      if (binding.name.view() != name) continue;
      LOG(Debug, "peeling the local binding of '%.*s' from caller frame %zu",
          static_cast<int>(name.length), name.data, frame_index);

      restore_local_binding(binding);

      /* Removing the consumed restore entry lets a value a deeper callee
         assigns survive into the caller. */
      frame.remove(i);
      return true;
    }
  }
  return false;
}

fn EvalContext::restore_local_binding(local_binding &binding) throws -> void
{
  /* Restore through assign_variable, not set_shell_variable, since the scope
     pop runs this inside a noexcept defer where a readonly name would throw
     from a destructor and terminate the shell. The stores below skip the
     readonly check for the same reason. */
  if (binding.previous_value.has_value())
    assign_variable(binding.name, *binding.previous_value);
  else
    force_unset_shell_variable(binding.name);
  /* The dense run is restored first so the sparse re-insert below routes each
     saved gap index beyond it. */
  if (binding.previous_indexed_array.has_value())
    m_indexed_arrays.set(binding.name.view(),
                         steal(*binding.previous_indexed_array));
  else
    m_indexed_arrays.erase(binding.name.view());
  clear_sparse_array(binding.name.view());
  for (usize i = 0; i < binding.previous_sparse_indices.count(); i++)
    set_array_element(binding.name.view(), binding.previous_sparse_indices[i],
                      binding.previous_sparse_values[i].view());
  clear_associative_array(binding.name.view());
  if (binding.previous_was_associative)
    for (usize k = 0; k < binding.previous_associative_keys.count(); k++)
      set_associative_element(binding.name.view(),
                              binding.previous_associative_keys[k].view(),
                              binding.previous_associative_values[k].view());
  if (binding.previous_was_integer)
    m_integer_names.add(binding.name.view());
  else
    m_integer_names.remove(binding.name.view());
  if (binding.previous_was_readonly)
    m_readonly_names.add(binding.name.view());
  else
    m_readonly_names.remove(binding.name.view());

  if (binding.previous_was_exported) {
    m_exported_names.add(binding.name.view());
    if (binding.previous_value.has_value())
      os::set_environment_variable(binding.name, *binding.previous_value);
  } else if (is_exported(binding.name)) {
    m_exported_names.remove(binding.name.view());
    os::unset_environment_variable(binding.name);
  }
}

fn EvalContext::set_indexed_array(StringView name,
                                  ArrayList<String> values) throws -> void
{
  LOG(All, "storing indexed array '%.*s' with %zu elements",
      static_cast<int>(name.length), name.data, values.count());
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};
  /* The scalar entry is dropped so a $name read falls through to element zero
     the way bash treats $a as ${a[0]}. */
  m_shell_variables.erase(name);
  clear_sparse_array(name);
  m_indexed_arrays.set(name, steal(values));
}

fn EvalContext::publish_single_pipe_status(i32 status) throws -> void
{
  let single = ArrayList<String>{heap_allocator()};
  single.push(String::from(status, heap_allocator()));
  set_indexed_array("PIPESTATUS", steal(single));
}

fn EvalContext::append_indexed_array(StringView name,
                                     ArrayList<String> values) throws -> void
{
  if (let *existing = m_indexed_arrays.find(name)) {
    LOG(All, "appending %zu elements to the existing array '%.*s'",
        values.count(), static_cast<int>(name.length), name.data);
    if (is_readonly(name))
      throw Error{"Unable to assign '" + name + "' because it is read only"};
    m_shell_variables.erase(name);
    for (let &element : values)
      existing->push(steal(element));
    return;
  }
  set_indexed_array(name, steal(values));
}

/* The set -u read and the ${name:?} report abort the whole run, unlike the
   command-level errors the bash mood continues past, so their throws carry the
   script-fatal mark. */
[[noreturn]] fn throw_script_fatal(String message, StringView note) throws
    -> void
{
  let error = Error{message.view()};
  error.set_script_fatal();
  if (!note.is_empty()) error.set_note(note);
  throw error;
}

cold fn EvalContext::show_runtime_warning(StringView message) wontthrow -> void
{
  show_runtime_warning_at(m_current_location, message);
}

cold fn EvalContext::show_runtime_warning_at(SourceLocation location,
                                             StringView message,
                                             StringView note) wontthrow -> void
{
  if (diagnostics_disabled()) return;
  /* A windowed resolution rebases the position onto the definition copy and
     swaps in its owned filename, since the stamped view may outlive its buffer
     once the defining command's sources are freed. */
  try {
    let const resolved_source = resolve_render_source(location);
    usize line_offset = 0;
    if (resolved_source.is_windowed) {
      location.position = location.position -
                          resolved_source.body_start_position +
                          resolved_source.header_length;
      location.filename = resolved_source.filename.is_empty()
                              ? Maybe<StringView>{}
                              : Maybe<StringView>{resolved_source.filename};
      line_offset = resolved_source.line_offset;
    }
    if (resolved_source.text == nullptr ||
        location.position > resolved_source.text->count())
    {
      let fallback = Warning{message};
      if (!note.is_empty()) fallback.set_note(note);
      show_message(fallback.to_string());
      return;
    }
    let warning = WarningWithLocation{location, message};
    warning.set_line_offset(line_offset);
    if (!note.is_empty()) warning.set_note(note);
    show_message(warning.to_string(resolved_source.text->view()));
    if (!m_source_frames.is_empty()) print_source_backtrace();
  } catch (...) {
    LOG(Debug, "formatting a runtime warning failed, the error is swallowed");
  }
}

pure fn EvalContext::locate_variable_reference(StringView name) const wontthrow
    -> SourceLocation
{
  let const fallback = m_current_location;
  if (name.is_empty()) return fallback;
  let const resolved_source = resolve_render_source(fallback);
  if (resolved_source.text == nullptr) return fallback;
  let const source = resolved_source.text->view();

  /* The scan rebases into the windowed copy and the found location converts
     back, so every stored location stays absolute. */
  usize scan_start = fallback.position;
  usize absolute_shift = 0;
  if (resolved_source.is_windowed) {
    scan_start = fallback.position - resolved_source.body_start_position +
                 resolved_source.header_length;
    absolute_shift =
        resolved_source.body_start_position > resolved_source.header_length
            ? resolved_source.body_start_position -
                  resolved_source.header_length
            : 0;
  }
  if (scan_start >= source.length) return fallback;

  /* The first $name or ${name in the logical line takes the caret, the byte
     after the name must end it so $FOO does not match $FOOBAR. */
  usize i = scan_start;
  while (i < source.length) {
    const char byte = source[i];
    if (byte == '\n' && (i == 0 || source[i - 1] != '\\')) {
      break;
    }
    if (byte != '$' || i + 1 >= source.length) {
      i++;
      continue;
    }
    usize name_start = i + 1;
    const bool is_braced = source[name_start] == '{';
    if (is_braced) name_start++;
    if (name_start + name.length <= source.length &&
        source.substring_of_length(name_start, name.length) == name &&
        (name_start + name.length == source.length ||
         !lexer::is_variable_name(source[name_start + name.length])))
    {
      /* A closing brace right after the name joins the caret so ${PAGER}
         underlines whole. */
      usize reference_end = name_start + name.length;
      if (is_braced && reference_end < source.length &&
          source[reference_end] == '}')
      {
        reference_end++;
      }
      return SourceLocation{i + absolute_shift, reference_end - i,
                            fallback.filename};
    }
    i++;
  }

  /* Arithmetic reads a variable as a bare name, so a second pass takes the
     first name-delimited spelling inside the same span. */
  usize k = scan_start;
  while (k + name.length <= source.length) {
    const char byte = source[k];
    if (byte == '\n' && (k == 0 || source[k - 1] != '\\')) {
      break;
    }
    if (source.substring_of_length(k, name.length) == name &&
        (k == 0 || !lexer::is_variable_name(source[k - 1])) &&
        (k + name.length == source.length ||
         !lexer::is_variable_name(source[k + name.length])))
    {
      return SourceLocation{k + absolute_shift, name.length, fallback.filename};
    }
    k++;
  }
  return fallback;
}

fn EvalContext::report_unset_reference(StringView name) throws -> void
{
  /* bash does not nounset on the operand of [[ -v name ]]. */
  if (is_warning_suppressed(suppressible_warning::UnsetReference)) return;

  /* -W downgrades the mood-seeded fatality to a warning, an explicit set -u
     keeps the abort. */
  let const empty_expansion_note =
      "Replace it with ${" + String{name} + "-} if empty expansion is desired";

  if (m_runtime.error_unset &&
      (m_runtime.error_unset_explicit || !warnings_enabled()))
  {
    throw_script_fatal("Unable to expand '" + String{name} +
                           "' because the parameter is not set",
                       empty_expansion_note.view());
  }
  /* A test or [ operand probes emptiness, so the advisory warning is silenced
     there, while the fatal set -u abort above still fires. */
  if (is_warning_suppressed(suppressible_warning::UnsetTestOperand)) return;

  if (m_runtime.error_unset ||
      (warnings_enabled() && warnings_reach_every_mood()))
  {
    show_runtime_warning_at(locate_variable_reference(name),
                            "The variable '" + String{name} +
                                "' is not set, it expands to empty",
                            empty_expansion_note.view());
  }
}

fn EvalContext::warn_or_throw(bool fatal, bool explicitly_requested,
                              SourceLocation location, StringView message,
                              StringView note) throws -> void
{
  if (fatal && (explicitly_requested || !warnings_enabled())) {
    let error = ErrorWithLocation{location, message};
    if (!note.is_empty()) error.set_note(note);
    throw error;
  }
  if ((fatal || (warnings_enabled() && warnings_reach_every_mood())) &&
      !diagnostics_disabled() && m_current_source != nullptr)
  {
    try {
      let warning = WarningWithLocation{location, message};
      if (!note.is_empty()) warning.set_note(note);
      show_message(warning.to_string(m_current_source->view()));
    } catch (...) {
      LOG(Debug, "showing a located warning failed, the error is swallowed");
    }
  }
}

fn EvalContext::force_unset_shell_variable(StringView name) throws -> void
{
  LOG(All, "removing variable '%.*s' from the store and the environment",
      static_cast<int>(name.length), name.data);
  m_shell_variables.erase(name);
  /* An exported name is removed from the environment too, otherwise a later
     lookup falls back to the stale value. */
  record_environment_change(name);
  os::unset_environment_variable(name);
  unmark_exported(name);
  if (name == "IFS") set_field_separators(" \t\n");
  if (name == "PATH")
    utils::set_path_for_resolution(os::get_environment_variable("PATH"));
}

fn EvalContext::record_environment_change(StringView name) throws -> void
{
  if (m_subshell_depth == 0) return;
  m_environment_undo_log.push(
      environment_undo_entry{String{name}, os::get_environment_variable(name)});
}

fn EvalContext::mark_exported(StringView name) throws -> void
{
  LOG(All, "marking '%.*s' as exported", static_cast<int>(name.length),
      name.data);
  m_exported_names.add(name);
}

fn EvalContext::unmark_exported(StringView name) throws -> void
{
  m_exported_names.remove(name);
}

pure fn EvalContext::is_exported(StringView name) const wontthrow -> bool
{
  return m_exported_names.contains(name);
}

fn EvalContext::sync_exported_after_restore(StringView name,
                                            bool has_value) throws -> void
{
  if (has_value)
    m_exported_names.add(name);
  else
    m_exported_names.remove(name);
}

struct ansi_color_variable
{
  const char *name;
  const char *escape;
};

static constexpr ansi_color_variable SHIT_ANSI_COLORS[] = {
    {"SHIT_ANSI_BLACK",          "\x1b[30m"},
    {"SHIT_ANSI_RED",            "\x1b[31m"},
    {"SHIT_ANSI_GREEN",          "\x1b[32m"},
    {"SHIT_ANSI_YELLOW",         "\x1b[33m"},
    {"SHIT_ANSI_BLUE",           "\x1b[34m"},
    {"SHIT_ANSI_MAGENTA",        "\x1b[35m"},
    {"SHIT_ANSI_CYAN",           "\x1b[36m"},
    {"SHIT_ANSI_WHITE",          "\x1b[37m"},
    {"SHIT_ANSI_BRIGHT_BLACK",   "\x1b[90m"},
    {"SHIT_ANSI_BRIGHT_RED",     "\x1b[91m"},
    {"SHIT_ANSI_BRIGHT_GREEN",   "\x1b[92m"},
    {"SHIT_ANSI_BRIGHT_YELLOW",  "\x1b[93m"},
    {"SHIT_ANSI_BRIGHT_BLUE",    "\x1b[94m"},
    {"SHIT_ANSI_BRIGHT_MAGENTA", "\x1b[95m"},
    {"SHIT_ANSI_BRIGHT_CYAN",    "\x1b[96m"},
    {"SHIT_ANSI_BRIGHT_WHITE",   "\x1b[97m"},
    {"SHIT_ANSI_BOLD",           "\x1b[1m" },
    {"SHIT_ANSI_DIM",            "\x1b[2m" },
    {"SHIT_ANSI_RESET",          "\x1b[0m" },
};

static fn ansi_escape_for_color(StringView name) throws -> Maybe<StringView>
{
  for (let const &color : SHIT_ANSI_COLORS)
    if (StringView{color.name} == name) return StringView{color.escape};
  return None;
}

hot fn EvalContext::get_variable_value(StringView name) const throws
    -> Maybe<String>
{
  /* The specials, the positional digit run, and $LINENO split out below so the
     common name read pays only the first-byte test. */
  const char first_byte = name.is_empty() ? '\0' : name[0];

  if (name.count() == 1) {
    switch (first_byte) {
    case '?': return String::from(m_last_exit_status, heap_allocator());
    case '$': return String::from(os::get_shell_process_id(), heap_allocator());
    case '!':
      return m_last_background_pid
                 ? String::from(*m_last_background_pid, heap_allocator())
                 : String{heap_allocator()};
    case '-': return option_flags_string();
    case '#':
      return String{heap_allocator(),
                    String::from(m_positional_params.count(), heap_allocator())};
    case '0': return String{heap_allocator(), m_shell_name};
    case '_': return String{heap_allocator(), m_last_argument.view()};

    case '*':
    case '@': {
      let separator = ' ';
      let has_separator = true;
      if (first_byte == '*') {
        let const &ifs = m_field_separators;
        has_separator = !ifs.is_empty();
        if (has_separator) separator = ifs.first_character();
      }
      let joined = String{heap_allocator()};
      usize joined_length = 0;
      for (usize i = 0; i < m_positional_params.count(); i++)
        joined_length += m_positional_params[i].count();
      if (has_separator && m_positional_params.count() > 1) {
        joined_length += m_positional_params.count() - 1;
      }
      joined.reserve(joined_length);
      for (usize i = 0; i < m_positional_params.count(); i++) {
        if (i > 0 && has_separator) joined.push(separator);
        joined.append(m_positional_params[i].view());
      }
      return joined;
    }

    default: break;
    }
  }

  if (first_byte >= '0' && first_byte <= '9') {
    if (name.is_all_decimal_digits()) {
      /* A positional beyond the count is unset rather than empty, so the strict
         unset report fires and ${1-default} takes its default. */
      if (name.count() > 9) return None;
      let const parsed_index = name.to<i64>();
      if (parsed_index.is_error()) return None;
      let const index = static_cast<usize>(parsed_index.value());
      if (index >= 1 && index <= m_positional_params.count()) {
        ASSERT(index - 1 < m_positional_params.count());
        return m_positional_params[index - 1];
      }
      return None;
    }
  }

  if (let const *stored = m_shell_variables.find(name)) return *stored;

  /* A read of an array name with no scalar yields element zero, the way bash
     treats $a as ${a[0]}. The empty-map guard skips the second hash for a shell
     with no arrays. */
  if (m_indexed_arrays.count() != 0)
    if (let const *array = m_indexed_arrays.find(name)) {
      if (array->is_empty()) return shit::None;
      return array->front();
    }

  /* IFS is held live in m_field_separators. The store lookup above wins, so
     IFS= empty reads back empty while the unset default reads back
     space-tab-newline, making the IFS save/restore idiom round-trip. */
  if (first_byte == 'I' && name == "IFS") {
    return String{heap_allocator(), m_field_separators.view()};
  }

  if (first_byte == 'S' && name == "SHIT_GIT_BRANCH") {
    return utils::current_git_branch();
  }

  if (first_byte == 'S' && name.starts_with("SHIT_ANSI_")) {
    if (let const escape = ansi_escape_for_color(name)) {
      if (!colors::stdout_wants_color()) return String{heap_allocator()};
      return String{heap_allocator(), *escape};
    }
  }

  /* $LINENO reports the line of the command currently evaluating, a stored
     value above winning so a script that assigns LINENO reads it back. */
  if (first_byte == 'L' && name == "LINENO") {
    return String::from(line_number_at_location(m_current_location),
                        heap_allocator());
  }

  /* The bash dynamic variables are computed on each read in every mood but
     POSIX. A stored assignment above wins, so RANDOM=5 reads back 5. */
  if (bash_dynamic_variables_enabled()) {
    if (first_byte == 'R' && name == "RANDOM") {
      if (!m_random_seeded) {
        std::srand(static_cast<unsigned>(m_shell_start_time) ^
                   static_cast<unsigned>(os::get_shell_process_id()));
        m_random_seeded = true;
      }
      return String::from(static_cast<usize>(std::rand() % 32768),
                          heap_allocator());
    }
    if (first_byte == 'S' && name == "SECONDS") {
      return String::from(static_cast<i64>(std::time(nullptr)) -
                              m_shell_start_time,
                          heap_allocator());
    }
    /* SHELLOPTS is the colon list of enabled set -o options, which
       bash-completion greps for posix membership. */
    if (first_byte == 'S' && name == "SHELLOPTS") {
      struct shellopts_row
      {
        const char *option_name;
        bool (EvalContext::*get)() const;
      };
      static const shellopts_row SHELLOPTS_ROWS[] = {
          {"allexport", &EvalContext::export_all          },
          {"errexit",   &EvalContext::error_exit          },
          {"failglob",  &EvalContext::failglob            },
          {"monitor",   &EvalContext::monitor             },
          {"noclobber", &EvalContext::no_clobber          },
          {"noexec",    &EvalContext::no_exec             },
          {"noglob",    &EvalContext::no_glob             },
          {"nounset",   &EvalContext::error_unset         },
          {"pipefail",  &EvalContext::pipefail            },
          {"posix",     &EvalContext::is_posix_mode       },
          {"verbose",   &EvalContext::should_echo         },
          {"xtrace",    &EvalContext::should_echo_expanded},
      };
      let joined = String{heap_allocator()};
      for (let const &row : SHELLOPTS_ROWS) {
        if (!(this->*(row.get))()) continue;
        if (!joined.is_empty()) joined.push(':');
        joined.append(StringView{row.option_name});
      }
      return joined;
    }
    if (first_byte == 'E' && name == "EPOCHSECONDS") {
      return String::from(static_cast<i64>(std::time(nullptr)), heap_allocator());
    }
    /* The fraction is always six digits so a reader can slice it by a fixed
       width. */
    if (first_byte == 'E' && name == "EPOCHREALTIME") {
      const u64 microseconds = os::realtime_microseconds();
      char fraction[8];
      std::snprintf(fraction, sizeof(fraction), "%06llu",
                    static_cast<unsigned long long>(microseconds % 1000000ULL));
      return String::from(static_cast<i64>(microseconds / 1000000ULL),
                          heap_allocator()) +
             "." + StringView{fraction};
    }
    if (first_byte == 'B' && name == "BASHPID") {
      return String::from(os::get_shell_process_id(), heap_allocator());
    }
    if (first_byte == 'P' && name == "PPID") {
      return String::from(os::get_parent_process_id(), heap_allocator());
    }
    if (first_byte == 'U' && name == "UID") {
      return String::from(os::get_real_user_id(), heap_allocator());
    }
    if (first_byte == 'E' && name == "EUID") {
      return String::from(os::get_effective_user_id(), heap_allocator());
    }
    if (first_byte == 'H' && name == "HOSTNAME") {
      if (let host = os::get_hostname(); host.has_value()) return steal(*host);
      return String{heap_allocator()};
    }
    /* A monotonic millisecond count, immune to a wall-clock jump. */
    if (first_byte == 'B' && name == "BASH_MONOSECONDS") {
      return String::from(
          static_cast<i64>(os::monotonic_nanos() / 1000000ULL), heap_allocator());
    }
    if (first_byte == 'B' && name == "BASH_ARGV0") {
      return String{heap_allocator(), m_shell_name.view()};
    }
    /* BASH_EXECUTION_STRING is set only on the -c path. */
    if (first_byte == 'B' && name == "BASH_EXECUTION_STRING" &&
        !m_execution_string.is_empty())
    {
      return String{heap_allocator(), m_execution_string.view()};
    }
    /* The array form ${GROUPS[@]} is not modelled. */
    if (first_byte == 'G' && name == "GROUPS") {
      return String::from(os::get_real_group_id(), heap_allocator());
    }
    if (first_byte == 'H' && name == "HOSTTYPE") {
      return os::machine_type();
    }
    if (first_byte == 'M' && name == "MACHTYPE") {
      return os::machine_type() + "-unknown-linux-gnu";
    }
    /* SRANDOM mixes the clock with the rand source, so a run that never seeds
       RANDOM still varies. */
    if (first_byte == 'S' && name == "SRANDOM") {
      let const value = static_cast<u32>(os::realtime_microseconds()) ^
                        (static_cast<u32>(std::rand()) << 16) ^
                        static_cast<u32>(std::rand());
      return String::from(static_cast<i64>(value), heap_allocator());
    }
    if (first_byte == 'O' && name == "OSTYPE") {
      return String{heap_allocator(), os::ostype_name()};
    }
    if (first_byte == 'B' && name == "BASH_SUBSHELL") {
      return String{heap_allocator(),
                    String::from(static_cast<i64>(m_subshell_depth),
                                 heap_allocator())};
    }
    if (first_byte == 'F' && name == "FUNCNAME") {
      if (funcname_frame_count() > 0)
        return String{heap_allocator(), funcname_frame_at(0)};
      return shit::None;
    }
    if (first_byte == 'B' && name == "BASH_SOURCE") {
      if (!m_source_frames.is_empty())
        return String{
            heap_allocator(),
            m_source_frames[m_source_frames.count() - 1].source_path.view()};
      /* Inside a function BASH_SOURCE[0] is the file the function was defined
         in, so a sourced completion function resolves a helper beside its
         script. */
      if (funcname_frame_count() > 0) {
        let const *info =
            m_function_definition_infos.find(funcname_frame_at(0));
        if (info != nullptr && !info->filename.is_empty()) {
          return String{heap_allocator(), info->filename.view()};
        }
      }
      /* A script-file run reads BASH_SOURCE[0] as $0, so the envman style probe
         test "$BASH_SOURCE" == "$0" takes its executed branch. */
      if (m_is_script_run) return String{heap_allocator(), m_shell_name.view()};
      return String{heap_allocator()};
    }

    if (first_byte == 'B' && name == "BASH_LINENO") {
      if (funcname_frame_count() > 0)
        return String::from(funcname_line_at(0), heap_allocator());
      return shit::None;
    }

    if (first_byte == 'B' && name == "BASH_COMMAND" &&
        !m_current_command.is_empty())
    {
      return String{heap_allocator(), m_current_command.view()};
    }
  }

  if (let const env = os::get_environment_variable(name))
    return String{heap_allocator(), env->view()};
  return shit::None;
}

fn EvalContext::append_dynamic_variable_names(
    ArrayList<StringView> &out) const throws -> void
{
  out.push(StringView{"IFS"});
  out.push(StringView{"LINENO"});
  out.push(StringView{"SHIT_GIT_BRANCH"});

  for (let const &color : SHIT_ANSI_COLORS)
    out.push(StringView{color.name});

  if (!bash_dynamic_variables_enabled()) return;

  static constexpr const char *BASH_DYNAMIC_NAMES[] = {
      "RANDOM",
      "SECONDS",
      "SHELLOPTS",
      "EPOCHSECONDS",
      "EPOCHREALTIME",
      "BASHPID",
      "PPID",
      "UID",
      "EUID",
      "HOSTNAME",
      "BASH_MONOSECONDS",
      "BASH_ARGV0",
      "BASH_EXECUTION_STRING",
      "GROUPS",
      "HOSTTYPE",
      "MACHTYPE",
      "SRANDOM",
      "OSTYPE",
      "BASH_SUBSHELL",
      "FUNCNAME",
      "BASH_SOURCE",
      "BASH_LINENO",
      "BASH_COMMAND",
  };
  for (let const name : BASH_DYNAMIC_NAMES)
    out.push(StringView{name});
}

pure fn EvalContext::positional_params() const wontthrow
    -> const ArrayList<String> &
{
  return m_positional_params;
}

fn EvalContext::set_positional_params(ArrayList<String> params) wontthrow
    -> void
{
  m_positional_params = steal(params);
}

fn EvalContext::take_positional_params() wontthrow -> ArrayList<String>
{
  return steal(m_positional_params);
}

fn EvalContext::enter_function_scope() throws -> void
{
  m_local_scopes.push(ArrayList<local_binding>{heap_allocator()});
  LOG(Debug, "entered function scope, local scope depth now %zu",
      m_local_scopes.count());
}

fn EvalContext::leave_function_scope() throws -> void
{
  if (m_local_scopes.is_empty()) return;

  /* Restore each shadowed binding in reverse, so a name declared local twice
     ends with the value it held before the function ran. */
  ASSERT(!m_local_scopes.is_empty());
  let &scope = m_local_scopes.back();
  LOG(Debug, "leaving function scope, restoring %zu shadowed locals",
      scope.count());
  for (usize i = scope.count(); i > 0; i--) {
    ASSERT(i - 1 < scope.count());
    restore_local_binding(scope[i - 1]);
  }
  m_local_scopes.pop_back();
}

fn EvalContext::push_function_call_name(StringView name) throws -> void
{
  m_function_call_names.push(String{heap_allocator(), name});
  m_function_call_locations.push(m_current_location);
}

fn EvalContext::pop_function_call_name() wontthrow -> void
{
  if (!m_function_call_names.is_empty()) {
    m_function_call_names.remove(m_function_call_names.count() - 1);
    m_function_call_locations.remove(m_function_call_locations.count() - 1);
  }
}

fn EvalContext::funcname_frame_count() const wontthrow -> usize
{
  if (m_function_call_names.is_empty()) return 0;
  return m_function_call_names.count() + m_sourced_file_frames +
         (m_is_script_run ? 1 : 0);
}

fn EvalContext::funcname_frame_at(usize index) const wontthrow -> StringView
{
  let const call_count = m_function_call_names.count();
  if (index < call_count)
    return m_function_call_names[call_count - 1 - index].view();
  if (index < call_count + m_sourced_file_frames) return StringView{"source"};
  return StringView{"main"};
}

fn EvalContext::line_number_at_location(
    const SourceLocation &location) const throws -> usize
{
  /* A windowed resolution adds the defining file's line offset. */
  let const resolved_source = resolve_render_source(location);
  usize line = 1;
  if (resolved_source.text != nullptr) {
    const usize render_position =
        resolved_source.is_windowed
            ? location.position - resolved_source.body_start_position +
                  resolved_source.header_length
            : location.position;
    line =
        utils::line_number_at(resolved_source.text->view(), render_position) +
        (resolved_source.is_windowed ? resolved_source.line_offset : 0);
  }
  return line;
}

fn EvalContext::funcname_line_at(usize index) const throws -> usize
{
  /* A source or main frame past the function calls reports 0, the way bash
     bottoms out BASH_LINENO. An outer call-site location is rendered against
     the live source window, so the innermost frame and a single-source script
     are exact, while a frame whose defining file was sourced and freed can
     misnumber.
   */
  let const call_count = m_function_call_names.count();
  if (index < call_count)
    return line_number_at_location(
        m_function_call_locations[call_count - 1 - index]);
  return 0;
}

pure fn EvalContext::in_function_scope() const wontthrow -> bool
{
  return !m_local_scopes.is_empty();
}

fn EvalContext::is_local_in_current_scope(StringView name) const wontthrow
    -> bool
{
  if (m_local_scopes.is_empty()) return false;
  for (let const &binding : m_local_scopes.back())
    if (binding.name.view() == name) return true;
  return false;
}

fn EvalContext::set_alias(StringView name, StringView value) throws -> void
{
  LOG(All, "setting alias '%.*s' to a %zu byte value",
      static_cast<int>(name.length), name.data, value.length);
  m_aliases.set(name, value);
}

fn EvalContext::remove_alias(StringView name) throws -> bool
{
  if (m_aliases.find(name) == nullptr) return false;
  LOG(All, "removing alias '%.*s'", static_cast<int>(name.length), name.data);
  m_aliases.erase(name);
  return true;
}

pure fn EvalContext::has_aliases() const wontthrow -> bool
{
  return m_aliases.count() != 0;
}

fn EvalContext::get_alias(StringView name) const throws -> Maybe<String>
{
  if (let const *value = m_aliases.find(name))
    return String{heap_allocator(), value->view()};
  return None;
}

fn EvalContext::alias_definitions() const throws -> ArrayList<String>
{
  let out = ArrayList<String>{heap_allocator()};
  m_aliases.for_each([&out](StringView key, const String &value) {
    let definition = String{heap_allocator(), key};
    definition.append(StringView{"='", 2});
    definition.append(value);
    definition.push('\'');
    out.push(steal(definition));
  });
  out.sort();
  return out;
}

fn EvalContext::alias_names() const throws -> HashSet
{
  let out = HashSet{heap_allocator()};
  m_aliases.for_each([&out](StringView key, const String &value) {
    unused(value);
    out.add(key);
  });
  return out;
}

fn EvalContext::enter_subshell() wontthrow -> void
{
  m_subshell_depth++;
  LOG(Debug, "entered a subshell, depth now %zu", m_subshell_depth);
}

fn EvalContext::leave_subshell() wontthrow -> void
{
  ASSERT(m_subshell_depth > 0);
  /* A bare exec inside this subshell moved real descriptors. The backups put
     them back newest first so stacked moves unwind in order. */
  while (!m_subshell_saved_descriptors.is_empty() &&
         m_subshell_saved_descriptors.back().depth == m_subshell_depth)
  {
    LOG(Debug, "restoring descriptor %d a subshell exec moved at depth %zu",
        m_subshell_saved_descriptors.back().saved.shell_fd, m_subshell_depth);
    os::restore_descriptor(m_subshell_saved_descriptors.back().saved);
    m_subshell_saved_descriptors.remove(m_subshell_saved_descriptors.count() -
                                        1);
  }
  m_subshell_depth--;
  LOG(Debug, "left a subshell, depth now %zu", m_subshell_depth);
}

fn EvalContext::snapshot_subshell_descriptor(i32 shell_fd) throws -> void
{
  if (m_subshell_depth == 0) return;
  for (let const &entry : m_subshell_saved_descriptors) {
    if (entry.depth == m_subshell_depth && entry.saved.shell_fd == shell_fd) {
      return;
    }
  }
  LOG(Debug,
      "backing up descriptor %d before a subshell exec moves it at depth %zu",
      shell_fd, m_subshell_depth);
  m_subshell_saved_descriptors.push(subshell_saved_descriptor{
      m_subshell_depth, os::save_descriptor(shell_fd)});
}

pure fn EvalContext::in_subshell() const wontthrow -> bool
{
  return m_subshell_depth > 0;
}

fn EvalContext::request_break(i64 level, SourceLocation location) throws -> void
{
  /* A break with no enclosing loop is a no-op, and a level past the nesting
     clamps to the outermost loop, so no leftover level escapes as an error. */
  if (m_loop_depth == 0) {
    LOG(Debug, "break requested outside a loop, ignored");
    return;
  }
  if (static_cast<usize>(level) > m_loop_depth)
    level = static_cast<i64>(m_loop_depth);
  LOG(All, "break requested, level %lld of depth %zu", (long long) level,
      m_loop_depth);
  m_control_flow = control_flow{control_flow::Kind::Break, level, location,
                                m_current_source, String{m_current_origin}};
}

fn EvalContext::request_continue(i64 level, SourceLocation location) throws
    -> void
{
  if (m_loop_depth == 0) {
    LOG(Debug, "continue requested outside a loop, ignored");
    return;
  }
  if (static_cast<usize>(level) > m_loop_depth)
    level = static_cast<i64>(m_loop_depth);
  LOG(All, "continue requested, level %lld of depth %zu", (long long) level,
      m_loop_depth);
  m_control_flow = control_flow{control_flow::Kind::Continue, level, location,
                                m_current_source, String{m_current_origin}};
}

fn EvalContext::request_return(i64 status, SourceLocation location) throws
    -> void
{
  LOG(Debug, "return requested, status %lld", (long long) status);
  m_control_flow = control_flow{control_flow::Kind::Return, status, location,
                                m_current_source, String{m_current_origin}};
}

fn EvalContext::request_exit(i64 status, SourceLocation location) throws -> void
{
  LOG(Debug, "exit requested, status %lld", (long long) status);
  m_control_flow = control_flow{control_flow::Kind::Exit, status, location,
                                m_current_source, String{m_current_origin}};
}

pure fn EvalContext::has_pending_control_flow() const wontthrow -> bool
{
  return m_control_flow.kind != control_flow::Kind::Normal;
}

fn EvalContext::pending_control_flow() wontthrow -> control_flow &
{
  return m_control_flow;
}

pure fn EvalContext::pending_control_flow() const wontthrow
    -> const control_flow &
{
  return m_control_flow;
}

fn EvalContext::clear_control_flow() wontthrow -> void
{
  m_control_flow.kind = control_flow::Kind::Normal;
}

fn EvalContext::set_current_source(const String *source,
                                   String origin) wontthrow -> void
{
  m_current_source = source;
  m_current_origin = steal(origin);
}

pure fn EvalContext::current_source() const wontthrow -> const String *
{
  return m_current_source;
}

pure fn EvalContext::current_origin() const wontthrow -> const String &
{
  return m_current_origin;
}

fn EvalContext::print_source_backtrace(
    Maybe<SourceLocation> error_location) const throws -> void
{
  for (usize i = m_source_frames.count(); i > 0; i--) {
    let const &frame = m_source_frames[i - 1];
    if (frame.parent_source != nullptr) {
      /* A frame whose call site is exactly where the error already pointed is
         dropped to leave a single caret there. The file must match too, since
         two sources can share a byte offset and length while pointing at
         unrelated text. */
      let const &call_file = frame.call_site.filename;
      let const &error_file =
          error_location.has_value() ? error_location->filename : call_file;
      let const same_file =
          call_file.has_value() == error_file.has_value() &&
          (!call_file.has_value() || *call_file == *error_file);
      if (error_location.has_value() && same_file &&
          frame.call_site.position == error_location->position &&
          frame.call_site.length == error_location->length)
      {
        continue;
      }
      let const sourced_here = TraceWithLocation{frame.call_site};
      show_message(sourced_here.to_string(*frame.parent_source));
    }
  }
}

fn EvalContext::set_current_location(SourceLocation location) wontthrow -> void
{
  m_current_location = location;
}

/* TODO: these caps are hand-tuned below the observed native overflow point.
   Query the actual stack size per platform, getrlimit RLIMIT_STACK on POSIX and
   the thread stack on Windows, and derive the caps from it. */
/* A cap on nested dot-source and eval runs, below the native stack overflow
   point since each level spends many native frames between run_source calls. */
static constexpr usize MAX_SOURCE_DEPTH = 400;
/* A separate, larger cap on nested function calls, which reach the native stack
   overflow point at a greater depth by spending fewer native frames per level.
 */
static constexpr usize MAX_FUNCTION_CALL_DEPTH = 900;
/* A cap on nested command substitution, which reparses and re-evaluates the
   inner command at each level and so spends the most native frames per level. A
   release build overflows the native stack near a thousand levels and a
   sanitizer build past two hundred, so the cap stays well below both. */
static constexpr usize MAX_SUBSTITUTION_DEPTH = 128;
/* A cap on nested parameter expansion, which re-enters the dispatch for each
   ${name:-${...}} default and spends fewer native frames per level than command
   substitution, so a release build overflows near nine thousand levels and the
   cap stays well below it. */
static constexpr usize MAX_PARAMETER_EXPANSION_DEPTH = 256;

fn EvalContext::enter_source(SourceLocation location) throws -> void
{
  if (m_source_depth >= MAX_SOURCE_DEPTH) {
    LOG(Debug, "source depth %zu exceeds cap %zu", m_source_depth,
        MAX_SOURCE_DEPTH);
    throw ErrorWithLocation{location,
                            "Maximum source/recursion depth exceeded"};
  }
  m_source_depth++;
}

fn EvalContext::leave_source() wontthrow -> void
{
  ASSERT(m_source_depth > 0);
  m_source_depth--;
}

fn EvalContext::enter_function_call(SourceLocation location) throws -> void
{
  if (m_function_call_depth >= MAX_FUNCTION_CALL_DEPTH) {
    LOG(Debug, "function call depth %zu exceeds cap %zu", m_function_call_depth,
        MAX_FUNCTION_CALL_DEPTH);
    throw ErrorWithLocation{location,
                            "Maximum source/recursion depth exceeded"};
  }
  m_function_call_depth++;
  LOG(Debug, "entered function call depth %zu", m_function_call_depth);
}

fn EvalContext::leave_function_call() wontthrow -> void
{
  ASSERT(m_function_call_depth > 0);
  m_function_call_depth--;
}

fn EvalContext::enter_substitution() throws -> void
{
  if (m_substitution_depth >= MAX_SUBSTITUTION_DEPTH) {
    LOG(Debug, "substitution depth %zu exceeds cap %zu", m_substitution_depth,
        MAX_SUBSTITUTION_DEPTH);
    throw Error{"Command substitution nested too deeply"};
  }
  m_substitution_depth++;
}

fn EvalContext::leave_substitution() wontthrow -> void
{
  ASSERT(m_substitution_depth > 0);
  m_substitution_depth--;
}

fn EvalContext::enter_parameter_expansion() throws -> void
{
  if (m_parameter_expansion_depth >= MAX_PARAMETER_EXPANSION_DEPTH) {
    LOG(Debug, "parameter expansion depth %zu exceeds cap %zu",
        m_parameter_expansion_depth, MAX_PARAMETER_EXPANSION_DEPTH);
    throw Error{"Parameter expansion nested too deeply"};
  }
  m_parameter_expansion_depth++;
}

fn EvalContext::leave_parameter_expansion() wontthrow -> void
{
  ASSERT(m_parameter_expansion_depth > 0);
  m_parameter_expansion_depth--;
}

fn EvalContext::set_error_exit(bool enabled) wontthrow -> void
{
  LOG(Info, "the errexit option flips to %s", enabled ? "on" : "off");
  m_error_exit = enabled;
}

pure fn EvalContext::error_exit() const wontthrow -> bool
{
  return m_error_exit;
}

fn EvalContext::set_echo_expanded(bool enabled) wontthrow -> void
{
  LOG(Info, "the xtrace option flips to %s", enabled ? "on" : "off");
  m_enable_echo_expanded = enabled;
}

fn EvalContext::set_error_unset(bool enabled) wontthrow -> void
{
  LOG(Info, "the nounset option flips to %s", enabled ? "on" : "off");
  m_runtime.error_unset = enabled;
}

pure fn EvalContext::error_unset() const wontthrow -> bool
{
  return m_runtime.error_unset;
}

fn EvalContext::set_pipefail(bool enabled) wontthrow -> void
{
  LOG(Info, "the pipefail option flips to %s", enabled ? "on" : "off");
  m_runtime.pipefail = enabled;
}

pure fn EvalContext::pipefail() const wontthrow -> bool
{
  return m_runtime.pipefail;
}

fn EvalContext::set_no_clobber(bool enabled) wontthrow -> void
{
  LOG(Info, "the noclobber option flips to %s", enabled ? "on" : "off");
  m_no_clobber = enabled;
}

pure fn EvalContext::no_clobber() const wontthrow -> bool
{
  return m_no_clobber;
}

fn EvalContext::set_export_all(bool enabled) wontthrow -> void
{
  LOG(Info, "the allexport option flips to %s", enabled ? "on" : "off");
  m_export_all = enabled;
}

pure fn EvalContext::export_all() const wontthrow -> bool
{
  return m_export_all;
}

fn EvalContext::set_stats_enabled(bool enabled) wontthrow -> void
{
  m_stats_enabled = enabled;
}

pure fn EvalContext::stats_enabled() const wontthrow -> bool
{
  return m_stats_enabled;
}

fn EvalContext::set_no_glob(bool enabled) wontthrow -> void
{
  LOG(Info, "the noglob option flips to %s", enabled ? "on" : "off");
  m_enable_path_expansion = !enabled;
}

pure fn EvalContext::no_glob() const wontthrow -> bool
{
  return !m_enable_path_expansion;
}

fn EvalContext::set_no_exec(bool enabled) wontthrow -> void
{
  LOG(Info, "the noexec option flips to %s", enabled ? "on" : "off");
  m_no_exec = enabled;
}

pure fn EvalContext::no_exec() const wontthrow -> bool { return m_no_exec; }

fn EvalContext::set_shitbox(bool enabled) wontthrow -> void
{
  LOG(Info, "the shitbox option flips to %s", enabled ? "on" : "off");
  m_shitbox = enabled;
}

pure fn EvalContext::shitbox() const wontthrow -> bool { return m_shitbox; }

fn EvalContext::set_failglob(bool enabled) wontthrow -> void
{
  LOG(Info, "the failglob option flips to %s", enabled ? "on" : "off");
  m_runtime.failglob = enabled;
}

pure fn EvalContext::failglob() const wontthrow -> bool
{
  return m_runtime.failglob;
}

fn EvalContext::enter_condition() wontthrow -> void { m_condition_depth++; }

fn EvalContext::leave_condition() wontthrow -> void
{
  ASSERT(m_condition_depth > 0);
  m_condition_depth--;
}

pure fn EvalContext::in_condition() const wontthrow -> bool
{
  return m_condition_depth > 0;
}

fn EvalContext::enter_loop() wontthrow -> void { m_loop_depth++; }

fn EvalContext::leave_loop() wontthrow -> void
{
  ASSERT(m_loop_depth > 0);
  m_loop_depth--;
}

/* A loop appending to many distinct targets keeps each open until the loop
   exits, so the count is bounded and a target past the bound reopens every
   iteration the way bash does, rather than exhausting the descriptor table. */
static constexpr usize MAX_LOOP_REDIRECT_FDS = 16;

fn EvalContext::mark_loop_redirect_fds() const wontthrow
    -> loop_redirect_fd_mark
{
  return {m_loop_redirect_fds.count()};
}

fn EvalContext::cleanup_loop_redirect_fds(loop_redirect_fd_mark mark) wontthrow
    -> void
{
  for (usize i = m_loop_redirect_fds.count(); i > mark.count; i--)
    os::close_fd(m_loop_redirect_fds[i - 1].fd);

  while (m_loop_redirect_fds.count() > mark.count)
    m_loop_redirect_fds.remove(m_loop_redirect_fds.count() - 1);
}

fn EvalContext::find_loop_redirect_fd(i32 target_fd, const String &path,
                                      os::file_open_mode mode) const wontthrow
    -> Maybe<os::descriptor>
{
  for (let const &entry : m_loop_redirect_fds) {
    if (entry.target_fd == target_fd && entry.mode == mode &&
        entry.path == path)
    {
      return entry.fd;
    }
  }

  return None;
}

fn EvalContext::retain_loop_redirect_fd(i32 target_fd, const String &path,
                                        os::file_open_mode mode,
                                        os::descriptor fd) throws -> bool
{
  if (m_loop_redirect_fds.count() >= MAX_LOOP_REDIRECT_FDS) return false;

  m_loop_redirect_fds.push(loop_redirect_fd{
      target_fd, mode, String{heap_allocator(), path.view()},
        fd
  });
  return true;
}

pure fn EvalContext::loop_depth() const wontthrow -> usize
{
  return m_loop_depth;
}

fn EvalContext::set_loop_depth(usize depth) wontthrow -> void
{
  m_loop_depth = depth;
}

fn EvalContext::set_terminal_exec_allowed(bool enabled) wontthrow -> void
{
  m_terminal_exec_allowed = enabled;
}

pure fn EvalContext::terminal_exec_allowed() const wontthrow -> bool
{
  return m_terminal_exec_allowed;
}

pure fn EvalContext::getopts_char_index() const wontthrow -> usize
{
  return m_getopts_char_index;
}

fn EvalContext::set_getopts_char_index(usize index) wontthrow -> void
{
  m_getopts_char_index = index;
}

pure fn EvalContext::getopts_last_optind() const wontthrow -> i64
{
  return m_getopts_last_optind;
}

fn EvalContext::set_getopts_last_optind(i64 optind) wontthrow -> void
{
  m_getopts_last_optind = optind;
}

fn EvalContext::sorted_variable_assignments() const throws -> ArrayList<String>
{
  let assignments = ArrayList<String>{heap_allocator()};
  assignments.reserve(m_shell_variables.count());
  m_shell_variables.for_each([&](StringView name, const String &value) {
    let entry = String{heap_allocator(), name};
    entry.push('=');
    entry.append(value);
    assignments.push(steal(entry));
  });
  assignments.sort();
  return assignments;
}

fn EvalContext::clear_functions() wontthrow -> void
{
  m_functions.clear();
  m_function_sources.clear();
  m_function_definition_infos.clear();
}

fn EvalContext::snapshot_state() const throws -> eval_state_snapshot
{
  return eval_state_snapshot{m_shell_variables,
                             m_functions,
                             m_function_sources,
                             m_function_definition_infos,
                             m_aliases,
                             m_positional_params,
                             Path::current_directory(),
                             m_traps,
                             m_readonly_names,
                             m_integer_names,
                             m_error_exit,
                             m_enable_path_expansion,
                             m_enable_echo,
                             m_enable_echo_expanded,
                             m_environment_undo_log.count(),
                             RuntimeState::capture(*this),
                             m_no_clobber,
                             m_export_all};
}

fn EvalContext::restore_state(eval_state_snapshot snapshot) throws -> void
{
  LOG(Debug, "restoring the evaluator state after a subshell or substitution");
  m_shell_variables = steal(snapshot.shell_variables);
  m_functions = steal(snapshot.functions);
  m_function_sources = steal(snapshot.function_sources);
  m_function_definition_infos = steal(snapshot.function_definition_infos);
  m_aliases = steal(snapshot.aliases);
  m_positional_params = steal(snapshot.positional_params);

  /* The option flags revert with the subshell. */
  m_error_exit = snapshot.error_exit;
  m_enable_path_expansion = snapshot.is_path_expansion_enabled;
  m_enable_echo = snapshot.is_echo_enabled;
  m_enable_echo_expanded = snapshot.is_echo_expanded_enabled;

  snapshot.runtime.restore(*this);
  m_no_clobber = snapshot.no_clobber;
  m_export_all = snapshot.export_all;

  m_readonly_names = steal(snapshot.readonly_names);
  m_integer_names = steal(snapshot.integer_names);

  /* A signal the subshell trapped that the parent does not is returned to
     default first, then the parent's dispositions are reinstalled from the
     restored table. */
  if (m_traps.count() != 0 || snapshot.traps.count() != 0) {
    m_traps.for_each([&](StringView condition, const String &action) {
      unused(action);
      if (condition == "EXIT") return;
      if (snapshot.traps.find(condition) != nullptr) return;
      if (let const number = os::signal_number_from_name(condition))
        os::clear_trap_handler(*number);
    });
    m_traps = steal(snapshot.traps);
    install_trap_dispositions();
  } else {
    m_traps = steal(snapshot.traps);
  }

  /* A failed restore means the directory was removed mid-run, which is not
     fatal to the parent, so it is logged rather than thrown. */
  if (Path::set_current_directory(snapshot.working_directory).is_error())
    LOG(Debug, "the subshell could not restore the working directory");

  /* A write to an exported name hit the process environment, which the restored
     variable map does not cover, so the logged writes revert newest first. The
     rewind precedes the PATH re-point so an exported PATH reads its restored
     value. */
  LOG(Debug, "rewinding %zu environment writes made inside the subshell",
      m_environment_undo_log.count() - snapshot.environment_undo_mark);
  while (m_environment_undo_log.count() > snapshot.environment_undo_mark) {
    let const &entry = m_environment_undo_log.back();
    if (entry.previous_value)
      os::set_environment_variable(entry.name.view(),
                                   entry.previous_value->view());
    else
      os::unset_environment_variable(entry.name.view());
    sync_exported_after_restore(entry.name.view(),
                                entry.previous_value.has_value());
    m_environment_undo_log.pop_back();
  }

  /* The cached field separators track the restored map so an inner IFS change
     does not leak its split to the parent. */
  if (let const *ifs = m_shell_variables.find(StringView{"IFS", 3}))
    set_field_separators(ifs->view());
  else
    set_field_separators(" \t\n");

  /* The resolver reads a process-global PATH, so it is re-pointed at the
     restored PATH. */
  utils::set_path_for_resolution(get_variable_value("PATH"));

  /* The exit status is intentionally not restored, a subshell propagates its
     last command's status to the parent. */
}

fn EvalContext::option_flags_string() const throws -> String
{
  /* The letters follow bash's own order, a e f h u v x B C, so $- matches it
     flag for flag. hashall and braceexpand are on by default outside the posix
     mood the way bash reports them, while dash names neither. */
  let const bash_flags_on = !is_posix_mode();
  let flags = String{heap_allocator()};
  if (export_all()) flags += 'a';
  if (m_error_exit) flags += 'e';
  if (!m_enable_path_expansion) flags += 'f';
  if (bash_flags_on) flags += 'h';
  if (m_shell_is_interactive) flags += 'i';
  if (error_unset()) flags += 'u';
  if (m_enable_echo) flags += 'v';
  if (m_enable_echo_expanded) flags += 'x';
  if (bash_flags_on) flags += 'B';
  if (no_clobber()) flags += 'C';

  return flags;
}

fn EvalContext::set_last_exit_status(i32 status) wontthrow -> void
{
  m_last_exit_status = status;
}

fn EvalContext::set_last_command_duration_ns(u64 nanos) wontthrow -> void
{
  m_last_command_duration_ns = nanos;
}

pure fn EvalContext::last_command_duration_ns() const wontthrow -> u64
{
  return m_last_command_duration_ns;
}

pure fn EvalContext::last_exit_status() const wontthrow -> i32
{
  return m_last_exit_status;
}

fn EvalContext::apply_indirect_or_name_listing(StringView body) throws -> String
{
  LOG(All, "applying the indirect expansion '${!%.*s}'",
      static_cast<int>(body.length), body.data);
  if (body.is_empty()) return String{scratch_allocator()};

  if (body.length >= 4 && body[body.length - 1] == ']' &&
      (body[body.length - 2] == '@' || body[body.length - 2] == '*') &&
      body[body.length - 3] == '[' && lexer::is_variable_name_start(body[0]))
  {
    const StringView array_name = body.substring_of_length(0, body.length - 3);
    let const subscripts = collect_array_subscripts(array_name);
    let out = String{scratch_allocator()};
    for (usize i = 0; i < subscripts.count(); i++) {
      if (i > 0) out.push(' ');
      out.append(subscripts[i].view());
    }
    return out;
  }

  const char last = body[body.length - 1];
  if (last == '*' || last == '@') {
    /* ${!prefix*} joins the matching names with a space. The quoted
       "${!prefix@}" per-name field form is produced in the field-expansion
       path, since this string return cannot carry the field boundaries. */
    const StringView prefix = body.substring_of_length(0, body.length - 1);
    let const names = matching_prefix_names(prefix);
    let out = String{scratch_allocator()};
    for (usize i = 0; i < names.count(); i++) {
      if (i > 0) out.push(' ');
      out.append(names[i].view());
    }
    return out;
  }

  const Maybe<String> target = get_variable_value(body);
  if (!target.has_value()) {
    if (m_runtime.error_unset)
      throw_script_fatal("Unable to expand '" + body +
                         "' because the parameter is not set");
    return String{scratch_allocator()};
  }
  /* A target naming an array element, such as the a[1] a reference variable
     holds, routes through the subscript read. */
  let const target_view = target->view();
  if (let const bracket = target_view.find_character('[');
      bracket.has_value() && !target_view.is_empty() &&
      target_view[target_view.length - 1] == ']')
  {
    return apply_array_subscript(
        target_view.substring_of_length(0, *bracket),
        target_view.substring_of_length(*bracket + 1,
                                        target_view.length - *bracket - 2));
  }
  if (!get_variable_value(target_view).has_value())
    report_unset_reference(*target);
  return expand_variable(target_view);
}

cold fn EvalContext::make_stats_string() const throws -> String
{
  let stats_text = String{heap_allocator()};

  /* Stats print before end_command runs the rollup, so the live arena is
     sampled here. */
  const usize live_ast_arena_bytes =
      AST_ARENA != nullptr ? AST_ARENA->bytes_used() : 0;
  const usize peak_ast_arena_bytes =
      live_ast_arena_bytes > m_peak_ast_arena_bytes ? live_ast_arena_bytes
                                                    : m_peak_ast_arena_bytes;

  stats_text += "[Stats\n";

  stats_text += EXPRESSION_DOUBLE_AST_INDENT;
  stats_text +=
      "Commands evaluated: " +
      String::from(m_commands_evaluated + 1, heap_allocator());
  stats_text += '\n';
  stats_text += EXPRESSION_DOUBLE_AST_INDENT;
  stats_text += "Expansions: " + String::from(last_expansion_count(), heap_allocator());
  stats_text += '\n';
  stats_text += EXPRESSION_DOUBLE_AST_INDENT;
  stats_text +=
      "Nodes evaluated: " + String::from(last_expressions_executed(), heap_allocator());
  stats_text += '\n';
  stats_text += EXPRESSION_DOUBLE_AST_INDENT;
  stats_text +=
      "Total expansions: " + String::from(total_expansion_count(), heap_allocator());
  stats_text += '\n';
  stats_text += EXPRESSION_DOUBLE_AST_INDENT;
  stats_text += "Total nodes evaluated: " +
                String::from(total_expressions_executed(), heap_allocator());
  stats_text += '\n';
  stats_text += EXPRESSION_DOUBLE_AST_INDENT;
  stats_text += "AST arena bytes: " + String::from(live_ast_arena_bytes, heap_allocator());
  stats_text += '\n';
  stats_text += EXPRESSION_DOUBLE_AST_INDENT;
  stats_text +=
      "Peak AST arena bytes: " + String::from(peak_ast_arena_bytes, heap_allocator());
  stats_text += '\n';

  stats_text += "]";

  return stats_text;
}

pure fn EvalContext::should_echo() const wontthrow -> bool
{
  return m_enable_echo;
}

pure fn EvalContext::should_echo_expanded() const wontthrow -> bool
{
  return m_enable_echo_expanded;
}

pure fn EvalContext::shell_is_interactive() const wontthrow -> bool
{
  return m_shell_is_interactive;
}

pure fn EvalContext::last_expressions_executed() const wontthrow -> usize
{
  return m_expressions_executed_last;
}

pure fn EvalContext::total_expressions_executed() const wontthrow -> usize
{
  return m_expressions_executed_total + m_expressions_executed_last;
}

pure fn EvalContext::last_expansion_count() const wontthrow -> usize
{
  return m_expansions_last;
}

pure fn EvalContext::total_expansion_count() const wontthrow -> usize
{
  return m_expansions_total + m_expansions_last;
}

pure fn EvalContext::commands_evaluated() const wontthrow -> usize
{
  return m_commands_evaluated;
}

pure fn EvalContext::peak_ast_arena_bytes() const wontthrow -> usize
{
  return m_peak_ast_arena_bytes;
}

/* The arithmetic engine, the ArithmeticParser, the cached-token fast path, and
   the EvalContext arithmetic methods, lives in EvalArithmetic.cpp. */

ExecContext::ExecContext(SourceLocation location, ResolvedCommand &&kind,
                         ArrayList<String> &&args)
    : m_kind(steal(kind)), m_location(location), m_args(steal(args))
{}

pure fn ExecContext::source_location() const wontthrow -> const SourceLocation &
{
  return m_location;
}

pure fn ExecContext::program() const wontthrow -> const String &
{
  ASSERT(!m_args.is_empty());
  return m_args[0];
}

pure fn ExecContext::args() const wontthrow -> const ArrayList<String> &
{
  return m_args;
}

pure fn ExecContext::is_builtin() const wontthrow -> bool
{
  return m_kind.is_builtin();
}

pure fn ExecContext::is_unresolved() const wontthrow -> bool
{
  return m_kind.is_unresolved();
}

pure fn ExecContext::program_path() const wontthrow -> const Path &
{
  ASSERT(!is_builtin());
  return m_kind.program_path;
}

fn ExecContext::close_fds() throws -> void
{
  if (in_fd.has_value()) {
    os::close_fd(*in_fd);
    in_fd.reset();
  }
  if (out_fd.has_value()) {
    os::close_fd(*out_fd);
    out_fd.reset();
  }
  if (err_fd.has_value()) {
    os::close_fd(*err_fd);
    err_fd.reset();
  }
}

pure fn ExecContext::builtin_kind() const wontthrow -> const Builtin::Kind &
{
  ASSERT(is_builtin());
  return m_kind.builtin_kind;
}

fn ExecContext::print_to_stdout(StringView s) const throws -> void
{
  if (!os::write_fd(out_fd.value_or(SHIT_STDOUT), s.data, s.length).has_value())
  {
    throw Error{"Unable to write to stdout: " +
                os::last_system_error_message()};
  }
}

fn ExecContext::print_to_stderr(StringView s) const throws -> void
{
  if (!os::write_fd(err_fd.value_or(SHIT_STDERR), s.data, s.length).has_value())
  {
    throw Error{"Unable to write to stderr: " +
                os::last_system_error_message()};
  }
}

fn ExecContext::make_from(SourceLocation location, ArrayList<String> &&args,
                          mimic_mood mood, bool is_shitbox_enabled) throws
    -> ExecContext
{
  ASSERT(args.count() > 0);

  let const &program = args[0];

  Maybe<Builtin::Kind> resolved_builtin;
  Maybe<Path> resolved_program_path;

  if (!program.find_character('/').has_value()) {
    resolved_builtin = search_builtin(program.view());

    /* let is a bash extension absent from a POSIX sh, so the sh mood resolves
       it as an external command and reports it not found the way dash does. */
    if (resolved_builtin == Builtin::Kind::Let && mood == mimic_mood::Posix) {
      resolved_builtin = None;
    }

    /* With the shitbox option on, a bare utility name resolves to the shitbox
       builtin, beating an external program the way a busybox applet does. */
    if (!resolved_builtin.has_value() && is_shitbox_enabled &&
        shitbox::find_util(program.view()).has_value())
    {
      resolved_builtin = Builtin::Kind::Shitbox;
    }

    if (!resolved_builtin.has_value()) {
      let program_search_paths = utils::search_program_path(program.view());
      if (program_search_paths.count() > 0)
        resolved_program_path = steal(program_search_paths[0]);
    }
  } else {
    /* canonicalize_path tries the omitted suffixes, so a path-given program
       resolves its extension. */
    resolved_program_path = Path::canonicalize(program.view());
  }

  ResolvedCommand kind;
  if (!resolved_builtin) {
    if (resolved_program_path.has_value()) {
      LOG(Debug, "resolved '%s' to the program '%s'", program.c_str(),
          resolved_program_path->text().c_str());
      kind = ResolvedCommand::from_program(steal(*resolved_program_path));
    } else if (mood == mimic_mood::Default &&
               shitbox::find_util(program.view()).has_value())
    {
      /* In the default mood a coreutil falls back to the bundled shitbox
         implementation when PATH has no binary of that name. */
      LOG(Debug, "no program matches '%s', using the shitbox utility",
          program.c_str());
      kind = ResolvedCommand::from_builtin(Builtin::Kind::Shitbox);
    } else {
      LOG(Debug, "no builtin or program matches '%s'", program.c_str());
      let error =
          CommandNotFound{location, "Program '" + program + "' wasn't found"};
      if (Maybe<String> suggestion = utils::suggest_command(
              program.view(), ArrayList<String>{heap_allocator()}))
      {
        let const hint = "Did you mean '" + *suggestion + "'?";
        error.set_note(hint.view());
      }
      throw error;
    }
  } else {
    LOG(Debug, "resolved '%s' to a builtin", program.c_str());
    kind = ResolvedCommand::from_builtin(*resolved_builtin);
  }

  return {location, steal(kind), steal(args)};
}

fn ExecContext::from_resolved(SourceLocation location, ResolvedCommand kind,
                              ArrayList<String> &&args) throws -> ExecContext
{
  ASSERT(args.count() > 0);
  return {location, steal(kind), steal(args)};
}

fn ExecContext::make_unresolved(SourceLocation location) throws -> ExecContext
{
  /* The stage never runs, so one placeholder argument satisfies the argv[0]
     invariant. */
  let args = ArrayList<String>{heap_allocator()};
  args.push(String{heap_allocator()});
  return {location, ResolvedCommand::from_unresolved(), steal(args)};
}

} // namespace shit
