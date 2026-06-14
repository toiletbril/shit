#include "Eval.hpp"

#include "Arena.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "ResolvedCommand.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <exception>

/* POSIX regcomp and regexec back the [[ =~ operator. The release build drops
   libstdc++, so std::regex is unavailable and the libc regex is used instead.
 */
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
  /* Seed the separator table from the default IFS, since the table starts
     empty while m_field_separators is initialized to whitespace. */
  set_field_separators(m_field_separators.view());

  /* The shell start time anchors $SECONDS. The $RANDOM seed is deferred to the
     first read of RANDOM, so a run that never reads it, the common -c case,
     pays neither the seed nor the syscall it mixes in. */
  m_shell_start_time = static_cast<i64>(std::time(nullptr));

  /* Every inherited environment variable is exported, so the set starts from
     the process environment. An assignment then tests this set rather than
     scanning the environment on every write. */
  for (const String &name : os::environment_names())
    m_exported_names.add(name.view());
}

fn EvalContext::add_evaluated_expression() wontthrow -> void
{
  /* The count feeds only the -S report, so skip the increment unless -S asked
     for it and keep the per-node hot path free of the bookkeeping. */
  if (!m_stats_enabled) return;
  m_expressions_executed_last++;
}

fn EvalContext::add_expansion() wontthrow -> void { m_expansions_last++; }

fn EvalContext::end_command() wontthrow -> void
{
  m_expansions_total += m_expansions_last;
  m_expressions_executed_total += m_expressions_executed_last;
  m_commands_evaluated++;

  /* Sample the arena before the next command resets it, so the peak reflects
     the largest tree this run has built. The arena is null only outside a
     parse, which end_command never runs in. */
  if (AST_ARENA != nullptr) {
    const usize used = AST_ARENA->bytes_used();
    if (used > m_peak_ast_arena_bytes) m_peak_ast_arena_bytes = used;
  }

  m_expansions_last = m_expressions_executed_last = 0;
}

hot fn EvalContext::assign_variable(StringView name, StringView value) throws
    -> void
{
  LOG(verbosity::All, "assigning variable '%.*s' to a value of %zu bytes",
      static_cast<int>(name.length), name.data, value.length);
  /* The field separators are read once per expanded word, so the live value is
     cached here to keep that path off the map and the environment. */
  if (name == "IFS") set_field_separators(value);
  /* A new PATH names a different search order, so a cached resolution may point
     at a directory PATH no longer lists. The resolver is pointed at the new
     value, so a plain PATH=... assignment the store holds drives the search
     even without an export, and the cache is marked stale so the next command
     re-resolves. */
  if (name == "PATH") utils::set_path_for_resolution(String{value});
  m_shell_variables.set(name, value);
  /* An exported name lives in the process environment, where export moved it
     and cleared the shell copy. A plain reassignment writes the shell store
     above, so an in-process read sees the new value, but a child still inherits
     the stale environment entry. The environment is refreshed here when the
     name is exported, so a child of `export FOO=1; FOO=2` sees 2. Membership is
     the exported-names set, an O(1) test, so a non-exported assignment, the
     common loop counter, never scans the environment. */
  if (m_exported_names.contains(name)) {
    /* The environment write outlives the current statement, so inside a
       subshell the name's prior value is read and logged for the restore on the
       subshell's exit. Outside a subshell the log stays empty, so the prior
       value is never read. */
    if (m_subshell_depth > 0)
      m_environment_undo_log.push(environment_undo_entry{
          String{name}, os::get_environment_variable(name)});
    os::set_environment_variable(name, value);
  }
}

fn EvalContext::set_field_separators(StringView value) throws -> void
{
  LOG(verbosity::Debug, "caching %zu field separator bytes", value.length);
  /* The table is built before m_field_separators is touched, since the
     constructor seeds it from m_field_separators' own view, so value may alias
     the buffer that the assignment below rewrites. */
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
  /* A read-only variable rejects the assignment. The common case has no
     read-only names, so the scan is skipped entirely. */
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  /* An integer-marked name evaluates its value as arithmetic on every
     assignment, the way bash applies declare -i, so the store receives the
     decimal result rather than the raw text. An empty value stores zero the
     way bash does. */
  if (is_integer_variable(name)) [[unlikely]] {
    let const result = value.length == 0 ? 0 : evaluate_arithmetic(value);
    char result_text[24];
    assign_variable(name, utils::int_to_text_into(result, result_text,
                                                  sizeof(result_text)));
    return;
  }

  assign_variable(name, value);
}

fn EvalContext::seed_shell_identity_variables(bool bash_identity) throws -> void
{
  if (bash_identity) {
    LOG(verbosity::Info, "seeding the bash identity variables");
    set_shell_variable("BASH_VERSION", "5.2.0(1)-shit");
    /* BASH_VERSINFO is the version broken into its components, the array a
       config such as ble.sh reads to gate on a major version rather than
       parsing the string. */
    let versinfo = ArrayList<String>{};
    versinfo.push(String{"5"});
    versinfo.push(String{"2"});
    versinfo.push(String{"0"});
    versinfo.push(String{"1"});
    versinfo.push(String{"release"});
    versinfo.push(String{SHIT_OS_INFO});
    set_indexed_array("BASH_VERSINFO", steal(versinfo));
    /* $BASH is the path the shell records as the executable that started it,
       which is the SHELL value the shell already holds. */
    set_shell_variable("BASH", get_variable_value("SHELL").value_or(String{}));
    /* COMP_WORDBREAKS carries readline's word-break set the way bash exposes
       it. bash-completion's word reassembly walks COMP_LINE against it, so a
       missing value collapses every word into one and the computed cursor
       word lands at zero, which kills every completion function. */
    if (!get_variable_value("COMP_WORDBREAKS").has_value())
      set_shell_variable("COMP_WORDBREAKS", StringView{" \t\n\"'><=;|&(:"});
    return;
  }
  /* sh and dash advertise no version variable, so the bash identity is just
     cleared and nothing replaces it. The clear matters for a mimicked sh whose
     parent ran in bash mode, and is a no-op at startup where nothing is set. */
  LOG(verbosity::Info,
      "clearing the bash identity variables for a non-bash mood");
  force_unset_shell_variable("BASH_VERSION");
  force_unset_shell_variable("BASH");
}

fn EvalContext::unset_shell_variable(StringView name) throws -> void
{
  /* A read-only variable rejects removal the same way it rejects assignment,
     so unset cannot defeat readonly. */
  if (is_readonly(name))
    throw Error{"Unable to unset '" + name + "' because it is read only"};

  /* A local a caller declared peels back to that caller's saved value, the
     bash upvar semantics bash-completion bubbles every result through. The
     same-scope local keeps the plain removal below, the unset local bash
     reads as empty. */
  if (peel_caller_local_binding(name)) return;

  force_unset_shell_variable(name);
  m_indexed_arrays.erase(name);
  clear_sparse_array(name);
  /* unset drops the integer mark with the value, the way bash clears the
     declare -i attribute, so a later assignment stores raw text again. */
  m_integer_names.remove(name);
}

fn EvalContext::peel_caller_local_binding(StringView name) throws -> bool
{
  /* The current scope's own local is not peeled, and outside a function there
     is nothing to peel. */
  if (m_local_scopes.count() < 2) return false;
  if (is_local_in_current_scope(name)) return false;

  /* The innermost caller frame holding a saved binding is the one bash's
     unset peels, so the scan walks from the nearest caller outward. */
  for (usize frame_index = m_local_scopes.count() - 1; frame_index-- > 0;) {
    ArrayList<local_binding> &frame = m_local_scopes[frame_index];
    for (usize i = frame.count(); i-- > 0;) {
      let &binding = frame[i];
      if (binding.name.view() != name) continue;
      LOG(verbosity::Debug,
          "peeling the local binding of '%.*s' from caller frame %zu",
          static_cast<int>(name.length), name.data, frame_index);

      /* The caller's saved state comes back right now, the same restore the
         scope pop runs, so the read after the unset already sees the next
         binding out and a following assignment writes it. */
      restore_local_binding(binding);

      /* The restore entry is consumed, so the frame's pop no longer rewinds
         this name and the value a deeper callee assigns next survives into
         the caller's caller. */
      frame.remove(i);
      return true;
    }
  }
  return false;
}

fn EvalContext::restore_local_binding(local_binding &binding) throws -> void
{
  /* Restore through assign_variable, not set_shell_variable, since the scope
     pop runs this inside a noexcept defer where a readonly name would
     otherwise throw from a destructor and terminate the shell. Both callers
     drop the binding right after, so the saved array moves out. */
  if (binding.previous_value.has_value())
    assign_variable(binding.name, *binding.previous_value);
  else
    force_unset_shell_variable(binding.name);
  /* The store is written directly rather than through set_indexed_array,
     since the readonly check could throw from the same noexcept defer. */
  /* The dense run is restored first so the sparse re-insert below routes each
     saved gap index beyond it the way the caller held it. */
  if (binding.previous_indexed_array.has_value())
    m_indexed_arrays.set(binding.name.view(),
                         steal(*binding.previous_indexed_array));
  else
    m_indexed_arrays.erase(binding.name.view());
  /* The body's sparse elements drop and the caller's saved ones come back, so
     a deep index the body set does not survive while a gap index the caller
     held is restored rather than wiped. */
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
}

fn EvalContext::set_indexed_array(StringView name,
                                  ArrayList<String> values) throws -> void
{
  LOG(verbosity::All, "storing indexed array '%.*s' with %zu elements",
      static_cast<int>(name.length), name.data, values.count());
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};
  /* The scalar entry is dropped so a $name read falls through to element zero
     the way bash treats $a as ${a[0]}. Any sparse element of a prior array of
     the same name is dropped too, since this replaces the whole array. */
  m_shell_variables.erase(name);
  clear_sparse_array(name);
  m_indexed_arrays.set(name, steal(values));
}

fn EvalContext::append_indexed_array(StringView name,
                                     ArrayList<String> values) throws -> void
{
  /* An existing array grows in place, so appending element by element stays
     linear rather than rebuilding the whole array on each call. The readonly
     guard and the scalar clear match set_indexed_array, since the in-place path
     bypasses it. */
  if (let *existing = m_indexed_arrays.find(name)) {
    LOG(verbosity::All, "appending %zu elements to the existing array '%.*s'",
        values.count(), static_cast<int>(name.length), name.data);
    if (is_readonly(name))
      throw Error{"Unable to assign '" + name + "' because it is read only"};
    m_shell_variables.erase(name);
    for (String &element : values)
      existing->push(steal(element));
    return;
  }
  set_indexed_array(name, steal(values));
}

/* The set -u read and the ${name:?} report abort the whole run in bash too,
   unlike the command-level errors the bash mood continues past, so their
   throws carry the script-fatal mark. Shared with the parameter expansion in
   EvalParamExpansion.cpp. */
[[noreturn]] fn throw_script_fatal(String message) throws -> void
{
  let error = Error{message.view()};
  error.set_script_fatal();
  throw error;
}

cold fn EvalContext::show_runtime_warning(StringView message) wontthrow -> void
{
  show_runtime_warning_at(m_current_location, message);
}

cold fn EvalContext::show_runtime_warning_at(SourceLocation location,
                                             StringView message) wontthrow
    -> void
{
  /* no-diagnostics promises no warnings at all, so the runtime advisories
     honor the live toggle the way the analysis stage does. */
  if (diagnostics_disabled()) return;
  /* The whole location is rendered, so the filename the lexer stamped
     prefixes a warning from a sourced file. A windowed resolution rebases
     the absolute position onto the function's definition copy and swaps in
     its owned filename, since the stamped view may outlive its buffer once
     the defining command's sources are freed. A formatting failure is
     swallowed so a diagnostic never becomes an error. */
  try {
    let const resolved = resolve_render_source(location);
    usize line_offset = 0;
    if (resolved.windowed) {
      location.position = location.position - resolved.body_start_position +
                          resolved.header_length;
      location.filename = resolved.filename.is_empty()
                              ? Maybe<StringView>{}
                              : Maybe<StringView>{resolved.filename};
      line_offset = resolved.line_offset;
    }
    if (resolved.text == nullptr || location.position > resolved.text->count())
    {
      show_message(Warning{message}.to_string());
      return;
    }
    let warning = WarningWithLocation{location, message};
    warning.set_line_offset(line_offset);
    show_message(warning.to_string(resolved.text->view()));
    /* A warning from a sourced file names the chain that reached it, the
       same frames an error prints, since the user typed no source command
       for an rc chain and the file alone does not say who pulled it in. A
       warning in the typed line has no frames and stays a single report. */
    if (!m_source_frames.is_empty()) print_source_backtrace();
  } catch (...) {
    LOG(verbosity::Debug,
        "formatting a runtime warning failed, the error is swallowed");
  }
}

pure fn EvalContext::locate_variable_reference(StringView name) const wontthrow
    -> SourceLocation
{
  let const fallback = m_current_location;
  if (name.is_empty()) return fallback;
  let const resolved = resolve_render_source(fallback);
  if (resolved.text == nullptr) return fallback;
  let const source = resolved.text->view();

  /* A windowed resolution means the text is the definition copy while the
     location is absolute, so the scan indexes through the rebase and the
     found location converts back, keeping every stored location absolute. */
  usize scan_start = fallback.position;
  usize absolute_shift = 0;
  if (resolved.windowed) {
    scan_start = fallback.position - resolved.body_start_position +
                 resolved.header_length;
    absolute_shift = resolved.body_start_position > resolved.header_length
                         ? resolved.body_start_position - resolved.header_length
                         : 0;
  }
  if (scan_start >= source.length) return fallback;

  /* The command's span runs from its location to the end of its logical
     line, a backslash-newline continues it. The first $name or ${name
     spelling inside that span takes the caret, with the byte after the name
     required to end it so $FOO does not match a $FOOBAR reference. */
  usize i = scan_start;
  while (i < source.length) {
    const char byte = source[i];
    if (byte == '\n' && (i == 0 || source[i - 1] != '\\')) break;
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
      /* The caret spans the $ or ${ through the name, and a brace pair
         closing right after the name joins it so ${PAGER} underlines
         whole. */
      usize reference_end = name_start + name.length;
      if (is_braced && reference_end < source.length &&
          source[reference_end] == '}')
        reference_end++;
      return SourceLocation{i + absolute_shift, reference_end - i,
                            fallback.filename};
    }
    i++;
  }

  /* Arithmetic reads a variable as a bare name with no dollar, so a second
     pass takes the first name-delimited spelling inside the same span. */
  usize k = scan_start;
  while (k + name.length <= source.length) {
    const char byte = source[k];
    if (byte == '\n' && (k == 0 || source[k - 1] != '\\')) break;
    if (source.substring_of_length(k, name.length) == name &&
        (k == 0 || !lexer::is_variable_name(source[k - 1])) &&
        (k + name.length == source.length ||
         !lexer::is_variable_name(source[k + name.length])))
      return SourceLocation{k + absolute_shift, name.length, fallback.filename};
    k++;
  }
  return fallback;
}

fn EvalContext::report_unset_reference(StringView name) throws -> void
{
  /* -W downgrades the mood-seeded fatality to a warning so the run proceeds,
     while an explicit set -u keeps the abort the script asked for. A lenient
     run without -W expands the name to empty in silence. */
  if (m_error_unset && (m_error_unset_explicit || !m_warnings_enabled))
    throw_script_fatal("Unable to expand '" + String{name} +
                       "' because the parameter is not set");
  if (m_error_unset || m_warnings_enabled)
    show_runtime_warning_at(locate_variable_reference(name),
                            "The variable '" + String{name} +
                                "' is not set, it expands to empty, replace "
                                "it with ${" +
                                String{name} +
                                "-} if empty expansion is desired");
}

fn EvalContext::warn_or_throw(bool fatal, bool explicitly_requested,
                              SourceLocation location,
                              StringView message) throws -> void
{
  if (fatal && (explicitly_requested || !m_warnings_enabled))
    throw ErrorWithLocation{location, message};
  if ((fatal || m_warnings_enabled) && !diagnostics_disabled() &&
      m_current_source != nullptr)
  {
    try {
      show_message(WarningWithLocation{location, message}.to_string(
          m_current_source->view()));
    } catch (...) {
      LOG(verbosity::Debug,
          "showing a located warning failed, the error is swallowed");
    }
  }
}

/* The flat-map key for one sparse indexed element, the array name and the
   decimal index joined by a byte that cannot occur in a name. The map copies a
   key it stores, so the callers build this on the per-command scratch arena. */
fn EvalContext::force_unset_shell_variable(StringView name) throws -> void
{
  LOG(verbosity::All,
      "removing variable '%.*s' from the store and the environment",
      static_cast<int>(name.length), name.data);
  m_shell_variables.erase(name);
  /* An exported variable also lives in the process environment, so it is
     removed there too. Otherwise a later lookup falls back to the stale
     environment value and the variable appears still set, which dash does not
     do. The removal outlives the current statement, so inside a subshell the
     prior value is logged first for the restore on the subshell's exit. */
  record_environment_change(name);
  os::unset_environment_variable(name);
  unmark_exported(name);
  if (name == "IFS") set_field_separators(" \t\n");
  /* An unset PATH drops the search order, so the resolver falls back to the
     process environment's PATH, which this just removed and so reads None. An
     export PATH=... routes through here to drop the bare copy, then sets the
     environment and refreshes the resolver itself, so the None set here is
     transient on that path. */
  if (name == "PATH")
    utils::set_path_for_resolution(os::get_environment_variable("PATH"));
}

fn EvalContext::record_environment_change(StringView name) throws -> void
{
  /* A top-level write is permanent, so the log stays empty and a later subshell
     restore finds nothing to rewind. */
  if (m_subshell_depth == 0) return;
  m_environment_undo_log.push(
      environment_undo_entry{String{name}, os::get_environment_variable(name)});
}

fn EvalContext::mark_exported(StringView name) throws -> void
{
  LOG(verbosity::All, "marking '%.*s' as exported",
      static_cast<int>(name.length), name.data);
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

hot fn EvalContext::get_variable_value(StringView name) const throws
    -> Maybe<String>
{
  /* The ordinary name dominates every read, so its dispatch is reached first.
     Every special single-character name is one byte, so a name longer than one
     byte that does not begin with a digit or 'L' is an ordinary name that goes
     straight to the store. The single-character specials, the positional digit
     run, and $LINENO are split out below so the common read pays only the first
     byte test. */
  const char first_byte = name.is_empty() ? '\0' : name[0];

  if (name.count() == 1) {
    switch (first_byte) {
    case '?': return utils::int_to_text(m_last_exit_status);
    case '$': return utils::int_to_text(os::get_shell_process_id());
    case '!':
      return m_last_background_pid ? utils::int_to_text(*m_last_background_pid)
                                   : String{};
    case '-': return option_flags_string();
    case '#':
      return String{heap_allocator(),
                    utils::uint_to_text(m_positional_params.count())};
    case '0': return String{heap_allocator(), m_shell_name};

    /* $* and $@ outside the special quoted handling join into a single word. $*
       joins with the first IFS character, $@ joins with a space. */
    case '*':
    case '@': {
      let separator = ' ';
      let has_separator = true;
      if (first_byte == '*') {
        let const &ifs = m_field_separators;
        has_separator = !ifs.is_empty();
        if (has_separator) separator = ifs.first_character();
      }
      let joined = String{};
      for (usize i = 0; i < m_positional_params.count(); i++) {
        if (i > 0 && has_separator) joined.push(separator);
        joined.append(m_positional_params[i].view());
      }
      return joined;
    }

    default: break;
    }
  }

  /* A purely numeric name selects a positional parameter, $1 upward. Only a
     name that begins with a digit can be all digits, so the scan runs only
     then. An index too large to fit, or beyond the count, has no value. The
     single '0' name is handled above as the shell name. */
  if (first_byte >= '0' && first_byte <= '9') {
    let is_all_digits = true;
    for (usize i = 0; i < name.count(); i++)
      if (std::isdigit(static_cast<unsigned char>(name[i])) == 0) {
        is_all_digits = false;
        break;
      }
    if (is_all_digits) {
      /* A positional beyond the count is unset rather than empty, so the
         strict unset report fires on it and ${1-default} takes its default
         the way bash reads an absent argument. */
      if (name.count() > 9) return None;
      let const parsed_index = utils::parse_decimal_integer(name);
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
     treats $a as ${a[0]}. An array with no element zero, empty or sparse,
     reads as unset the way bash answers ${a+set} for it. The empty-map guard
     keeps an ordinary shell with no arrays from hashing the name a second
     time on every variable read. */
  if (m_indexed_arrays.count() != 0)
    if (let const *array = m_indexed_arrays.find(name)) {
      if (array->is_empty()) return shit::None;
      return array->front();
    }

  /* IFS is held live in m_field_separators rather than the store, so a read
     with no prior assignment must report the cached separators. The store
     lookup above wins first, so an explicit IFS= empty value still reads back
     empty, while the unset default reads back space-tab-newline. This makes the
     o=$IFS; IFS=:; ...; IFS=$o save and restore idiom round-trip. The first
     byte gates the compare so an ordinary name skips it. */
  if (first_byte == 'I' && name == "IFS")
    return String{heap_allocator(), m_field_separators.view()};

  /* The branch the \G prompt segment renders, the shell's own dynamic
     variable, so a script or a prompt command reads it without forking git.
     Empty outside a repository, the short hash on a detached HEAD. A stored
     value above wins the way the other dynamic names yield. */
  if (first_byte == 'S' && name == "SHIT_GIT_BRANCH")
    return utils::current_git_branch();

  /* $LINENO reports the line of the command currently evaluating. It yields to
     a stored value above, so a script that assigns LINENO reads back what it
     set, matching dash. With no assignment it computes the line from the
     current source and position, which the command dispatcher keeps current. A
     run with no real source, such as an interactive single line, reports
     line 1. The first byte gates the compare so an ordinary name skips it. */
  if (first_byte == 'L' && name == "LINENO") {
    /* A windowed resolution maps the absolute position onto the definition
       copy and adds the defining file's line offset back, so LINENO in a
       function body reports the file line. */
    let const resolved = resolve_render_source(m_current_location);
    usize line = 1;
    if (resolved.text != nullptr) {
      const usize render_position = resolved.windowed
                                        ? m_current_location.position -
                                              resolved.body_start_position +
                                              resolved.header_length
                                        : m_current_location.position;
      line = utils::line_number_at(resolved.text->view(), render_position) +
             (resolved.windowed ? resolved.line_offset : 0);
    }
    return utils::uint_to_text(line);
  }

  /* The bash dynamic variables are computed on each read. They apply in every
     mood but POSIX, where these names stay ordinary so the mode behaves like
     dash. A stored assignment above still wins, so RANDOM=5 reads back 5. The
     first byte gates each compare so an ordinary name skips them. */
  if (bash_dynamic_variables_enabled()) {
    if (first_byte == 'R' && name == "RANDOM") {
      if (!m_random_seeded) {
        std::srand(static_cast<unsigned>(m_shell_start_time) ^
                   static_cast<unsigned>(os::get_shell_process_id()));
        m_random_seeded = true;
      }
      return utils::uint_to_text(static_cast<usize>(std::rand() % 32768));
    }
    if (first_byte == 'S' && name == "SECONDS") {
      return utils::int_to_text(static_cast<i64>(std::time(nullptr)) -
                                m_shell_start_time);
    }
    /* SHELLOPTS is the colon list of the enabled set -o options, the variable
       bash maintains and bash-completion greps for posix membership. The rows
       mirror the live option getters in bash's alphabetical order. */
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
      for (const shellopts_row &row : SHELLOPTS_ROWS) {
        if (!(this->*(row.get))()) continue;
        if (!joined.is_empty()) joined.push(':');
        joined.append(StringView{row.option_name});
      }
      return joined;
    }
    if (first_byte == 'E' && name == "EPOCHSECONDS") {
      return utils::int_to_text(static_cast<i64>(std::time(nullptr)));
    }
    /* EPOCHREALTIME is the wall clock as seconds.microseconds, the high
       resolution form a config such as ble.sh reads to build a clock. The
       fraction is always six digits so a reader can slice it by a fixed width.
     */
    if (first_byte == 'E' && name == "EPOCHREALTIME") {
      const u64 microseconds = os::realtime_microseconds();
      char fraction[8];
      std::snprintf(fraction, sizeof(fraction), "%06llu",
                    static_cast<unsigned long long>(microseconds % 1000000ULL));
      return utils::int_to_text(static_cast<i64>(microseconds / 1000000ULL)) +
             "." + StringView{fraction};
    }
    if (first_byte == 'B' && name == "BASHPID") {
      return utils::int_to_text(os::get_shell_process_id());
    }
    /* OSTYPE names the platform the way bash compiles it in, the read a
       config such as bash_completion branches on. */
    if (first_byte == 'O' && name == "OSTYPE") {
      return String{heap_allocator(), os::ostype_name()};
    }
    /* The subshell nesting level, zero at the top, the way bash reports it. */
    if (first_byte == 'B' && name == "BASH_SUBSHELL") {
      return String{heap_allocator(),
                    utils::int_to_text(static_cast<i64>(m_subshell_depth))};
    }
    /* The path of the file being sourced, the scalar form of BASH_SOURCE[0].
       The array form is backed by m_source_paths so ${BASH_SOURCE[0]} resolves
       through the ordinary subscript path. */
    /* The innermost function-call name, the scalar form of FUNCNAME[0]. The
       array forms route through the subscript and length specials. Outside a
       function the name is unset the way bash leaves it. */
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
      /* A script-file run roots the stack at the script itself, so outside
         any dot-source the scalar reads as $0, the way bash sets
         BASH_SOURCE[0] for an executed file. The envman style probe
         test "$BASH_SOURCE" == "$0" then takes its executed branch. */
      if (m_is_script_run) return String{heap_allocator(), m_shell_name.view()};
      return String{heap_allocator()};
    }
  }

  if (let const env = os::get_environment_variable(name))
    return String{heap_allocator(), env->view()};
  return shit::None;
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
  m_local_scopes.push(ArrayList<local_binding>{});
  LOG(verbosity::Debug, "entered function scope, local scope depth now %zu",
      m_local_scopes.count());
}

fn EvalContext::leave_function_scope() throws -> void
{
  if (m_local_scopes.is_empty()) return;

  /* Restore each shadowed binding in reverse, so a name declared local twice
     ends with the value it held before the function ran. */
  ASSERT(!m_local_scopes.is_empty());
  let &scope = m_local_scopes.back();
  LOG(verbosity::Debug, "leaving function scope, restoring %zu shadowed locals",
      scope.count());
  for (usize i = scope.count(); i > 0; i--) {
    ASSERT(i - 1 < scope.count());
    /* The shared restore puts back the scalar, the arrays, and the integer
       mark, the same steps the unset peel runs for a single binding. */
    restore_local_binding(scope[i - 1]);
  }
  /* The innermost scope is the one just restored, so it is dropped in place
     rather than rebuilding the whole stack into a fresh list on every return.
   */
  m_local_scopes.pop_back();
}

fn EvalContext::push_function_call_name(StringView name) throws -> void
{
  m_function_call_names.push(String{heap_allocator(), name});
}

fn EvalContext::pop_function_call_name() wontthrow -> void
{
  if (!m_function_call_names.is_empty())
    m_function_call_names.remove(m_function_call_names.count() - 1);
}

fn EvalContext::funcname_frame_count() const wontthrow -> usize
{
  if (m_function_call_names.is_empty()) return 0;
  return m_function_call_names.count() + m_sourced_file_frames +
         (m_is_script_run ? 1 : 0);
}

fn EvalContext::funcname_frame_at(usize index) const wontthrow -> StringView
{
  let const calls = m_function_call_names.count();
  if (index < calls) return m_function_call_names[calls - 1 - index].view();
  if (index < calls + m_sourced_file_frames) return StringView{"source"};
  return StringView{"main"};
}

pure fn EvalContext::in_function_scope() const wontthrow -> bool
{
  return !m_local_scopes.is_empty();
}

fn EvalContext::is_local_in_current_scope(StringView name) const wontthrow
    -> bool
{
  if (m_local_scopes.is_empty()) return false;
  for (const local_binding &binding : m_local_scopes.back())
    if (binding.name.view() == name) return true;
  return false;
}

fn EvalContext::set_alias(StringView name, StringView value) throws -> void
{
  LOG(verbosity::All, "setting alias '%.*s' to a %zu byte value",
      static_cast<int>(name.length), name.data, value.length);
  m_aliases.set(name, value);
}

fn EvalContext::remove_alias(StringView name) throws -> bool
{
  if (m_aliases.find(name) == nullptr) return false;
  LOG(verbosity::All, "removing alias '%.*s'", static_cast<int>(name.length),
      name.data);
  m_aliases.erase(name);
  return true;
}

fn EvalContext::get_alias(StringView name) const throws -> Maybe<String>
{
  if (let const *value = m_aliases.find(name))
    return String{heap_allocator(), value->view()};
  return None;
}

fn EvalContext::alias_definitions() const throws -> ArrayList<String>
{
  let out = ArrayList<String>{};
  m_aliases.for_each([&out](StringView key, const String &value) {
    let definition = String{heap_allocator(), key};
    definition.append(StringView{"='", 2});
    definition.append(value);
    definition.push('\'');
    out.push(steal(definition));
  });
  utils::sort_ascending(out);
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
  LOG(verbosity::Debug, "entered a subshell, depth now %zu", m_subshell_depth);
}

fn EvalContext::leave_subshell() wontthrow -> void
{
  ASSERT(m_subshell_depth > 0);
  /* A bare exec inside this subshell moved real descriptors. The backups put
     them back the way a forked subshell's exit would, newest first so
     stacked moves unwind in order. */
  while (!m_subshell_saved_descriptors.is_empty() &&
         m_subshell_saved_descriptors.back().depth == m_subshell_depth)
  {
    LOG(verbosity::Debug,
        "restoring descriptor %d a subshell exec moved at depth %zu",
        m_subshell_saved_descriptors.back().saved.shell_fd, m_subshell_depth);
    os::restore_descriptor(m_subshell_saved_descriptors.back().saved);
    m_subshell_saved_descriptors.remove(m_subshell_saved_descriptors.count() -
                                        1);
  }
  m_subshell_depth--;
  LOG(verbosity::Debug, "left a subshell, depth now %zu", m_subshell_depth);
}

fn EvalContext::snapshot_subshell_descriptor(i32 shell_fd) throws -> void
{
  if (m_subshell_depth == 0) return;
  for (const subshell_saved_descriptor &entry : m_subshell_saved_descriptors)
    if (entry.depth == m_subshell_depth && entry.saved.shell_fd == shell_fd)
      return;
  LOG(verbosity::Debug,
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
    LOG(verbosity::Debug, "break requested outside a loop, ignored");
    return;
  }
  if (static_cast<usize>(level) > m_loop_depth)
    level = static_cast<i64>(m_loop_depth);
  LOG(verbosity::All, "break requested, level %lld of depth %zu",
      (long long) level, m_loop_depth);
  m_control_flow = control_flow{control_flow::Kind::Break, level, location,
                                m_current_source, String{m_current_origin}};
}

fn EvalContext::request_continue(i64 level, SourceLocation location) throws
    -> void
{
  /* A continue with no enclosing loop is a no-op, and a level past the nesting
     clamps to the outermost loop, so no leftover level escapes as an error. */
  if (m_loop_depth == 0) {
    LOG(verbosity::Debug, "continue requested outside a loop, ignored");
    return;
  }
  if (static_cast<usize>(level) > m_loop_depth)
    level = static_cast<i64>(m_loop_depth);
  LOG(verbosity::All, "continue requested, level %lld of depth %zu",
      (long long) level, m_loop_depth);
  m_control_flow = control_flow{control_flow::Kind::Continue, level, location,
                                m_current_source, String{m_current_origin}};
}

fn EvalContext::request_return(i64 status, SourceLocation location) throws
    -> void
{
  LOG(verbosity::Debug, "return requested, status %lld", (long long) status);
  m_control_flow = control_flow{control_flow::Kind::Return, status, location,
                                m_current_source, String{m_current_origin}};
}

fn EvalContext::request_exit(i64 status, SourceLocation location) throws -> void
{
  LOG(verbosity::Debug, "exit requested, status %lld", (long long) status);
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

fn EvalContext::print_source_backtrace() const throws -> void
{
  for (usize i = m_source_frames.count(); i > 0; i--) {
    const source_frame &frame = m_source_frames[i - 1];
    if (frame.parent_source != nullptr) {
      /* A frame is context under the primary error, not an error of its own, so
         it prints with the Trace severity rather than Error. */
      let const sourced_here = TraceWithLocation{frame.call_site};
      show_message(sourced_here.to_string(*frame.parent_source));
    } else {
      /* The origin line is context under the primary error, so it carries the
         note severity word rather than printing bare. */
      show_message(Note{"This error was raised while running " + frame.origin}
                       .to_string());
    }
  }
}

fn EvalContext::set_current_location(SourceLocation location) wontthrow -> void
{
  m_current_location = location;
}

/* A cap on nested dot-source and eval runs. A configure script nests at most a
   handful of source levels, so the cap sits far above any legitimate depth yet
   below the point where the native call stack would overflow first, since each
   level spends many native frames between run_source calls. A run that crosses
   it is a runaway that would otherwise exhaust memory. */
static constexpr usize MAX_SOURCE_DEPTH = 400;
/* A separate, larger cap on nested function calls, since deep but finite
   recursion in a real script is more common than deep sourcing. It too stays
   below the native stack overflow point, which a function call reaches at a
   greater depth because it spends fewer native frames per level. */
static constexpr usize MAX_FUNCTION_CALL_DEPTH = 900;

fn EvalContext::enter_source(SourceLocation location) throws -> void
{
  if (m_source_depth >= MAX_SOURCE_DEPTH) {
    LOG(verbosity::Debug, "source depth %zu exceeds cap %zu", m_source_depth,
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
    LOG(verbosity::Debug, "function call depth %zu exceeds cap %zu",
        m_function_call_depth, MAX_FUNCTION_CALL_DEPTH);
    throw ErrorWithLocation{location,
                            "Maximum source/recursion depth exceeded"};
  }
  m_function_call_depth++;
  LOG(verbosity::Debug, "entered function call depth %zu",
      m_function_call_depth);
}

fn EvalContext::leave_function_call() wontthrow -> void
{
  ASSERT(m_function_call_depth > 0);
  m_function_call_depth--;
}

fn EvalContext::set_error_exit(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the errexit option flips to %s",
      enabled ? "on" : "off");
  m_error_exit = enabled;
}

pure fn EvalContext::error_exit() const wontthrow -> bool
{
  return m_error_exit;
}

fn EvalContext::set_echo_expanded(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the xtrace option flips to %s", enabled ? "on" : "off");
  m_enable_echo_expanded = enabled;
}

fn EvalContext::set_error_unset(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the nounset option flips to %s",
      enabled ? "on" : "off");
  m_error_unset = enabled;
}

pure fn EvalContext::error_unset() const wontthrow -> bool
{
  return m_error_unset;
}

fn EvalContext::set_pipefail(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the pipefail option flips to %s",
      enabled ? "on" : "off");
  m_pipefail = enabled;
}

pure fn EvalContext::pipefail() const wontthrow -> bool { return m_pipefail; }

fn EvalContext::set_no_clobber(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the noclobber option flips to %s",
      enabled ? "on" : "off");
  m_no_clobber = enabled;
}

pure fn EvalContext::no_clobber() const wontthrow -> bool
{
  return m_no_clobber;
}

fn EvalContext::set_export_all(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the allexport option flips to %s",
      enabled ? "on" : "off");
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
  LOG(verbosity::Info, "the noglob option flips to %s", enabled ? "on" : "off");
  m_enable_path_expansion = !enabled;
}

pure fn EvalContext::no_glob() const wontthrow -> bool
{
  return !m_enable_path_expansion;
}

fn EvalContext::set_no_exec(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the noexec option flips to %s", enabled ? "on" : "off");
  m_no_exec = enabled;
}

pure fn EvalContext::no_exec() const wontthrow -> bool { return m_no_exec; }

fn EvalContext::set_failglob(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the failglob option flips to %s",
      enabled ? "on" : "off");
  m_failglob = enabled;
}

pure fn EvalContext::failglob() const wontthrow -> bool { return m_failglob; }

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
  let assignments = ArrayList<String>{};
  assignments.reserve(m_shell_variables.count());
  m_shell_variables.for_each([&](StringView name, const String &value) {
    let entry = String{heap_allocator(), name};
    entry.push('=');
    entry.append(value);
    assignments.push(steal(entry));
  });
  utils::sort_ascending(assignments);
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
                             m_error_exit,
                             m_enable_path_expansion,
                             m_enable_echo,
                             m_enable_echo_expanded,
                             m_environment_undo_log.count()};
}

fn EvalContext::restore_state(eval_state_snapshot snapshot) throws -> void
{
  LOG(verbosity::Debug,
      "restoring the evaluator state after a subshell or substitution");
  m_shell_variables = steal(snapshot.shell_variables);
  m_functions = steal(snapshot.functions);
  m_function_sources = steal(snapshot.function_sources);
  m_function_definition_infos = steal(snapshot.function_definition_infos);
  /* An alias defined or removed inside a subshell or a command substitution
     stays inside it, the way bash isolates an alias change in a subshell. */
  m_aliases = steal(snapshot.aliases);
  m_positional_params = steal(snapshot.positional_params);

  /* A set -e, set -f, or set -x inside a subshell or a command substitution
     stays inside it, the way dash runs the inner code in a forked child whose
     option flags die with the child. */
  m_error_exit = snapshot.error_exit;
  m_enable_path_expansion = snapshot.enable_path_expansion;
  m_enable_echo = snapshot.enable_echo;
  m_enable_echo_expanded = snapshot.enable_echo_expanded;

  /* A trap installed inside the subshell stays inside it. The parent's table is
     restored whole, so an EXIT trap the parent set survives and an EXIT trap
     the subshell set is dropped after it has already fired at the subshell's
     end. A signal the subshell trapped that the parent does not is returned to
     its default first, then the parent's own signal dispositions are
     reinstalled from the restored table, so the process matches it. */
  /* The common subshell and command substitution traps nothing, so the two
     full table scans below are skipped entirely when neither the inner nor the
     restored table holds an entry. */
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

  /* set_current_directory reports an error through ErrorOr, ignored here to
     match the prior void call that swallowed a failed chdir. */
  (void) Path::set_current_directory(snapshot.working_directory);

  /* An export, an unset, or a reassignment of an exported name inside the
     subshell wrote the process environment, which the restored variable map
     above does not cover. Each such write logged the name's prior value while
     the subshell ran, so they revert newest first back to the mark the snapshot
     recorded, the way a forked subshell's environment dies with it. A subshell
     that wrote no exported name logged nothing, so the count already matches
     the mark and the loop does not run. The rewind precedes the PATH re-point
     below, so an exported PATH reads its restored value. */
  LOG(verbosity::Debug,
      "rewinding %zu environment writes made inside the subshell",
      m_environment_undo_log.count() - snapshot.environment_undo_mark);
  while (m_environment_undo_log.count() > snapshot.environment_undo_mark) {
    const environment_undo_entry &entry = m_environment_undo_log.back();
    if (entry.previous_value)
      os::set_environment_variable(entry.name.view(),
                                   entry.previous_value->view());
    else
      os::unset_environment_variable(entry.name.view());
    /* The exported set follows the rewind, so a name the subshell created is
       dropped and a name it merely changed stays exported. */
    sync_exported_after_restore(entry.name.view(),
                                entry.previous_value.has_value());
    m_environment_undo_log.pop_back();
  }

  /* The cached field separators track the restored map, so an IFS change inside
     the subshell or the command substitution does not leak its split behavior
     to the parent. */
  if (let const *ifs = m_shell_variables.find(StringView{"IFS", 3}))
    set_field_separators(ifs->view());
  else
    set_field_separators(" \t\n");

  /* The resolver reads a process-global PATH rather than the restored map, so a
     PATH change inside the subshell or the command substitution would leak its
     search order to the parent. The search is re-pointed at the restored PATH,
     read as None when the snapshot held no PATH so the resolver falls back the
     way an unset PATH does. */
  utils::set_path_for_resolution(get_variable_value("PATH"));

  /* The exit status is intentionally not restored. A subshell and a command
     substitution propagate the status of their last command to the parent. */
}

fn EvalContext::option_flags_string() const throws -> String
{
  let flags = String{};
  if (m_error_exit) flags += 'e';
  if (!m_enable_path_expansion) flags += 'f';
  if (m_enable_echo) flags += 'v';
  if (m_enable_echo_expanded) flags += 'x';
  if (m_shell_is_interactive) flags += 'i';
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
  LOG(verbosity::All, "applying the indirect expansion '${!%.*s}'",
      static_cast<int>(body.length), body.data);
  if (body.is_empty()) return String{scratch_allocator()};

  /* ${!a[@]} and ${!a[*]} list the subscripts of an array, zero through the
     last index, the way bash enumerates its keys. */
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
    /* List the variable names that start with the prefix, deduplicated, sorted,
       and joined with a space the way bash renders ${!prefix*}. The quoted
       "${!prefix@}" per-name field form is produced in the field-expansion path
       instead, since this string return cannot carry the field boundaries. */
    const StringView prefix = body.substring_of_length(0, body.length - 1);
    let const names = matching_prefix_names(prefix);
    let out = String{scratch_allocator()};
    for (usize i = 0; i < names.count(); i++) {
      if (i > 0) out.push(' ');
      out.append(names[i].view());
    }
    return out;
  }

  /* The indirect form reads body, takes its value as a variable name, and
     expands that variable. */
  const Maybe<String> target = get_variable_value(body);
  if (!target.has_value()) {
    if (m_error_unset)
      throw_script_fatal("Unable to expand '" + body +
                         "' because the parameter is not set");
    return String{scratch_allocator()};
  }
  /* A target naming an array element, the a[1] a reference variable holds,
     routes through the subscript read the way bash dereferences it. */
  let const target_view = target->view();
  if (let const bracket = target_view.find_character('[');
      bracket.has_value() && !target_view.is_empty() &&
      target_view[target_view.length - 1] == ']')
    return apply_array_subscript(
        target_view.substring_of_length(0, *bracket),
        target_view.substring_of_length(*bracket + 1,
                                        target_view.length - *bracket - 2));
  if (!get_variable_value(target_view).has_value())
    report_unset_reference(*target);
  return expand_variable(target_view);
}

cold fn EvalContext::make_stats_string() const throws -> String
{
  let s = String{};

  /* Stats print before end_command runs the per-command rollup, so the live
     arena is sampled here and the current command is counted as one beyond the
     completed total. */
  const usize live_ast_arena_bytes =
      AST_ARENA != nullptr ? AST_ARENA->bytes_used() : 0;
  const usize peak_ast_arena_bytes =
      live_ast_arena_bytes > m_peak_ast_arena_bytes ? live_ast_arena_bytes
                                                    : m_peak_ast_arena_bytes;

  s += "[Stats\n";

  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Commands evaluated: " + utils::uint_to_text(m_commands_evaluated + 1);
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Expansions: " + utils::uint_to_text(last_expansion_count());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Nodes evaluated: " + utils::uint_to_text(last_expressions_executed());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total expansions: " + utils::uint_to_text(total_expansion_count());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total nodes evaluated: " +
       utils::uint_to_text(total_expressions_executed());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "AST arena bytes: " + utils::uint_to_text(live_ast_arena_bytes);
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Peak AST arena bytes: " + utils::uint_to_text(peak_ast_arena_bytes);
  s += '\n';

  s += "]";

  return s;
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
  if (in_fd) {
    os::close_fd(*in_fd);
    in_fd.reset();
  }
  if (out_fd) {
    os::close_fd(*out_fd);
    out_fd.reset();
  }
  if (err_fd) {
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

fn ExecContext::make_from(SourceLocation location,
                          ArrayList<String> &&args) throws -> ExecContext
{
  ASSERT(args.count() > 0);

  let const &program = args[0];

  Maybe<Builtin::Kind> bk;
  Maybe<Path> p;

  if (!program.find_character('/').has_value()) {
    bk = search_builtin(program.view());

    if (!bk) {
      let ps = utils::search_program_path(program.view());
      if (ps.count() > 0) p = steal(ps[0]);
    }
  } else {
    /* canonicalize_path already tries the omitted suffixes, so a path-given
       program resolves its extension the way the PATH search does. */
    p = utils::canonicalize_path(program.view());
  }

  /* Builtins take precedence over programs. */
  ResolvedCommand kind;
  if (!bk) {
    if (p.has_value()) {
      LOG(verbosity::Debug, "resolved '%s' to the program '%s'",
          program.c_str(), p->text().c_str());
      kind = ResolvedCommand::from_program(steal(*p));
    } else {
      LOG(verbosity::Debug, "no builtin or program matches '%s'",
          program.c_str());
      /* A close builtin or PATH program is offered as a did-you-mean hint, so a
         typo such as gti points at git. */
      String message = "Program '" + program + "' wasn't found";
      if (Maybe<String> suggestion =
              utils::suggest_command(program.view(), ArrayList<String>{}))
      {
        message += ", did you mean '" + *suggestion + "'?";
      }
      throw CommandNotFound{location, steal(message)};
    }
  } else {
    LOG(verbosity::Debug, "resolved '%s' to a builtin", program.c_str());
    kind = ResolvedCommand::from_builtin(*bk);
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
  /* The stage never runs, so one placeholder argument satisfies the
     constructor's invariant that a context carries at least argv[0]. */
  let args = ArrayList<String>{};
  args.push(String{});
  return {location, ResolvedCommand::from_unresolved(), steal(args)};
}

} /* namespace shit */
