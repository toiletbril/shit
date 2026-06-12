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
  LOG(verbosity::Info, "clearing the bash identity variables for a non-bash mood");
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
  /* The sparse elements drop first, so a deep index the body set does not
     survive the restore the way the plain unset clears it. */
  clear_sparse_array(binding.name.view());
  if (binding.previous_indexed_array.has_value())
    m_indexed_arrays.set(binding.name.view(),
                         steal(*binding.previous_indexed_array));
  else
    m_indexed_arrays.erase(binding.name.view());
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
    LOG(verbosity::All,
        "appending %zu elements to the existing array '%.*s'", values.count(),
        static_cast<int>(name.length), name.data);
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
   throws carry the script-fatal mark. */
[[noreturn]] static fn throw_script_fatal(String message) throws -> void
{
  let error = Error{message.view()};
  error.set_script_fatal();
  throw error;
}

cold fn EvalContext::show_runtime_warning(StringView message) wontthrow -> void
{
  /* no-diagnostics promises no warnings at all, so the runtime advisories
     honor the live toggle the way the analysis stage does. */
  if (diagnostics_disabled()) return;
  /* The command being evaluated locates the caret, the same position the reap
     and substitution warnings render at. A formatting failure is swallowed so
     a diagnostic never becomes an error. */
  try {
    let const location = SourceLocation{m_current_location_position, 1};
    show_message(m_current_source != nullptr
                     ? WarningWithLocation{location, message}.to_string(
                           m_current_source->view())
                     : Warning{message}.to_string());
  } catch (...) {
    LOG(verbosity::Debug,
        "formatting a runtime warning failed, the error is swallowed");
  }
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
    show_runtime_warning("the variable '" + String{name} +
                         "' is not set, it expands to empty, which is rarely "
                         "intended");
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
static fn sparse_array_key(StringView name, usize index,
                           Allocator allocator) throws -> String
{
  let key = String{allocator, name};
  key.push('\x01');
  key.append(utils::uint_to_text(index).view());
  return key;
}

/* One sparse element, its index and its value, used to merge the sparse map
   into the dense run when an array is enumerated. */
struct sparse_array_entry
{
  usize index;
  String value;
};

/* Every sparsely-held element of an array, sorted by ascending index. The
   sparse indices always sit beyond the dense run, so appending these after the
   dense elements yields the whole array in index order. */
static fn collect_sparse_array_entries(const StringMap<String> &sparse,
                                       StringView name,
                                       Allocator allocator) throws
    -> ArrayList<sparse_array_entry>
{
  let out = ArrayList<sparse_array_entry>{allocator};
  let const prefix = sparse_array_key(name, 0, allocator);
  /* The prefix is name plus the separator byte, so the leading "0" of the key
     for index zero is dropped to leave just the name and separator. */
  let const name_prefix = prefix.view().substring_of_length(0, name.length + 1);
  sparse.for_each([&](StringView key, const String &value) throws {
    if (key.length <= name_prefix.length ||
        key.substring_of_length(0, name_prefix.length) != name_prefix)
      return;
    let const index_text = key.substring(name_prefix.length);
    if (let const parsed = utils::parse_decimal_integer(index_text);
        !parsed.is_error() && parsed.value() >= 0)
      out.push(sparse_array_entry{
          static_cast<usize>(parsed.value()), String{allocator, value.view()}
      });
  });
  /* An insertion sort keeps it simple, since a sparse array holds few far
     elements. */
  for (usize i = 1; i < out.count(); i++) {
    let key = steal(out[i]);
    usize j = i;
    while (j > 0 && out[j - 1].index > key.index) {
      out[j] = steal(out[j - 1]);
      j--;
    }
    out[j] = steal(key);
  }
  return out;
}

fn EvalContext::clear_sparse_array(StringView name) throws -> void
{
  let const entries = collect_sparse_array_entries(m_sparse_array_values, name,
                                                   scratch_allocator());
  for (const sparse_array_entry &entry : entries)
    m_sparse_array_values.erase(
        sparse_array_key(name, entry.index, scratch_allocator()).view());
}

/* Whether an array-literal element is the explicit [index]=value form, and if
   so its subscript text and its value. A leading '[' with a later "]=" marks
   it, every other element is a positional value. */
static fn parse_explicit_array_index(StringView element,
                                     StringView &subscript_out,
                                     StringView &value_out) wontthrow -> bool
{
  if (element.length < 3 || element[0] != '[') return false;
  for (usize i = 1; i + 1 < element.length; i++) {
    if (element[i] == ']' && element[i + 1] == '=') {
      subscript_out = element.substring_of_length(1, i - 1);
      value_out = element.substring(i + 2);
      return true;
    }
  }
  return false;
}

fn EvalContext::assign_indexed_array_elements(StringView name,
                                              ArrayList<String> elements,
                                              bool is_append) throws -> void
{
  /* POSIX mode has no arrays, but a sourced profile that carries a bash array
     literal should keep sourcing, so the assignment stands in as an empty
     scalar rather than a syntax error, the shim the parser used to apply at
     parse time for every non-bash mood. */
  if (is_posix_mode()) [[unlikely]] {
    LOG(verbosity::Debug,
        "posix mode stores the array literal for '%.*s' as an empty scalar",
        static_cast<int>(name.length), name.data);
    set_shell_variable(name, "");
    return;
  }

  usize running_index = 0;
  if (is_append) {
    /* An append continues after the highest set index, the dense end or the
       last sparse element, whichever is larger. */
    if (let const *array = lookup_indexed_array(name))
      running_index = array->count();
    let const sparse = collect_sparse_array_entries(m_sparse_array_values, name,
                                                    scratch_allocator());
    if (!sparse.is_empty()) {
      let const next_after_sparse = sparse[sparse.count() - 1].index + 1;
      if (next_after_sparse > running_index) running_index = next_after_sparse;
    }
  } else {
    /* A plain assignment replaces the array, clearing the dense run, the sparse
       elements, and any scalar of the same name. */
    set_indexed_array(name, ArrayList<String>{heap_allocator()});
  }

  for (const String &element : elements) {
    StringView subscript;
    StringView value;
    usize index = running_index;
    if (parse_explicit_array_index(element.view(), subscript, value)) {
      index = static_cast<usize>(evaluate_arithmetic(subscript));
      set_array_element(name, index, value);
    } else {
      set_array_element(name, index, element.view());
    }
    running_index = index + 1;
  }
}

fn EvalContext::set_array_element(StringView name, usize index,
                                  StringView value) throws -> void
{
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  /* The dense run holds the contiguous prefix from index zero, and any element
     past its end lives in the sparse map keyed by index, the way bash keeps an
     indexed array without padding a gap. An empty dense array still marks the
     name as indexed so a read routes here. A first write promotes an existing
     scalar to element zero. */
  ArrayList<String> *dense = m_indexed_arrays.find(name);
  if (dense == nullptr) {
    let elements = ArrayList<String>{heap_allocator()};
    if (let const *scalar = m_shell_variables.find(name))
      elements.push(String{heap_allocator(), scalar->view()});
    set_indexed_array(name, steal(elements));
    dense = m_indexed_arrays.find(name);
  }
  m_shell_variables.erase(name);
  ASSERT(dense != nullptr);

  const usize count = dense->count();
  if (index < count) {
    (*dense)[index] = String{heap_allocator(), value};
    return;
  }
  if (index == count) {
    /* The write extends the contiguous run, which may make an earlier sparse
       element contiguous too, so any element now at the run's end migrates from
       the sparse map into the dense run. */
    dense->push(String{heap_allocator(), value});
    for (;;) {
      let const key =
          sparse_array_key(name, dense->count(), scratch_allocator());
      let const *migrated = m_sparse_array_values.find(key.view());
      if (migrated == nullptr) break;
      dense->push(String{heap_allocator(), migrated->view()});
      m_sparse_array_values.erase(key.view());
    }
    return;
  }
  /* A write past the run's end leaves a gap, so it is held sparsely. */
  LOG(verbosity::All,
      "holding element %zu of '%.*s' sparsely past the dense run of %zu",
      index, static_cast<int>(name.length), name.data, count);
  m_sparse_array_values.set(
      sparse_array_key(name, index, scratch_allocator()).view(), value);
}

/* The flat-map key for an associative element, the array name and the element
   key joined by a byte that does not occur in a name. The map copies a key it
   stores, so the callers build this on the per-command scratch arena. */
static fn associative_composite_key(StringView name, StringView key,
                                    Allocator allocator) throws -> String
{
  let composite = String{allocator, name};
  composite.push('\x01');
  composite.append(key);
  return composite;
}

fn EvalContext::assign_array_element(StringView name, StringView subscript,
                                     StringView value, bool is_append) throws
    -> void
{
  LOG(verbosity::All, "assigning the array element '%.*s[%.*s]'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(subscript.length), subscript.data);
  if (is_readonly(name))
    throw Error{"Unable to assign '" + name + "' because it is read only"};

  /* An integer-marked name evaluates the element text as arithmetic and an
     append adds the prior element's evaluation, the same treatment
     set_shell_variable gives a scalar. The joined text lives on the scratch
     arena and the stores below copy the decimal result. */
  char integer_result[24];
  auto integer_element_value = [&](Maybe<String> existing)
                                   throws -> StringView {
    let joined = String{scratch_allocator()};
    if (is_append) {
      if (existing.has_value()) joined.append(existing->view());
      append_integer_expression(joined, value);
    } else {
      joined.append(value);
    }
    return utils::int_to_text_into(
        joined.is_empty() ? 0 : evaluate_arithmetic(joined.view()),
        integer_result, sizeof(integer_result));
  };

  if (is_associative_array(name)) {
    const String key = expand_modifier_word(subscript);
    if (is_integer_variable(name)) [[unlikely]] {
      set_associative_element(
          name, key.view(),
          integer_element_value(lookup_associative_element(name, key.view())));
      return;
    }
    if (is_append) {
      let combined = String{
          lookup_associative_element(name, key.view()).value_or(String{})};
      combined += value;
      set_associative_element(name, key.view(), combined.view());
    } else {
      set_associative_element(name, key.view(), value);
    }
    return;
  }

  i64 index = evaluate_arithmetic(subscript);
  if (index < 0) {
    if (let const *array = lookup_indexed_array(name))
      index += static_cast<i64>(array->count());
  }
  if (index < 0)
    throw Error{"Unable to index '" + name +
                "' because the array subscript is invalid"};

  if (is_integer_variable(name)) [[unlikely]] {
    let existing = Maybe<String>{};
    if (is_append)
      if (let const *array = lookup_indexed_array(name))
        if (static_cast<usize>(index) < array->count())
          existing = String{(*array)[static_cast<usize>(index)].view()};
    set_array_element(name, static_cast<usize>(index),
                      integer_element_value(steal(existing)));
    return;
  }

  /* The element text is transient, copied by set_array_element, so it lives
     on the per-command scratch arena. */
  let element = String{scratch_allocator(), value};
  if (is_append) {
    let combined = String{scratch_allocator()};
    if (let const *array = lookup_indexed_array(name))
      if (static_cast<usize>(index) < array->count())
        combined = String{(*array)[static_cast<usize>(index)].view()};
    combined += value;
    element = steal(combined);
  }
  set_array_element(name, static_cast<usize>(index), element.view());
}

fn EvalContext::declare_associative_array(StringView name) throws -> void
{
  LOG(verbosity::Debug, "declaring '%.*s' as an associative array",
      static_cast<int>(name.length), name.data);
  m_associative_names.add(name);
  m_shell_variables.erase(name);
}

fn EvalContext::set_associative_element(StringView name, StringView key,
                                        StringView value) throws -> void
{
  m_associative_names.add(name);
  m_shell_variables.erase(name);
  m_associative_values.set(
      associative_composite_key(name, key, scratch_allocator()).view(), value);
}

fn EvalContext::lookup_associative_element(StringView name,
                                           StringView key) const throws
    -> Maybe<String>
{
  if (let const *value = m_associative_values.find(
          associative_composite_key(name, key, scratch_allocator()).view()))
    return *value;
  return None;
}

fn EvalContext::associative_keys(StringView name) const throws
    -> ArrayList<String>
{
  let keys = ArrayList<String>{heap_allocator()};
  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  m_associative_values.for_each([&](StringView composite, const String &value) {
    unused(value);
    if (composite.length >= prefix.count() &&
        composite.substring_of_length(0, prefix.count()) == prefix.view())
      keys.push_managed(composite.substring(prefix.count()));
  });
  return keys;
}

fn EvalContext::associative_values(StringView name) const throws
    -> ArrayList<String>
{
  let values = ArrayList<String>{heap_allocator()};
  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  m_associative_values.for_each([&](StringView composite, const String &value) {
    if (composite.length >= prefix.count() &&
        composite.substring_of_length(0, prefix.count()) == prefix.view())
      values.push_managed(value.view());
  });
  return values;
}

fn EvalContext::clear_associative_array(StringView name) throws -> void
{
  if (!is_associative_array(name)) return;
  /* The composite keys are collected before erasing, since removing entries
     while iterating the value map would be unsafe. */
  const String prefix =
      associative_composite_key(name, "", scratch_allocator());
  let to_erase = ArrayList<String>{heap_allocator()};
  m_associative_values.for_each([&](StringView composite, const String &) {
    if (composite.length >= prefix.count() &&
        composite.substring_of_length(0, prefix.count()) == prefix.view())
      to_erase.push_managed(composite);
  });
  for (const String &composite : to_erase)
    m_associative_values.erase(composite.view());
  m_associative_names.remove(name);
}

fn EvalContext::unset_array_element(StringView name,
                                    StringView subscript) throws -> void
{
  LOG(verbosity::All, "unsetting the array element '%.*s[%.*s]'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(subscript.length), subscript.data);
  if (is_readonly(name))
    throw Error{"Unable to unset '" + name + "' because it is read only"};

  if (is_associative_array(name)) {
    m_associative_values.erase(
        associative_composite_key(name, subscript, scratch_allocator()).view());
    return;
  }

  if (ArrayList<String> *array = m_indexed_arrays.find(name)) {
    const i64 index = evaluate_arithmetic(subscript);
    const i64 count = static_cast<i64>(array->count());
    const i64 resolved = index < 0 ? index + count : index;
    if (resolved < 0) return;
    /* An element inside the dense run leaves a hole at its index the way bash
       does, rather than renumbering the tail. The elements after it move to the
       sparse store under their original indices and the dense run is dropped
       from the removed index on. An element past the dense run already lives in
       the sparse store and is erased by its key. Erasing an absent key is a
       no-op, matching bash unsetting a missing element silently. */
    if (resolved < count) {
      for (usize i = static_cast<usize>(resolved) + 1;
           i < static_cast<usize>(count); i++)
        m_sparse_array_values.set(
            sparse_array_key(name, i, scratch_allocator()).view(),
            (*array)[i].view());
      while (array->count() > static_cast<usize>(resolved))
        array->remove(array->count() - 1);
    } else {
      m_sparse_array_values.erase(sparse_array_key(name,
                                                   static_cast<usize>(resolved),
                                                   scratch_allocator())
                                      .view());
    }
  }
}

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

  /* $LINENO reports the line of the command currently evaluating. It yields to
     a stored value above, so a script that assigns LINENO reads back what it
     set, matching dash. With no assignment it computes the line from the
     current source and position, which the command dispatcher keeps current. A
     run with no real source, such as an interactive single line, reports
     line 1. The first byte gates the compare so an ordinary name skips it. */
  if (first_byte == 'L' && name == "LINENO") {
    const usize line = m_current_source != nullptr
                           ? utils::line_number_at(m_current_source->view(),
                                                   m_current_location_position)
                           : 1;
    return utils::uint_to_text(line);
  }

  /* The bash dynamic variables are computed on each read. They only apply in
     bash mode so POSIX behaves like dash, where these names are ordinary. A
     stored assignment above still wins, so RANDOM=5 reads back 5. The first
     byte gates each compare so an ordinary name skips them. */
  if (is_bash_compatible()) {
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
          {"allexport", &EvalContext::export_all},
          {"errexit", &EvalContext::error_exit},
          {"failglob", &EvalContext::failglob},
          {"monitor", &EvalContext::monitor},
          {"noclobber", &EvalContext::no_clobber},
          {"noexec", &EvalContext::no_exec},
          {"noglob", &EvalContext::no_glob},
          {"nounset", &EvalContext::error_unset},
          {"pipefail", &EvalContext::pipefail},
          {"posix", &EvalContext::is_posix_mode},
          {"verbose", &EvalContext::should_echo},
          {"xtrace", &EvalContext::should_echo_expanded},
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

fn EvalContext::set_last_background_pid(i64 pid) wontthrow -> void
{
  m_last_background_pid = pid;
}

fn EvalContext::register_job(os::process pid, StringView command) throws -> i32
{
  let new_job = job{};
  new_job.id = m_next_job_id++;
  new_job.pid = pid;
  new_job.command = command;
  new_job.state = job::State::Running;
  m_jobs.push(steal(new_job));
  ASSERT(!m_jobs.is_empty());
  LOG(verbosity::Info, "registered job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn EvalContext::update_jobs() throws -> void
{
  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;

    i32 status = 0;
    let const state = os::poll_process(job.pid, status);
    switch (state) {
    case os::process_state::Exited:
      LOG(verbosity::Info, "job %d finished with status %d", job.id, status);
      job.state = job::State::Done;
      job.last_status = status;
      break;
    case os::process_state::Stopped: job.state = job::State::Stopped; break;
    default: job.state = job::State::Running; break;
    }
  }
}

fn EvalContext::jobs() wontthrow -> ArrayList<job> & { return m_jobs; }

fn EvalContext::find_job(i32 id) wontthrow -> job *
{
  for (job &job : m_jobs)
    if (job.id == id) return &job;
  return nullptr;
}

fn EvalContext::most_recent_job() wontthrow -> job *
{
  /* Skip a finished job, so a bare fg or bg acts on a job that is still
     running or stopped rather than a dead pid. */
  for (usize i = m_jobs.count(); i > 0; i--) {
    ASSERT(i - 1 < m_jobs.count());
    if (m_jobs[i - 1].state != job::State::Done) return &m_jobs[i - 1];
  }
  return nullptr;
}

fn EvalContext::forget_done_jobs() throws -> void
{
  let kept = ArrayList<job>{};
  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;
    kept.push(steal(job));
  }
  LOG(verbosity::Debug, "dropping finished jobs, keeping %zu of %zu",
      kept.count(), m_jobs.count());
  m_jobs = steal(kept);
}

fn EvalContext::notify_done_jobs() throws -> void
{
  update_jobs();

  for (usize i = 0; i < m_jobs.count(); i++) {
    const job &job = m_jobs[i];
    if (job.state != job::State::Done) continue;

    /* The bash current-job marker, '+' for the last entry and '-' for the one
       before it, otherwise a space. */
    char marker = ' ';
    if (i == m_jobs.count() - 1)
      marker = '+';
    else if (m_jobs.count() >= 2 && i == m_jobs.count() - 2)
      marker = '-';

    let line = String{};
    line += "[" + utils::int_to_text(job.id) + "]";
    line.push(marker);
    line += " Done  ";
    line += job.command.c_str();
    line.push('\n');
    print_error(line);
  }

  forget_done_jobs();
}

fn EvalContext::set_monitor(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the monitor option flips to %s",
      enabled ? "on" : "off");
  m_monitor = enabled;
}

pure fn EvalContext::monitor() const wontthrow -> bool { return m_monitor; }

fn EvalContext::register_function(StringView name, const Expression *body,
                                  StringView definition_text) throws -> void
{
  LOG(verbosity::Info, "registering function '%.*s' with a %zu byte definition",
      static_cast<int>(name.length), name.data, definition_text.length);
  m_functions.set(name, body);
  m_function_sources.set(name, definition_text);
}

fn EvalContext::find_function_source(StringView name) const wontthrow
    -> const String *
{
  return m_function_sources.find(name);
}

fn EvalContext::sorted_function_names() const throws -> ArrayList<String>
{
  let out = ArrayList<String>{};
  out.reserve(m_functions.count());
  m_functions.for_each(
      [&](StringView name, const Expression *) { out.push_managed(name); });
  utils::sort_ascending(out);
  return out;
}

fn EvalContext::find_function(StringView name) const wontthrow
    -> const Expression *
{
  if (let const *const *slot = m_functions.find(name)) return *slot;
  return nullptr;
}

pure fn EvalContext::has_functions() const wontthrow -> bool
{
  return m_functions.count() != 0;
}

fn EvalContext::unset_function(StringView name) throws -> void
{
  LOG(verbosity::Info, "unsetting function '%.*s'",
      static_cast<int>(name.length), name.data);
  m_functions.erase(name);
  m_function_sources.erase(name);
}

fn EvalContext::function_names() const throws -> HashSet
{
  let names = HashSet{heap_allocator()};
  m_functions.for_each([&](StringView name, const Expression *body) {
    unused(body);
    names.add(name);
  });
  return names;
}

fn EvalContext::variable_names(Allocator result_allocator) const throws
    -> HashSet
{
  let names = HashSet{result_allocator};
  m_shell_variables.for_each([&](StringView name, const String &value) {
    unused(value);
    names.add(name);
  });
  /* An indexed or associative array is a set variable too, so its name joins
     the scalar names. A reference such as $arr reads element zero, so
     completion and the unset-variable highlight both treat the array as
     present. */
  m_indexed_arrays.for_each(
      [&](StringView name, const ArrayList<String> &value) {
        unused(value);
        names.add(name);
      });
  m_associative_names.for_each([&](StringView name) { names.add(name); });
  return names;
}

fn EvalContext::set_trap(StringView condition, StringView action) throws -> void
{
  LOG(verbosity::Info, "setting a trap for '%.*s' with a %zu byte action",
      static_cast<int>(condition.length), condition.data, action.length);
  m_traps.set(condition, action);
  /* EXIT runs at the shell's end and needs no OS handler. A signal condition
     installs the shell's handler so the action runs on arrival, or the ignore
     disposition when the action is the empty string, as trap "" SIG asks. */
  if (condition == "EXIT") return;
  if (let const number = os::signal_number_from_name(condition)) {
    if (action.is_empty())
      os::set_trap_ignore(*number);
    else
      os::set_trap_handler(*number);
  }
}

fn EvalContext::remove_trap(StringView condition) throws -> void
{
  LOG(verbosity::Info, "removing the trap for '%.*s'",
      static_cast<int>(condition.length), condition.data);
  m_traps.erase(condition);
  /* Removing a signal trap returns the signal to its default disposition, so a
     later arrival acts the way it would without any trap. */
  if (condition == "EXIT") return;
  if (let const number = os::signal_number_from_name(condition))
    os::clear_trap_handler(*number);
}

fn EvalContext::install_trap_dispositions() throws -> void
{
  LOG(verbosity::Info, "reinstalling the dispositions of %zu traps",
      m_traps.count());
  m_traps.for_each([&](StringView condition, const String &action) {
    if (condition == "EXIT") return;
    if (let const number = os::signal_number_from_name(condition)) {
      if (action.is_empty())
        os::set_trap_ignore(*number);
      else
        os::set_trap_handler(*number);
    }
  });
}

fn EvalContext::run_pending_traps() throws -> void
{
  /* A signal delivered while a trap action runs must not nest a second drain,
     so the guard returns early and the new flag waits for the next boundary. */
  if (m_running_traps) return;
  m_running_traps = true;
  defer { m_running_traps = false; };

  /* The fast flag is cleared before the per-signal flags are consumed, so a
     signal that arrives during the drain re-sets it and the next boundary
     drains again rather than dropping the arrival. */
  os::SIGNAL_PENDING = 0;

  /* A trap action must not change the $? the interrupted code goes on to read,
     so the status is saved here and restored after the actions run, the way
     dash keeps a trap transparent to $?. */
  const i32 saved_exit_status = m_last_exit_status;

  for (i32 number = os::take_pending_signal(); number != 0;
       number = os::take_pending_signal())
  {
    let const name = os::signal_name_from_number(number);
    if (!name.has_value()) continue;
    if (let const *action = m_traps.find(name->view()))
      if (action->count() > 0) {
        LOG(verbosity::Info, "running the trap action for signal '%s'",
            name->c_str());
        run_source(action->view(), "the " + *name + " trap");
      }
  }

  m_last_exit_status = saved_exit_status;
}

pure fn EvalContext::traps() const wontthrow -> const StringMap<String> &
{
  return m_traps;
}

cold fn EvalContext::run_exit_trap() throws -> void
{
  if (m_exit_trap_ran) return;
  m_exit_trap_ran = true;

  /* A Ctrl-C that ended the last command leaves the interrupt flag set. The
     exit trap runs as the shell winds down and must not be aborted by it, so
     the flag is dropped before the action evaluates. */
  os::INTERRUPT_REQUESTED = 0;

  if (let const *action = m_traps.find(StringView{"EXIT", 4}))
    if (action->count() > 0) {
      LOG(verbosity::Info, "running the EXIT trap action at shell exit");
      run_source(action->view(), "the EXIT trap");
    }
}

fn EvalContext::has_exit_trap() const wontthrow -> bool
{
  if (let const *action = m_traps.find(StringView{"EXIT", 4}))
    return action->count() > 0;
  return false;
}

fn EvalContext::clear_inherited_exit_trap() throws -> void
{
  m_traps.erase(StringView{"EXIT", 4});
}

cold fn EvalContext::run_subshell_exit_trap() throws -> void
{
  /* Only an EXIT action the subshell itself set is present here, since the
     boundary cleared the inherited one on entry. The action runs in the
     subshell's still-current state, before restore_state returns the parent's
     traps and variables. */
  if (let const *action = m_traps.find(StringView{"EXIT", 4}))
    if (action->count() > 0) {
      LOG(verbosity::Info,
          "running the EXIT trap action the subshell set at its end");
      run_source(action->view(), "the EXIT trap");
    }
}

fn EvalContext::mark_readonly(StringView name) throws -> void
{
  m_readonly_names.add(name);
}

fn EvalContext::is_readonly(StringView name) const wontthrow -> bool
{
  return m_readonly_names.contains(name);
}

fn EvalContext::readonly_names() const throws -> ArrayList<String>
{
  let out = ArrayList<String>{};
  out.reserve(m_readonly_names.count());
  m_readonly_names.for_each([&](StringView name) { out.push_managed(name); });
  utils::sort_ascending(out);
  return out;
}

fn EvalContext::mark_integer(StringView name) throws -> void
{
  m_integer_names.add(name);
}

fn EvalContext::unmark_integer(StringView name) throws -> void
{
  m_integer_names.remove(name);
}

fn EvalContext::is_integer_variable(StringView name) const wontthrow -> bool
{
  return m_integer_names.contains(name);
}

fn EvalContext::append_integer_expression(String &joined,
                                          StringView expression) const throws
    -> void
{
  joined += '+';
  for (usize i = 0; i < expression.length; i++) {
    let const c = expression[i];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      joined += '(';
      joined.append(expression.substring(i));
      joined += ')';
      return;
    }
  }
  joined += '0';
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

fn EvalContext::declare_local(StringView name) throws -> void
{
  if (m_local_scopes.is_empty()) return;
  ASSERT(!m_local_scopes.is_empty());
  /* One binding per scope, the bash rule. A second local declaration of the
     same name in the same scope keeps the first declaration's saved caller
     state, so the scope pop restores the true pre-call value and the unset
     peel finds exactly one entry to consume. */
  if (is_local_in_current_scope(name)) return;
  LOG(verbosity::All, "declaring '%.*s' local in scope depth %zu",
      static_cast<int>(name.length), name.data, m_local_scopes.count());

  /* The indexed array the name held is saved alongside the scalar value, so a
     local array restores the caller's array on return. A copy is taken since
     the body may overwrite the stored array in place. The lookup is skipped
     when no array exists at all, so a scalar local in an array-free script pays
     nothing on the function-call path. */
  let previous_array = Maybe<ArrayList<String>>{};
  if (m_indexed_arrays.count() != 0)
    if (let const *array = lookup_indexed_array(name); array != nullptr) {
      let copy = ArrayList<String>{heap_allocator()};
      copy.reserve(array->count());
      for (const String &element : *array)
        copy.push_managed(element.view());
      previous_array = steal(copy);
    }

  /* The associative array the name held is saved the same way, as parallel key
     and value lists, so a local -A restores the caller's map on return. */
  let const previous_was_associative = is_associative_array(name);
  let previous_keys = ArrayList<String>{heap_allocator()};
  let previous_values = ArrayList<String>{heap_allocator()};
  if (previous_was_associative) {
    previous_keys = associative_keys(name);
    previous_values = associative_values(name);
  }

  /* A local starts with no attributes the way bash localizes a name fresh, so
     the caller's integer mark is dropped here and the saved flag puts it back
     when the scope ends. */
  let const previous_was_integer = is_integer_variable(name);
  if (previous_was_integer) unmark_integer(name);

  m_local_scopes.back().push(local_binding{
      String{name}, get_variable_value(name), steal(previous_array),
      previous_was_associative, steal(previous_keys), steal(previous_values),
      previous_was_integer});
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
  LOG(verbosity::All, "removing alias '%.*s'",
      static_cast<int>(name.length), name.data);
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
  m_subshell_depth--;
  LOG(verbosity::Debug, "left a subshell, depth now %zu", m_subshell_depth);
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
  m_current_location_position = location.position;
}

/* A cap on nested dot-source and eval runs. A configure script nests at most a
   handful of source levels, so the cap sits far above any legitimate depth yet
   below the point where the native call stack would overflow first, since each
   level spends many native frames between run_source calls. A run that crosses
   it is a runaway that would otherwise exhaust memory. */
static constexpr usize MAX_SOURCE_DEPTH = 400;
/* A cap on nested mimicked scripts, so a script that mimics another that mimics
   it cannot recurse without limit. */
static constexpr usize MAX_MIMICRY_DEPTH = 400;
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
  LOG(verbosity::Info, "the xtrace option flips to %s",
      enabled ? "on" : "off");
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
  LOG(verbosity::Info, "the noglob option flips to %s",
      enabled ? "on" : "off");
  m_enable_path_expansion = !enabled;
}

pure fn EvalContext::no_glob() const wontthrow -> bool
{
  return !m_enable_path_expansion;
}

fn EvalContext::set_no_exec(bool enabled) wontthrow -> void
{
  LOG(verbosity::Info, "the noexec option flips to %s",
      enabled ? "on" : "off");
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

fn EvalContext::clear_functions() wontthrow -> void { m_functions.clear(); }

fn EvalContext::snapshot_state() const throws -> eval_state_snapshot
{
  return eval_state_snapshot{m_shell_variables,
                             m_functions,
                             m_function_sources,
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

hot fn EvalContext::expand_variable(StringView name) const throws -> String
{
  return get_variable_value(name).value_or(String{});
}

namespace {

enum class trim_end
{
  Prefix,
  Suffix,
};

/* Remove the shortest or longest prefix or suffix of value that matches pattern
   as a glob, returning the surviving part. The candidate substrings are views
   over value, so only the returned remainder allocates. The active mask runs
   parallel to pattern and marks which pattern bytes may act as glob
   metacharacters, so a quoted or escaped * or ? matches itself. */
fn trim_matching(Allocator result_allocator, StringView value,
                 StringView pattern, const ArrayList<bool> &active,
                 trim_end end, bool longest, bool extglob_enabled) throws
    -> String
{
  ASSERT(active.count() == pattern.length);

  if (end == trim_end::Prefix) {
    /* The longest match scans down from the whole string and the shortest
       scans up from the empty prefix, so the first hit is the wanted length. */
    if (longest) {
      for (usize length = value.length;; length--) {
        if (utils::glob_matches(pattern, value.substring_of_length(0, length),
                                active, 0, extglob_enabled))
          return String{result_allocator, value.substring(length)};
        if (length == 0) break;
      }
    } else {
      for (usize length = 0; length <= value.length; length++) {
        if (utils::glob_matches(pattern, value.substring_of_length(0, length),
                                active, 0, extglob_enabled))
          return String{result_allocator, value.substring(length)};
      }
    }
  } else {
    /* The longest match scans the suffix start up from byte zero and the
       shortest scans down from the end, so the first hit is the wanted start.
     */
    if (longest) {
      for (usize start = 0; start <= value.length; start++) {
        if (utils::glob_matches(pattern, value.substring(start), active, 0,
                                extglob_enabled))
          return String{result_allocator, value.substring_of_length(0, start)};
      }
    } else {
      for (usize start = value.length;; start--) {
        if (utils::glob_matches(pattern, value.substring(start), active, 0,
                                extglob_enabled))
          return String{result_allocator, value.substring_of_length(0, start)};
        if (start == 0) break;
      }
    }
  }
  return String{result_allocator, value};
}

/* The shared core of the # and % prefix and suffix trims, so the scalar
   ${v#pat} and ${v%pat} cases and the array-element path expand the pattern
   with its glob mask and run trim_matching through one place. */
static fn trim_value_with_modifier(EvalContext &cxt, StringView value,
                                   StringView word, trim_end end,
                                   bool longest) throws -> String
{
  LOG(verbosity::All,
      "trimming a value of %zu bytes with the pattern word '%.*s'",
      value.length, static_cast<int>(word.length), word.data);
  let active = ArrayList<bool>{cxt.scratch_allocator()};
  let const pattern = cxt.expand_modifier_word_masked(word, active);
  return trim_matching(cxt.scratch_allocator(), value, pattern.view(), active,
                       end, longest, cxt.extglob_enabled());
}

} /* namespace */

fn EvalContext::expand_modifier_word(StringView word, bool remove_quotes) throws
    -> String
{
  /* The default, assign, alternate, error, and arithmetic forms never glob, so
     the mask the worker fills is discarded here. The default word keeps a
     backslash that sits before an ordinary character, so the pattern-only
     unescape stays off. */
  let discarded_mask = ArrayList<bool>{scratch_allocator()};
  return expand_modifier_word_worker(word, discarded_mask, remove_quotes,
                                     false);
}

fn EvalContext::expand_modifier_word_masked(StringView word,
                                            ArrayList<bool> &active_out,
                                            bool remove_quotes) throws -> String
{
  /* A # or % pattern word has every backslash quote the next byte, so the byte
     is emitted literally and marked inactive in the mask. */
  return expand_modifier_word_worker(word, active_out, remove_quotes, true);
}

fn EvalContext::expand_modifier_word_worker(StringView word,
                                            ArrayList<bool> &active_out,
                                            bool remove_quotes,
                                            bool is_pattern_word) throws
    -> String
{
  LOG(verbosity::All, "expanding a modifier word of %zu bytes", word.length);
  let out = String{scratch_allocator()};

  /* Append one byte and record whether it may act as a glob metacharacter, so
     the mask stays parallel to out. */
  auto emit_byte = [&](char byte, bool is_active) {
    out += byte;
    active_out.push(is_active);
  };

  /* Append a run of bytes that share one glob-active state, used for an
     expansion result whose every byte takes the same mask. */
  auto emit_run = [&](StringView bytes, bool is_active) {
    out.append(bytes);
    for (usize k = 0; k < bytes.length; k++)
      active_out.push(is_active);
  };

  let in_single_quote = false;
  let in_double_quote = false;
  for (usize i = 0; i < word.length; i++) {
    /* In a default or a pattern word the quotes are removed, so a quoted
       expansion such as ${x%"$suffix"} matches the value of suffix literally.
       Heredoc bodies keep their quotes and pass remove_quotes as false. */
    if (remove_quotes && !in_single_quote && word[i] == '"') {
      in_double_quote = !in_double_quote;
      continue;
    }
    if (remove_quotes && !in_double_quote && word[i] == '\'') {
      in_single_quote = !in_single_quote;
      continue;
    }
    if (in_single_quote) {
      emit_byte(word[i], false);
      continue;
    }

    /* A backslash escapes the next byte from expansion. Before a dollar,
       backtick, or backslash, and before a double quote in a quote-stripping
       word, the escaped byte is emitted literally and not treated as an
       expansion or a quote. Before a newline it is a line continuation. Any
       other backslash is kept literally, matching the shell in a heredoc body
       and a parameter word. */
    if (word[i] == '\\') {
      /* In a # or % pattern word a backslash quotes whatever byte follows, so
         the byte is emitted literally and inactive and the backslash is
         dropped, which makes a quoted glob character such as \* match itself. A
         trailing backslash with no following byte is kept literally. */
      if (is_pattern_word && i + 1 < word.length) {
        emit_byte(word[i + 1], false);
        i++;
        continue;
      }
      if (i + 1 < word.length) {
        const char next = word[i + 1];
        if (next == '$' || next == '`' || next == '\\' ||
            (remove_quotes && next == '"'))
        {
          emit_byte(next, false);
          i++;
          continue;
        }
        if (next == '\n') {
          i++;
          continue;
        }
      }
      emit_byte('\\', false);
      continue;
    }

    if (word[i] == '`') {
      /* Old-style backtick command substitution in a default, alternate,
         assign, or error word, and in a heredoc body. It runs to the next
         unescaped backtick. The POSIX backquote unescaping strips a backslash
         that precedes a backtick, a dollar sign, or another backslash, and the
         unescaped bytes are captured the same way $(...) is. */
      let inner = String{scratch_allocator()};
      usize j = i + 1;
      for (; j < word.length; j++) {
        if (word[j] == '\\' && j + 1 < word.length &&
            (word[j + 1] == '`' || word[j + 1] == '$' || word[j + 1] == '\\'))
        {
          inner += word[j + 1];
          j++;
          continue;
        }
        if (word[j] == '`') break;
        inner += word[j];
      }
      emit_run(capture_command_substitution(inner), !in_double_quote);
      i = j;
      continue;
    }

    if (word[i] != '$') {
      emit_byte(word[i], !in_double_quote);
      continue;
    }
    if (i + 1 >= word.length) {
      emit_byte('$', !in_double_quote);
      break;
    }

    let const next = word[i + 1];
    if (next == '{') {
      /* Scan the ${...} body to the matching } at brace depth one. A nested
         ${...} bumps the depth, a nested $(...) or $((...)) is copied by
         quote-aware paren balance, a backtick run is copied to its close, and a
         single quote run, a double quote run, and a backslash escape keep their
         bytes literal so a } they contain is never counted. The structure
         mirrors the Lexer brace scanner. */
      let inner = String{scratch_allocator()};
      usize j = i + 2;
      i32 depth = 1;
      char quote = 0;
      while (j < word.length) {
        const char ch = word[j];
        if (quote != 0) {
          inner += ch;
          if (quote == '"' && ch == '\\' && j + 1 < word.length) {
            inner += word[++j];
            j++;
            continue;
          }
          if (ch == quote) quote = 0;
          j++;
          continue;
        }
        if (ch == '\\' && j + 1 < word.length) {
          inner += ch;
          inner += word[++j];
          j++;
          continue;
        }
        if (ch == '\'' || ch == '"') {
          quote = ch;
          inner += ch;
          j++;
          continue;
        }
        if (ch == '`') {
          inner += ch;
          j++;
          while (j < word.length) {
            const char b = word[j];
            inner += b;
            j++;
            if (b == '\\' && j < word.length) {
              inner += word[j++];
              continue;
            }
            if (b == '`') break;
          }
          continue;
        }
        if (ch == '$' && j + 1 < word.length && word[j + 1] == '(') {
          inner += ch;
          inner += word[++j];
          j++;
          usize paren_depth = 1;
          char nested_quote = 0;
          while (j < word.length) {
            const char p = word[j];
            inner += p;
            j++;
            if (nested_quote != 0) {
              if (nested_quote == '"' && p == '\\' && j < word.length) {
                inner += word[j++];
                continue;
              }
              if (p == nested_quote) nested_quote = 0;
              continue;
            }
            if (p == '\\' && j < word.length) {
              inner += word[j++];
              continue;
            }
            if (p == '\'' || p == '"') {
              nested_quote = p;
            } else if (p == '(') {
              paren_depth++;
            } else if (p == ')') {
              paren_depth--;
              if (paren_depth == 0) break;
            }
          }
          continue;
        }
        if (ch == '$' && j + 1 < word.length && word[j + 1] == '{') {
          depth++;
          inner += ch;
          inner += word[++j];
          j++;
          continue;
        }
        if (ch == '}') {
          depth--;
          if (depth == 0) break;
          inner += ch;
          j++;
          continue;
        }
        inner += ch;
        j++;
      }
      emit_run(apply_parameter_expansion(inner), !in_double_quote);
      i = j;
    } else if (lexer::is_variable_name_start(next)) {
      let name = String{scratch_allocator()};
      usize j = i + 1;
      while (j < word.length && lexer::is_variable_name(word[j]))
        name += word[j++];
      /* A nested reference inside a default or alternate word, or a heredoc
         body, obeys set -u the same way a top level reference does, so an unset
         name here aborts rather than expanding to nothing, or warns under -W.
       */
      if (!get_variable_value(name).has_value()) report_unset_reference(name);
      /* An ordinary name appends its stored value straight from the store, so
         the common reference pays no temporary String the way a synthesized
         special name would. */
      if (const String *stored = lookup_shell_variable(name))
        emit_run(stored->view(), !in_double_quote);
      else
        emit_run(expand_variable(name), !in_double_quote);
      i = j - 1;
    } else if (next == '(' && i + 2 < word.length && word[i + 2] == '(') {
      /* Arithmetic $((...)), scanned to the matching )). A quote run and a
         backslash escape keep their bytes literal so a ) inside a string is
         text and does not count toward the grouping depth or terminate the
         expansion. */
      let inner = String{scratch_allocator()};
      usize j = i + 3;
      usize depth = 0;
      char quote = 0;
      for (; j < word.length; j++) {
        const char ch = word[j];
        if (quote != 0) {
          inner += ch;
          if (quote == '"' && ch == '\\' && j + 1 < word.length) {
            inner += word[++j];
            continue;
          }
          if (ch == quote) quote = 0;
          continue;
        }
        if (ch == '\\' && j + 1 < word.length) {
          inner += ch;
          inner += word[++j];
          continue;
        }
        if (ch == '\'' || ch == '"') {
          quote = ch;
        } else if (ch == '(') {
          depth++;
        } else if (ch == ')' && depth > 0) {
          depth--;
        } else if (ch == ')' && j + 1 < word.length && word[j + 1] == ')') {
          j += 2;
          break;
        }
        inner += ch;
      }
      /* An arithmetic result is decimal digits and an optional minus, none of
         which can glob, so the bytes are emitted inactive. */
      emit_run(utils::int_to_text(evaluate_arithmetic(inner)), false);
      i = j - 1;
    } else if (next == '(') {
      /* Command substitution $(...), scanned to the matching ). A quote run and
         a backslash escape keep their bytes literal so a ) inside a string does
         not decrement the depth and close the substitution early. */
      let inner = String{scratch_allocator()};
      usize j = i + 2;
      usize depth = 1;
      char quote = 0;
      for (; j < word.length; j++) {
        const char ch = word[j];
        if (quote != 0) {
          inner += ch;
          if (quote == '"' && ch == '\\' && j + 1 < word.length) {
            inner += word[++j];
            continue;
          }
          if (ch == quote) quote = 0;
          continue;
        }
        if (ch == '\\' && j + 1 < word.length) {
          inner += ch;
          inner += word[++j];
          continue;
        }
        if (ch == '\'' || ch == '"') {
          quote = ch;
        } else if (ch == '(') {
          depth++;
        } else if (ch == ')') {
          depth--;
          if (depth == 0) break;
        }
        inner += ch;
      }
      emit_run(capture_command_substitution(inner), !in_double_quote);
      i = j;
    } else if (next == '?' || next == '@' || next == '*' || next == '#' ||
               next == '$' || next == '!' || next == '-' ||
               lexer::is_number(next))
    {
      let const special_name = StringView{&next, 1};
      if (!get_variable_value(special_name).has_value())
        report_unset_reference(special_name);
      emit_run(expand_variable(special_name), !in_double_quote);
      i++;
    } else {
      emit_byte('$', !in_double_quote);
    }
  }
  return out;
}

hot fn EvalContext::apply_parameter_expansion(StringView spec) throws -> String
{
  LOG(verbosity::All, "applying the parameter expansion '${%.*s}'",
      static_cast<int>(spec.length), spec.data);
  if (spec.is_empty()) return String{scratch_allocator()};

  /* ${!name} reads the value of the variable that name names, or lists the
     variable names that start with a prefix when it ends with * or @. The
     length form ${#name} below begins with the same kind of sigil, so this is
     checked first. */
  if (spec.length > 1 && spec[0] == '!') {
    let const body = spec.substring(1);
    /* A modifier after the name, such as ${!v+set} or ${!v:-fb}, applies to
       the indirected value, so the name splits off and the rewritten spec,
       the target name followed by the original modifier, runs through this
       same dispatch. The bare trailing * and @ stay with the body since those
       are the prefix-listing forms, and a [subscript] stays glued to the
       name. */
    usize name_end = 0;
    while (name_end < body.length && lexer::is_variable_name(body[name_end]))
      name_end++;
    if (name_end > 0 && name_end < body.length && body[name_end] == '[')
      if (let const close = body.substring(name_end).find_character(']'))
        name_end += *close + 1;
    if (name_end > 0 && name_end < body.length &&
        !(name_end == body.length - 1 &&
          (body[name_end] == '*' || body[name_end] == '@')))
    {
      let const name = body.substring_of_length(0, name_end);
      let const target = get_variable_value(name);
      let rewritten = String{scratch_allocator()};
      /* bash makes an unset indirection name with a modifier a command-level
         "invalid indirect expansion" failure. An expansion error here is
         fatal to the whole run, which would be harsher than bash, so the
         unset name stands in for the target instead, it is unset right here,
         and the modifier sees the unset state. */
      rewritten.append(target.has_value() ? StringView{target->view()} : name);
      rewritten.append(body.substring(name_end));
      return apply_parameter_expansion(rewritten.view());
    }
    return apply_indirect_or_name_listing(body);
  }

  /* ${#name} is the length of the value, distinct from $# which is the count of
     positional parameters. */
  if (spec.length > 1 && spec[0] == '#') {
    let const name = spec.substring(1);
    if (name == "@" || name == "*")
      return String{scratch_allocator(),
                    utils::uint_to_text(m_positional_params.count())};

    /* ${#a[@]} is the number of elements, while ${#a[i]} is the length of one
       element. A scalar counts as a single element. */
    if (let const bracket = name.find_character('[');
        bracket.has_value() && *bracket > 0 && name[name.length - 1] == ']' &&
        lexer::is_variable_name_start(name[0]))
    {
      const StringView array_name = name.substring_of_length(0, *bracket);
      const StringView subscript =
          name.substring_of_length(*bracket + 1, name.length - *bracket - 2);
      if (subscript == "@" || subscript == "*") {
        if (array_name == "FUNCNAME" && is_bash_compatible()) [[unlikely]]
          return String{scratch_allocator(),
                        utils::uint_to_text(funcname_frame_count())};
        if (is_associative_array(array_name))
          return String{
              heap_allocator(),
              utils::uint_to_text(associative_keys(array_name).count())};
        if (lookup_indexed_array(array_name) != nullptr)
          return utils::uint_to_text(
              collect_array_elements(array_name).count());
        return String{scratch_allocator(),
                      utils::uint_to_text(
                          get_variable_value(array_name).has_value() ? 1 : 0)};
      }
      return String{scratch_allocator(),
                    utils::uint_to_text(
                        apply_array_subscript(array_name, subscript).length())};
    }

    let const value = get_variable_value(name);
    if (!value.has_value()) report_unset_reference(name);
    return String{scratch_allocator(),
                  utils::uint_to_text(value.value_or(String{}).length())};
  }

  ASSERT(!spec.is_empty());
  usize name_end = 0;
  if (lexer::is_variable_name_start(spec[0])) {
    while (name_end < spec.length && lexer::is_variable_name(spec[name_end]))
      name_end++;
  } else if (lexer::is_number(spec[0])) {
    while (name_end < spec.length && lexer::is_number(spec[name_end]))
      name_end++;
  } else {
    /* A special single-character parameter, such as ? or @. */
    name_end = 1;
  }

  let const name = spec.substring_of_length(0, name_end);
  let const rest = spec.substring(name_end);

  /* ${name[subscript]} reads one element of an indexed array, or every element
     when the subscript is @ or *. Only the plain form with nothing after the
     ] is handled here, the modifier forms on an element are left for later. */
  if (!rest.is_empty() && rest[0] == '[' && !name.is_empty() &&
      lexer::is_variable_name_start(name[0]))
  {
    if (let const close = rest.find_character(']'); close.has_value()) {
      const StringView subscript = rest.substring_of_length(1, *close - 1);
      /* Nothing after the ] is the plain element read. */
      if (*close + 1 == rest.length)
        return apply_array_subscript(name, subscript);
      /* A value-transform modifier after the ], the / replacement, the # and %
         trims, or the ^ and , case changes, modifies the one element. The @ and
         * forms map per element and are produced in the field path, so only a
         single subscript is handled here. A different modifier such as :- falls
         through to the general parameter expansion. */
      const StringView modifier = rest.substring(*close + 1);
      const char modifier_op = modifier.is_empty() ? '\0' : modifier[0];
      if (subscript != "@" && subscript != "*" &&
          (modifier_op == '/' || modifier_op == '#' || modifier_op == '%' ||
           modifier_op == '^' || modifier_op == ','))
        return apply_value_modifier(
            apply_array_subscript(name, subscript).view(), modifier);
      /* The test and substring modifiers run against the element with its own
         setness, the way bash answers ${a[i]:-x} and ${a[i]:1:2}, and the =
         form assigns the element back. */
      if (subscript != "@" && subscript != "*" && !modifier.is_empty()) {
        let const is_colon = modifier_op == ':';
        let const after =
            is_colon && modifier.length > 1 ? modifier[1] : modifier_op;
        let const is_test_form =
            after == '-' || after == '+' || after == '=' || after == '?';
        if (is_colon && !is_test_form)
          return apply_substring_to_value(
              apply_array_subscript(name, subscript).view(),
              modifier.substring(1));
        if (is_test_form) {
          let const element_is_set = array_element_is_set(name, subscript);
          let const value = element_is_set
                                ? apply_array_subscript(name, subscript)
                                : String{scratch_allocator()};
          let const treat_as_unset =
              is_colon ? value.is_empty() : !element_is_set;
          let const word = modifier.substring(is_colon ? 2 : 1);
          switch (after) {
          case '-':
            if (treat_as_unset) return expand_modifier_word(word);
            return value;
          case '+':
            if (treat_as_unset) return String{scratch_allocator()};
            return expand_modifier_word(word);
          case '=': {
            if (!treat_as_unset) return value;
            let const assigned = expand_modifier_word(word);
            assign_array_element(name, subscript, assigned.view(), false);
            return assigned;
          }
          case '?':
            if (treat_as_unset) {
              if (word.is_empty())
                throw_script_fatal(
                    "Unable to expand '" + name + "[" + subscript +
                    "]' because the element is not set or is empty");
              throw_script_fatal(expand_modifier_word(word));
            }
            return value;
          default: break;
          }
        }
      }
    }
  }

  if (rest.is_empty()) {
    /* Under set -u a plain reference to a variable that is not set is an error,
       while a form with a modifier such as ${x:-w} handles the unset case
       itself. */
    if (!get_variable_value(name).has_value()) report_unset_reference(name);
    return expand_variable(name);
  }

  /* A leading colon makes the default, assign, alternate, and error forms treat
     an empty value as unset. */
  let const is_colon_form = rest[0] == ':';
  const usize op_index = is_colon_form ? 1 : 0;
  if (op_index >= rest.length) return expand_variable(name);

  /* ${name:offset:length} is bash substring expansion. It is the colon form
     whose character after the colon is not one of the - = + ? that name the
     default, assign, alternate, and error modifiers, so those still parse as
     before. A name of @ or * is a positional slice, left to the array work. */
  if (is_colon_form) {
    const char after_colon = rest[op_index];
    if (after_colon != '-' && after_colon != '=' && after_colon != '+' &&
        after_colon != '?' && name != "@" && name != "*")
      return apply_substring_expansion(name, rest.substring(1));
  }

  /* ${name/pat/rep} and its // # % variants are bash pattern replacement. They
     are the non-colon form whose operator is a slash. A name of @ or * applies
     per element, left to the array work. */
  if (!is_colon_form && rest[0] == '/' && name != "@" && name != "*")
    return apply_pattern_replacement(name, rest);

  /* ${name^}, ${name^^}, ${name,}, ${name,,}, ${name~}, ${name~~} are bash case
     modification. They are the non-colon form whose operator is a caret, a
     comma, or a tilde that toggles. */
  if (!is_colon_form && (rest[0] == '^' || rest[0] == ',' || rest[0] == '~') &&
      name != "@" && name != "*")
    return apply_case_modification(name, rest);

  let const op = rest[op_index];
  let const is_doubled = (op_index + 1 < rest.length &&
                          rest[op_index + 1] == op && (op == '#' || op == '%'));
  let const word = rest.substring(op_index + (is_doubled ? 2 : 1));

  /* A subscripted name tests and reads its element, the way bash answers
     ${a[i]+set}, while a bare name reads element zero through the ordinary
     lookup. */
  let current = Maybe<String>{};
  if (let const bracket = name.find_character('[');
      bracket.has_value() && name[name.length - 1] == ']')
  {
    let const array_name = name.substring_of_length(0, *bracket);
    let const subscript =
        name.substring_of_length(*bracket + 1, name.length - *bracket - 2);
    if (array_element_is_set(array_name, subscript))
      current = apply_array_subscript(array_name, subscript);
  } else {
    current = get_variable_value(name);
  }
  let const is_set = current.has_value();
  let const is_empty = !is_set || current->is_empty();
  let const treat_as_unset = is_colon_form ? is_empty : !is_set;

  switch (op) {
  case '-':
    if (treat_as_unset) return expand_modifier_word(word);
    ASSERT(current.has_value());
    return String{scratch_allocator(), current->view()};
  case '=':
    if (treat_as_unset) {
      let const assigned = expand_modifier_word(word);
      set_shell_variable(name, assigned);
      return assigned;
    }
    ASSERT(current.has_value());
    return String{scratch_allocator(), current->view()};
  case '+':
    if (treat_as_unset) return String{scratch_allocator()};
    return expand_modifier_word(word);
  case '?':
    if (treat_as_unset) {
      if (word.is_empty())
        throw_script_fatal("Unable to expand '" + name +
                           "' because the parameter is not set or is empty");
      throw_script_fatal(expand_modifier_word(word));
    }
    ASSERT(current.has_value());
    return String{scratch_allocator(), current->view()};

  case '#': {
    return trim_value_with_modifier(*this, current.value_or(String{}).view(),
                                    word, trim_end::Prefix, is_doubled);
  }

  case '%': {
    return trim_value_with_modifier(*this, current.value_or(String{}).view(),
                                    word, trim_end::Suffix, is_doubled);
  }

  default: return expand_variable(name);
  }
}

/* The index of the colon that separates the offset from the length in a
   substring body, or the body length when there is none. Parentheses and a
   ternary inside the offset are tracked so a colon belonging to a ternary is
   not mistaken for the separator. */
static fn find_substring_length_separator(StringView body) wontthrow -> usize
{
  usize paren_depth = 0;
  usize question_depth = 0;
  for (usize i = 0; i < body.length; i++) {
    const char c = body[i];
    if (c == '(') {
      paren_depth++;
    } else if (c == ')') {
      if (paren_depth > 0) paren_depth--;
    } else if (c == '?' && paren_depth == 0) {
      question_depth++;
    } else if (c == ':' && paren_depth == 0) {
      if (question_depth > 0)
        question_depth--;
      else
        return i;
    }
  }
  return body.length;
}

fn EvalContext::apply_substring_expansion(StringView name,
                                          StringView body) throws -> String
{
  let const current = get_variable_value(name);
  if (m_error_unset && !current.has_value())
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");
  return apply_substring_to_value(current.value_or(String{}).view(), body);
}

fn EvalContext::apply_substring_to_value(StringView value,
                                         StringView body) throws -> String
{
  LOG(verbosity::All, "taking the substring '%.*s' of a value of %zu bytes",
      static_cast<int>(body.length), body.data, value.length);
  const i64 value_length = static_cast<i64>(value.length);

  let const separator = find_substring_length_separator(body);
  let const offset_text = body.substring_of_length(0, separator);
  /* An empty offset reads as zero, the way bash treats ${v::length}. */
  const i64 offset =
      offset_text.is_empty() ? 0 : evaluate_arithmetic(offset_text);

  /* A negative offset counts from the end. An offset that still lands before
     the start of the value yields nothing, the way bash returns empty rather
     than clamping to the whole value. A positive offset past the end clamps to
     the end so the result is empty too. */
  i64 start = offset < 0 ? value_length + offset : offset;
  if (start < 0) return String{scratch_allocator()};
  if (start > value_length) start = value_length;

  i64 end = value_length;
  if (separator < body.length) {
    let const length_text = body.substring(separator + 1);
    /* An empty length reads as zero, so ${v:start:} is empty. */
    i64 length = length_text.is_empty() ? 0 : evaluate_arithmetic(length_text);
    if (length < 0) {
      /* A negative length names a position counted back from the end rather
         than a count of characters, so the substring runs up to that point. */
      end = value_length + length;
    } else {
      /* The end clamps to the value length just below, so the length is clamped
         here first to keep the start-plus-length sum from overflowing i64. */
      if (length > value_length) length = value_length;
      end = start + length;
    }
  }
  if (end > value_length) end = value_length;
  /* A length that resolves to a point before the offset is the bash
     "substring expression < 0" error, fatal the way bash makes it. */
  if (end < start)
    throw Error{"Unable to take the substring because the length names "
                "a point before the offset"};

  return String{scratch_allocator(),
                value.substring_of_length(static_cast<usize>(start),
                                          static_cast<usize>(end - start))};
}

/* The index of the unescaped slash that separates the pattern from the
   replacement, or the body length when there is none. A backslash escapes the
   following byte so a literal slash inside the pattern is not the separator. */
static fn find_replacement_separator(StringView body) wontthrow -> usize
{
  /* A slash inside a quote run or behind a backslash belongs to the pattern,
     the way bash reads ${var/#"a/b"/c}, so the scan tracks the quote state the
     expansion itself will read. */
  char quote = 0;
  for (usize i = 0; i < body.length; i++) {
    let const c = body[i];
    if (quote == '\'') {
      if (c == '\'') quote = 0;
      continue;
    }
    if (c == '\\') {
      i++;
      continue;
    }
    if (quote == '"') {
      if (c == '"') quote = 0;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == '/') return i;
  }
  return body.length;
}

/* The length of the longest match the pattern makes starting at the given
   position in the value, or None when it matches nothing there. The end shrinks
   from the value end so a greedy star takes the most it can, the way bash picks
   the leftmost-longest match. */
static fn longest_pattern_match_at(StringView pattern,
                                   const ArrayList<bool> &pattern_active,
                                   StringView value, usize start,
                                   bool extglob) throws -> Maybe<usize>
{
  for (usize end = value.length; end >= start; end--) {
    if (utils::glob_matches(pattern,
                            value.substring_of_length(start, end - start),
                            pattern_active, 0, extglob))
      return end - start;
    if (end == start) break;
  }
  return None;
}

fn EvalContext::apply_pattern_replacement(StringView name,
                                          StringView spec) throws -> String
{
  let const current = get_variable_value(name);
  if (m_error_unset && !current.has_value())
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");
  return pattern_replace_value(current.value_or(String{}), spec);
}

fn EvalContext::pattern_replace_value(const String &value,
                                      StringView spec) throws -> String
{
  LOG(verbosity::All,
      "applying the pattern replacement '%.*s' to a value of %zu bytes",
      static_cast<int>(spec.length), spec.data, value.count());
  /* The spec opens with the slash operator. A doubled slash replaces every
     match, and a # or % after the first slash anchors the pattern to the start
     or the end of the value. */
  StringView remainder = spec.substring(1);
  bool replace_all = false;
  bool anchor_start = false;
  bool anchor_end = false;
  if (!remainder.is_empty() && remainder[0] == '/') {
    replace_all = true;
    remainder = remainder.substring(1);
  } else if (!remainder.is_empty() && remainder[0] == '#') {
    anchor_start = true;
    remainder = remainder.substring(1);
  } else if (!remainder.is_empty() && remainder[0] == '%') {
    anchor_end = true;
    remainder = remainder.substring(1);
  }

  const usize separator = find_replacement_separator(remainder);
  let pattern_active = ArrayList<bool>{scratch_allocator()};
  let const pattern = expand_modifier_word_masked(
      remainder.substring_of_length(0, separator), pattern_active);
  /* No separator means the replacement is empty, so the matches are deleted. */
  let const replacement =
      separator < remainder.length
          ? expand_modifier_word(remainder.substring(separator + 1))
          : String{heap_allocator()};

  /* An empty unanchored pattern matches nothing in bash, so the value is
     returned unchanged rather than splicing the replacement between every
     character. The anchored forms still splice at the start or the end. */
  if (pattern.is_empty() && !anchor_start && !anchor_end) return value;

  let out = String{scratch_allocator()};

  /* The start anchor only matches a prefix, so a single longest match at the
     front is replaced and the rest is kept. */
  if (anchor_start) {
    if (let const matched = longest_pattern_match_at(
            pattern.view(), pattern_active, value.view(), 0, extglob_enabled()))
    {
      out += replacement;
      out.append(value.view().substring(*matched));
    } else {
      out.append(value.view());
    }
    return out;
  }

  /* The end anchor matches a suffix, so the leftmost start whose remainder
     fully matches the pattern names the longest matching suffix to replace. */
  if (anchor_end) {
    for (usize start = 0; start <= value.length(); start++) {
      if (utils::glob_matches(pattern.view(), value.view().substring(start),
                              pattern_active, 0, extglob_enabled()))
      {
        out.append(value.view().substring_of_length(0, start));
        out += replacement;
        return out;
      }
    }
    out.append(value.view());
    return out;
  }

  /* The unanchored form scans left to right, replacing the first match for the
     single-slash form and every non-overlapping match for the doubled-slash
     form. A zero-length match advances one byte so the scan cannot loop. */
  bool has_replaced = false;
  usize i = 0;
  while (i < value.length()) {
    Maybe<usize> matched;
    if (!has_replaced || replace_all)
      matched = longest_pattern_match_at(pattern.view(), pattern_active,
                                         value.view(), i, extglob_enabled());
    if (matched.has_value()) {
      out += replacement;
      has_replaced = true;
      if (*matched == 0) {
        out.push(value.view()[i]);
        i++;
      } else {
        i += *matched;
      }
      if (!replace_all) {
        out.append(value.view().substring(i));
        return out;
      }
    } else {
      out.push(value.view()[i]);
      i++;
    }
  }
  return out;
}

fn EvalContext::apply_case_modification(StringView name, StringView spec) throws
    -> String
{
  let const current = get_variable_value(name);
  if (m_error_unset && !current.has_value())
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");
  return apply_case_modification_to_value(current.value_or(String{}).view(),
                                          spec);
}

/* The value-only core of the case modification, so an array element and the
   scalar name path share one implementation. */
fn EvalContext::apply_case_modification_to_value(StringView value,
                                                 StringView spec) throws
    -> String
{
  LOG(verbosity::All,
      "applying the case modification '%.*s' to a value of %zu bytes",
      static_cast<int>(spec.length), spec.data, value.length);
  const char op = spec[0];
  /* A doubled operator touches every matching character, a single one only the
     first. */
  const bool modify_all = spec.length > 1 && spec[1] == op;
  const StringView pattern_word = spec.substring(modify_all ? 2 : 1);

  /* An omitted pattern means every character matches, the way bash defaults the
     glob to ?. */
  let pattern_active = ArrayList<bool>{scratch_allocator()};
  String pattern;
  if (pattern_word.is_empty()) {
    pattern = String{scratch_allocator(), "?"};
    pattern_active.push(true);
  } else {
    pattern = expand_modifier_word_masked(pattern_word, pattern_active);
  }

  let out = String{scratch_allocator()};
  for (usize i = 0; i < value.length; i++) {
    char c = value[i];
    const bool affected = modify_all || i == 0;
    if (affected &&
        utils::glob_matches(pattern.view(), value.substring_of_length(i, 1),
                            pattern_active, 0, extglob_enabled()))
    {
      const unsigned char byte = static_cast<unsigned char>(c);
      if (op == '^') {
        c = static_cast<char>(std::toupper(byte));
      } else if (op == ',') {
        c = static_cast<char>(std::tolower(byte));
      } else {
        /* The tilde toggles, so an upper becomes lower and a lower becomes
           upper, and a non-letter is left alone. */
        if (std::islower(byte) != 0)
          c = static_cast<char>(std::toupper(byte));
        else if (std::isupper(byte) != 0)
          c = static_cast<char>(std::tolower(byte));
      }
    }
    out.push(c);
  }
  return out;
}

/* Apply a trailing parameter-expansion modifier to a single value, the /
   pattern replacement, the # and % prefix and suffix trims, and the ^ and ,
   case changes. An array element and an element of the [@]/[*] field path share
   this so each modifier maps over the element the way bash does. A modifier
   byte that is not a value transform leaves the value unchanged, since the
   caller only routes the transform modifiers here. */
fn EvalContext::apply_value_modifier(StringView value,
                                     StringView modifier) throws -> String
{
  if (modifier.is_empty()) return String{scratch_allocator(), value};
  const char op = modifier[0];
  if (op == '/')
    return pattern_replace_value(String{scratch_allocator(), value}, modifier);
  if (op == '^' || op == ',')
    return apply_case_modification_to_value(value, modifier);
  if (op == '#' || op == '%') {
    const bool is_doubled = modifier.length > 1 && modifier[1] == op;
    const StringView pattern_word = modifier.substring(is_doubled ? 2 : 1);
    return trim_value_with_modifier(
        *this, value, pattern_word,
        op == '#' ? trim_end::Prefix : trim_end::Suffix, is_doubled);
  }
  return String{scratch_allocator(), value};
}

fn EvalContext::apply_array_subscript(StringView name,
                                      StringView subscript) throws -> String
{
  /* FUNCNAME reads the call-name stack, index zero the innermost frame the
     way bash exposes it, @ and * the whole stack outward. */
  if (name == "FUNCNAME" && is_bash_compatible()) [[unlikely]] {
    let const depth = funcname_frame_count();
    if (subscript == "@" || subscript == "*") {
      /* The * form joins with the first IFS byte the way "${a[*]}" does in
         bash, and an empty IFS joins with nothing. The @ form keeps the
         space. */
      let separator = ' ';
      let has_separator = true;
      if (subscript == "*") {
        has_separator = !m_field_separators.is_empty();
        if (has_separator) separator = m_field_separators.first_character();
      }
      let out = String{scratch_allocator()};
      for (usize i = 0; i < depth; i++) {
        if (i > 0 && has_separator) out.push(separator);
        out.append(funcname_frame_at(i));
      }
      return out;
    }
    let const index = evaluate_arithmetic(subscript);
    if (index >= 0 && static_cast<usize>(index) < depth)
      return String{scratch_allocator(),
                    funcname_frame_at(static_cast<usize>(index))};
    return String{scratch_allocator()};
  }

  /* An associative array reads by a string key, with @ and * naming every value
     joined by a space. The values come back in the store's order, which need
     not match bash for more than one key. */
  if (is_associative_array(name)) {
    if (subscript == "@" || subscript == "*") {
      /* The * form joins with the first IFS byte the way "${a[*]}" does in
         bash, and an empty IFS joins with nothing. The @ form keeps the
         space. */
      let separator = ' ';
      let has_separator = true;
      if (subscript == "*") {
        has_separator = !m_field_separators.is_empty();
        if (has_separator) separator = m_field_separators.first_character();
      }
      let out = String{scratch_allocator()};
      let const values = associative_values(name);
      for (usize i = 0; i < values.count(); i++) {
        if (i > 0 && has_separator) out.push(separator);
        out.append(values[i].view());
      }
      return out;
    }
    const String key = expand_modifier_word(subscript);
    return String{
        heap_allocator(),
        lookup_associative_element(name, key.view()).value_or(String{}).view()};
  }

  const ArrayList<String> *array = lookup_indexed_array(name);

  /* @ and * name the whole array, joined with a space. The single-string return
     loses the per-element split of a quoted "${a[@]}", which is the same
     limitation the positional "$@" has here. */
  if (subscript == "@" || subscript == "*") {
    if (array == nullptr) return get_variable_value(name).value_or(String{});
    /* The * form joins with the first IFS byte the way "${a[*]}" does in
       bash, and an empty IFS joins with nothing. The @ form keeps the
       space. */
    let separator = ' ';
    let has_separator = true;
    if (subscript == "*") {
      has_separator = !m_field_separators.is_empty();
      if (has_separator) separator = m_field_separators.first_character();
    }
    let out = String{scratch_allocator()};
    for (usize i = 0; i < array->count(); i++) {
      if (i > 0 && has_separator) out.push(separator);
      out.append((*array)[i].view());
    }
    return out;
  }

  /* An arithmetic subscript selects one element, with a negative index counting
     from the end the way bash does. */
  i64 index = evaluate_arithmetic(subscript);
  if (array == nullptr) {
    /* A scalar reads as a one-element array, so ${name[0]} is the value and any
       other index is empty. */
    if (index == 0) return get_variable_value(name).value_or(String{});
    return String{scratch_allocator()};
  }
  const i64 count = static_cast<i64>(array->count());
  if (index < 0) index += count;
  if (index < 0 || index >= count) {
    /* A subscript past the dense end may name a sparsely-held far element. */
    if (index >= 0) {
      let probe = String{scratch_allocator(), name};
      probe.push('\x01');
      probe.append(utils::uint_to_text(static_cast<usize>(index)).view());
      if (let const *sparse = m_sparse_array_values.find(probe.view()))
        return String{scratch_allocator(), sparse->view()};
    }
    return String{scratch_allocator()};
  }
  return String{scratch_allocator(),
                (*array)[static_cast<usize>(index)].view()};
}

fn EvalContext::collect_array_elements(StringView name) const throws
    -> ArrayList<String>
{
  /* FUNCNAME enumerates the call-name stack, innermost first the way bash
     orders it, so "${FUNCNAME[@]}" yields one frame per field. */
  if (name == "FUNCNAME" && is_bash_compatible() && funcname_frame_count() > 0)
      [[unlikely]]
  {
    let const depth = funcname_frame_count();
    let frames = ArrayList<String>{heap_allocator()};
    frames.reserve(depth);
    for (usize i = 0; i < depth; i++)
      frames.push_managed(funcname_frame_at(i));
    return frames;
  }

  if (is_associative_array(name)) return associative_values(name);

  let out = ArrayList<String>{heap_allocator()};
  if (const ArrayList<String> *array = lookup_indexed_array(name)) {
    out.reserve(array->count());
    for (const String &element : *array)
      out.push_managed(element.view());
    /* The sparse elements sit past the dense run, so appending them in index
       order yields the whole array in order. */
    for (sparse_array_entry &entry : collect_sparse_array_entries(
             m_sparse_array_values, name, scratch_allocator()))
      out.push(steal(entry.value));
    return out;
  }
  /* A plain scalar reads as a one-element array, the way bash treats $a as
     ${a[0]}, so "${a[@]}" of a scalar yields its single value. */
  if (Maybe<String> scalar = get_variable_value(name); scalar.has_value())
    out.push(steal(*scalar));
  return out;
}

fn EvalContext::array_element_is_set(StringView name,
                                     StringView subscript) throws -> bool
{
  if (subscript == "@" || subscript == "*")
    return !collect_array_elements(name).is_empty();
  if (is_associative_array(name))
    return lookup_associative_element(name, subscript).has_value();
  /* An indexed subscript is an arithmetic expression, so arr[1+1] resolves the
     way an indexed read would. A negative index counts from the end. */
  const i64 index = evaluate_arithmetic(subscript);
  if (const ArrayList<String> *array = lookup_indexed_array(name)) {
    const i64 count = static_cast<i64>(array->count());
    const i64 resolved = index < 0 ? index + count : index;
    if (resolved >= 0 && resolved < count) return true;
    /* An index past the dense run may name a sparsely-held element. */
    return resolved >= 0 &&
           m_sparse_array_values.find(
               sparse_array_key(name, static_cast<usize>(resolved),
                                scratch_allocator())
                   .view()) != nullptr;
  }
  /* A scalar answers for its sole index zero. */
  return index == 0 && get_variable_value(name).has_value();
}

fn EvalContext::matching_prefix_names(StringView prefix) const throws
    -> ArrayList<String>
{
  LOG(verbosity::All, "listing variable names with the prefix '%.*s'",
      static_cast<int>(prefix.length), prefix.data);
  let names = ArrayList<String>{heap_allocator()};
  let seen = HashSet{heap_allocator()};
  auto consider = [&](StringView candidate) throws {
    if (candidate.starts_with(prefix) && !seen.contains(candidate)) {
      seen.add(candidate);
      names.push_managed(candidate);
    }
  };
  m_shell_variables.for_each([&](StringView variable_name, const String &v) {
    unused(v);
    consider(variable_name);
  });
  for (const String &environment_name : os::environment_names())
    consider(environment_name.view());
  utils::sort_ascending(names);
  return names;
}

fn EvalContext::collect_array_subscripts(StringView name) const throws
    -> ArrayList<String>
{
  let out = ArrayList<String>{heap_allocator()};
  if (is_associative_array(name)) {
    for (const String &key : associative_keys(name))
      out.push(String{heap_allocator(), key.view()});
    return out;
  }
  if (let const *array = lookup_indexed_array(name)) {
    for (usize i = 0; i < array->count(); i++)
      out.push(utils::uint_to_text(i));
    for (const sparse_array_entry &entry : collect_sparse_array_entries(
             m_sparse_array_values, name, scratch_allocator()))
      out.push(utils::uint_to_text(entry.index));
    return out;
  }
  /* A scalar has the single index zero, an unset name has none. */
  if (get_variable_value(name).has_value())
    out.push(String{heap_allocator(), "0"});
  return out;
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

namespace {

/* A recursive-descent evaluator over the [[ ]] element list. The grammar joins
   primaries with && and ||, allows ! and parentheses, and reads unary and
   binary primaries the way the double-bracket conditional does, with no field
   splitting on the operands. */
struct ConditionalEvaluator
{
  EvalContext &cxt;
  const ArrayList<conditional_element> &elements;
  usize pos = 0;
  /* When a && or || branch is already decided, the other side is parsed to
     advance past its tokens but not evaluated, so a glob, a regex, a bad
     integer, or a command substitution on the dead branch runs no side effect
     and raises no evaluation error, the way bash short-circuits [[ ]]. */
  bool is_skipping = false;

  using Kind = conditional_element::Kind;

  pure bool at_end() const wontthrow { return pos >= elements.count(); }
  pure Kind kind_at(usize i) const wontthrow { return elements[i].kind; }

  /* The literal text of the token the evaluator stopped before, so an error
     names what was unexpected rather than leaving the reader to guess. */
  String unexpected_token() throws
  {
    return at_end() ? String{} : operand_literal(elements[pos]);
  }

  /* The literal source text of an operand, used to recognize a word operator
     such as == or -f without expanding it. */
  String operand_literal(const conditional_element &e) throws
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word)
      return static_cast<const tokens::WordToken *>(e.word)
          ->word()
          .to_literal_string();
    if (e.word != nullptr) return e.word->raw_string();
    return String{heap_allocator()};
  }

  /* The expanded value of an operand, with no field splitting. The expansion
     throws a plain Error, an unset variable under set -u, so it is relocated to
     a caret at the operand the way process_args locates a command argument. */
  String operand_value(const conditional_element &e) throws
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word) {
      try {
        return cxt.expand_word_for_assignment(
            static_cast<const tokens::WordToken *>(e.word)->word());
      } catch (const Error &err) {
        throw relocate_error(err, e.word->source_location());
      }
    }
    if (e.word != nullptr) return e.word->raw_string();
    return String{heap_allocator()};
  }

  /* The right side of == or != is a pattern, so it expands with a parallel mask
     that marks which *, ?, and [ stay active. A quoted or escaped metacharacter
     is masked off and matches literally, the way bash treats a quoted RHS. */
  String operand_pattern_masked(const conditional_element &e,
                                ArrayList<bool> &active) throws
  {
    if (e.word != nullptr && e.word->kind() == Token::Kind::Word) {
      try {
        return cxt.expand_case_pattern_masked(
            static_cast<const tokens::WordToken *>(e.word)->word(), active);
      } catch (const Error &err) {
        throw relocate_error(err, e.word->source_location());
      }
    }
    String raw =
        e.word != nullptr ? e.word->raw_string() : String{heap_allocator()};
    for (usize i = 0; i < raw.count(); i++)
      active.push(true);
    return raw;
  }

  static pure bool is_unary_op(StringView s) wontthrow
  {
    return s == "-z" || s == "-n" || s == "-e" || s == "-f" || s == "-d" ||
           s == "-r" || s == "-w" || s == "-x" || s == "-s" || s == "-h" ||
           s == "-L" || s == "-b" || s == "-c" || s == "-p" || s == "-S" ||
           s == "-g" || s == "-u" || s == "-k" || s == "-O" || s == "-G" ||
           s == "-v" || s == "-t" || s == "-o";
  }

  static pure bool is_binary_word_op(StringView s) wontthrow
  {
    return s == "=" || s == "==" || s == "!=" || s == "=~" || s == "-eq" ||
           s == "-ne" || s == "-lt" || s == "-le" || s == "-gt" || s == "-ge" ||
           s == "-ef" || s == "-nt" || s == "-ot";
  }

  /* The extended-regex metacharacters, escaped to a literal when a quoted byte
     of the pattern must match itself. */
  static pure bool is_regex_metacharacter(char c) wontthrow
  {
    return c == '.' || c == '^' || c == '$' || c == '*' || c == '+' ||
           c == '?' || c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '|' || c == '\\';
  }

  /* The =~ operator matches the value against an extended regular expression. A
     POSIX regcomp with the extended grammar mirrors the ERE bash uses, and a
     search rather than a full match finds the pattern anywhere in the value,
     the way [[ =~ does. On a match BASH_REMATCH is filled with the whole match
     at index 0 and each capture group after it, an unmatched group reading as
     an empty string the way bash leaves it. */
  bool regex_match(StringView value, StringView pattern,
                   const ArrayList<bool> &active) throws
  {
#if SHIT_PLATFORM_IS POSIX
    /* A byte the mask marks inactive came from a quoted or escaped part of the
       right operand, so a regex metacharacter there is backslash-escaped to
       match itself, the way bash matches a quoted portion of the operand
       literally. An active byte stays live regex. The escaped pattern is the
       cache key, built null-terminated for the regcomp a miss runs. */
    let escaped_pattern = String{cxt.scratch_allocator()};
    for (usize i = 0; i < pattern.length; i++) {
      const bool is_literal = i < active.count() && !active[i];
      if (is_literal && is_regex_metacharacter(pattern[i]))
        escaped_pattern += '\\';
      escaped_pattern += pattern[i];
    }
    /* The pattern compiles once and is reused on later matches through the
       context cache, so a hot =~ loop pays regcomp only the first time. regexec
       reads a C string, so the value is copied into a null-terminated buffer.
     */
    regex_t *compiled = cxt.cached_compiled_regex(escaped_pattern.view());
    let const value_text = String{cxt.scratch_allocator(), value};
    let const group_count = compiled->re_nsub + 1;
    let matches = ArrayList<regmatch_t>{cxt.scratch_allocator()};
    for (usize i = 0; i < group_count; i++)
      matches.push(regmatch_t{});
    const int match_result =
        regexec(compiled, value_text.c_str(), group_count, matches.begin(), 0);
    LOG(verbosity::All, "the =~ regex %s the value",
        match_result == 0 ? "matched" : "did not match");
    if (match_result != 0) {
      /* bash clears BASH_REMATCH on a non-match, so a later read does not see a
         prior match's captures. */
      cxt.set_indexed_array("BASH_REMATCH",
                            ArrayList<String>{heap_allocator()});
      return false;
    }

    let rematch = ArrayList<String>{heap_allocator()};
    for (usize i = 0; i < group_count; i++) {
      if (matches[i].rm_so < 0) {
        rematch.push(String{heap_allocator()});
        continue;
      }
      let const start = static_cast<usize>(matches[i].rm_so);
      let const end = static_cast<usize>(matches[i].rm_eo);
      rematch.push(String{heap_allocator(),
                          value.substring_of_length(start, end - start)});
    }
    cxt.set_indexed_array("BASH_REMATCH", steal(rematch));
    return true;
#else
    unused(value);
    unused(pattern);
    unused(active);
    throw Error{"Unable to use =~ in the [[ ]] because it is not supported on "
                "this platform"};
#endif
  }

  bool eval_unary(StringView op, StringView operand) throws
  {
    if (op == "-z") return operand.is_empty();
    if (op == "-n") return !operand.is_empty();
    if (op == "-v") {
      /* -v name[subscript] tests an array element or key, every other -v form
         tests a plain variable. */
      if (let const bracket = operand.find_character('[');
          bracket.has_value() && operand.length > 0 &&
          operand[operand.length - 1] == ']')
      {
        const StringView name = operand.substring_of_length(0, *bracket);
        const StringView subscript = operand.substring_of_length(
            *bracket + 1, operand.length - *bracket - 2);
        return cxt.array_element_is_set(name, subscript);
      }
      return cxt.get_variable_value(operand).has_value();
    }
    let const path = Path{operand};
    if (op == "-e") return path.exists();
    if (op == "-f") return path.is_regular_file();
    if (op == "-d") return path.is_directory();
    if (op == "-r") return path.is_readable();
    if (op == "-w") return path.is_writable();
    if (op == "-x") return path.is_executable();
    if (op == "-s") {
      let const size = path.file_size();
      return size.has_value() && size.value() > 0;
    }
    /* -t tests whether a file descriptor is an open terminal, the way a script
       gates an interactive feature on a real tty. Any descriptor is checked,
       not only the standard three, since a config such as ble.sh dups the
       controlling terminal onto a higher descriptor and tests that. */
    if (op == "-t") {
      if (ErrorOr<i64> descriptor = utils::parse_decimal_integer(operand);
          !descriptor.is_error())
#if SHIT_PLATFORM_IS WIN32
        /* A Windows descriptor is a HANDLE, so the shell fd number is mapped to
           its C runtime handle before the tty check. */
        return os::is_fd_a_tty(reinterpret_cast<os::descriptor>(
            _get_osfhandle(static_cast<int>(descriptor.value()))));
#else
        return os::is_fd_a_tty(static_cast<os::descriptor>(descriptor.value()));
#endif
      /* bash reports a non-integer -t operand as an error with status 2 and
         goes on, so the throw carries that status for the command-level
         catch. */
      let error = Error{"Unable to test '-t " + operand +
                        "' because the operand is not an integer"};
      error.set_command_status(2);
      throw error;
    }
    /* -o tests a shell option by name. Only the emacs line-editing option is
       reported, as on, since shit's interactive editing is emacs style. Every
       other option name reads as off for now, so a config that gates on the
       editing mode such as ble.sh sees emacs and proceeds. */
    if (op == "-o") return operand == "emacs";
    /* The remaining file-type tests fall back to existence, which covers the
       common scripts without a full stat-mode surface. */
    return path.exists();
  }

  bool eval_binary(StringView left, StringView op, StringView right) throws
  {
    if (op == "-ef") return Path{left}.is_same_file_as(Path{right});
    if (op == "-nt") return Path{left}.is_newer_than(Path{right});
    if (op == "-ot") return Path{left}.is_older_than(Path{right});

    /* The arithmetic comparison operands are full arithmetic expressions in
       bash, so 1+1 and a bare variable name evaluate rather than only a literal
       integer. An empty operand reads as zero the way an arithmetic context
       treats an unset value. */
    auto to_number = [&](StringView operand) throws -> i64 {
      for (usize i = 0; i < operand.length; i++)
        if (operand[i] != ' ' && operand[i] != '\t')
          return cxt.evaluate_arithmetic(operand);
      return 0;
    };
    const i64 a = to_number(left);
    const i64 b = to_number(right);
    if (op == "-eq") return a == b;
    if (op == "-ne") return a != b;
    if (op == "-lt") return a < b;
    if (op == "-le") return a <= b;
    if (op == "-gt") return a > b;
    return a >= b;
  }

  bool eval_primary() throws
  {
    if (at_end())
      throw Error{"Unable to evaluate the [[ ]] because the expression ends "
                  "unexpectedly"};
    const conditional_element &first = elements[pos];
    if (first.kind != Kind::Operand)
      throw Error{"Unable to evaluate the [[ ]] because an operator appears "
                  "where an operand is expected"};

    const String first_literal = operand_literal(first);

    /* A unary operator followed by an operand. */
    if (is_unary_op(first_literal.view()) && pos + 1 < elements.count() &&
        kind_at(pos + 1) == Kind::Operand)
    {
      pos += 2;
      if (is_skipping) return false;
      const String operand = operand_value(elements[pos - 1]);
      return eval_unary(first_literal.view(), operand.view());
    }

    /* A binary primary, with the operator either a < or > angle or a word
       operator such as == or -eq. */
    if (pos + 1 < elements.count()) {
      const Kind next = kind_at(pos + 1);
      if (next == Kind::Less || next == Kind::Greater) {
        if (pos + 2 >= elements.count() || kind_at(pos + 2) != Kind::Operand)
          throw Error{"Unable to evaluate the [[ ]] because an operand is "
                      "missing after a comparison"};
        pos += 3;
        if (is_skipping) return false;
        const String left = operand_value(elements[pos - 3]);
        const String right = operand_value(elements[pos - 1]);
        return next == Kind::Less ? left < right : right < left;
      }
      if (next == Kind::Operand) {
        const String op = operand_literal(elements[pos + 1]);
        if (is_binary_word_op(op.view())) {
          if (pos + 2 >= elements.count() || kind_at(pos + 2) != Kind::Operand)
            throw Error{"[[: expected operand after '" + op + "'"};
          pos += 3;
          if (is_skipping) return false;
          const String left = operand_value(elements[pos - 3]);
          /* == and != glob-match, and =~ regex-matches, with a quoting mask, so
             a quoted metacharacter of the right operand matches literally. The
             other binary operators read a plain string right operand. */
          if (op == "==" || op == "=" || op == "!=") {
            let active = ArrayList<bool>{cxt.scratch_allocator()};
            const String pattern =
                operand_pattern_masked(elements[pos - 1], active);
            const bool matched = utils::glob_matches(
                pattern.view(), left.view(), active, 0, cxt.extglob_enabled());
            return op == "!=" ? !matched : matched;
          }
          if (op == "=~") {
            let active = ArrayList<bool>{cxt.scratch_allocator()};
            const String pattern =
                operand_pattern_masked(elements[pos - 1], active);
            return regex_match(left.view(), pattern.view(), active);
          }
          const String right = operand_value(elements[pos - 1]);
          return eval_binary(left.view(), op.view(), right.view());
        }
      }
    }

    /* A lone operand is true when it is non-empty. */
    pos++;
    if (is_skipping) return false;
    const String value = operand_value(elements[pos - 1]);
    return !value.is_empty();
  }

  bool eval_term() throws
  {
    if (!at_end() && kind_at(pos) == Kind::Not) {
      pos++;
      return !eval_term();
    }
    if (!at_end() && kind_at(pos) == Kind::OpenParen) {
      pos++;
      const bool inner = eval_or();
      if (at_end() || kind_at(pos) != Kind::CloseParen)
        throw Error{"[[: expected ')'"};
      pos++;
      return inner;
    }
    return eval_primary();
  }

  bool eval_and() throws
  {
    bool result = eval_term();
    while (!at_end() && kind_at(pos) == Kind::And) {
      pos++;
      /* A false left already decides the and, so the right is parsed without
         evaluation. The skip nests, so an outer skip stays set. */
      const bool was_skipping = is_skipping;
      is_skipping = is_skipping || !result;
      const bool rhs = eval_term();
      is_skipping = was_skipping;
      result = result && rhs;
    }
    return result;
  }

  bool eval_or() throws
  {
    bool result = eval_and();
    while (!at_end() && kind_at(pos) == Kind::Or) {
      pos++;
      /* A true left already decides the or, so the right is parsed without
         evaluation. */
      const bool was_skipping = is_skipping;
      is_skipping = is_skipping || result;
      const bool rhs = eval_and();
      is_skipping = was_skipping;
      result = result || rhs;
    }
    return result;
  }
};

} /* namespace */

#if SHIT_PLATFORM_IS POSIX
/* The most distinct regex patterns the cache holds before it is cleared, so a
   pathological loop that builds a fresh pattern every iteration stays bounded
   rather than growing the table without end. A real script reuses a handful. */
static constexpr usize REGEX_CACHE_CAP = 128;

fn EvalContext::cached_compiled_regex(StringView pattern) throws -> regex_t *
{
  /* The key is the pattern text alone, which is sound only because compilation
     depends on nothing else, the flags are always REG_EXTENDED. A future option
     that changes compilation, such as REG_ICASE for nocasematch, must fold into
     the key so two intended compilations of one pattern do not collide. */
  if (CompiledRegex *cached = m_regex_cache.find(pattern)) {
    LOG(verbosity::All, "regex cache hit for the pattern '%.*s'",
        static_cast<int>(pattern.length), pattern.data);
    return cached->get();
  }

  /* A bounded miss path. When the cache is full it is cleared whole, which
     frees every compiled entry, rather than tracking a per-entry age. */
  if (m_regex_cache.count() >= REGEX_CACHE_CAP) {
    LOG(verbosity::Debug, "regex cache full, dropping %zu compiled patterns",
        m_regex_cache.count());
    m_regex_cache.clear();
  }

  LOG(verbosity::Debug, "regex cache miss, compiling the pattern '%.*s'",
      static_cast<int>(pattern.length), pattern.data);
  let const pattern_text = String{scratch_allocator(), pattern};
  regex_t compiled;
  if (regcomp(&compiled, pattern_text.c_str(), REG_EXTENDED) != 0) {
    /* bash returns status 2 for a malformed regex, which the conditional turns
       into an evaluation error. */
    throw Error{"Unable to evaluate the [[ ]] because the regular expression "
                "is invalid"};
  }
  m_regex_cache.set(pattern, CompiledRegex{compiled});
  return m_regex_cache.find(pattern)->get();
}
#endif

fn EvalContext::evaluate_conditional(
    const ArrayList<conditional_element> &elements) throws -> bool
{
  if (elements.is_empty())
    throw Error{"Unable to evaluate the [[ ]] because the conditional "
                "expression is empty"};
  LOG(verbosity::Debug, "evaluating a [[ ]] conditional of %zu elements",
      elements.count());
  let evaluator = ConditionalEvaluator{*this, elements};
  const bool result = evaluator.eval_or();
  if (!evaluator.at_end())
    throw Error{
        "Unable to evaluate the [[ ]] because the token '" +
        evaluator.unexpected_token() +
        "' came after a complete conditional, so it may be an operator shit "
        "does not support or a missing && or || between two tests"};
  return result;
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

fn EvalContext::expand_path_once(const glob_field &field,
                                 bool should_expand_files) throws
    -> ArrayList<glob_field>
{
  let const scratch = scratch_allocator();
  let expanded = ArrayList<glob_field>{scratch};

  /* This runs only for a field that holds a real glob, which is rare. The path
     text is split on its last separator into a parent directory and the glob
     stem. */
  let const path = field.text.view();
  LOG(verbosity::All, "scanning a directory for the glob component '%.*s'",
      static_cast<int>(path.length), path.data);

  let last_slash = Maybe<usize>{};
  for (usize i = path.length; i > 0; i--)
    if (path[i - 1] == '/') {
      last_slash = i - 1;
      break;
    }
  let const has_slashes = last_slash.has_value();

  let parent_dir = Path{};
  if (has_slashes)
    parent_dir =
        Path{*last_slash != 0 ? path.substring_of_length(0, *last_slash)
                              : path.substring_of_length(0, 1)};
  else
    parent_dir = Path{StringView{"."}};

  /* Stem of the glob after the last slash. Its mask starts at stem_start in the
     field, so glob_matches reads field.glob_active from there. */
  let const stem_start = has_slashes ? *last_slash + 1 : 0;
  let const has_glob = stem_start < path.length;
  let glob = StringView{};
  if (has_glob) glob = path.substring(stem_start);

  /* A missing or unreadable parent directory yields no match the way dash
     treats it, so the caller applies the failglob policy and the pattern stays
     literal under failglob-off rather than raising an error here. */
  let const entries = Path::read_directory(parent_dir);
  if (!entries.has_value()) {
    LOG(verbosity::Debug,
        "the parent directory is unreadable, the glob '%.*s' yields no match",
        static_cast<int>(path.length), path.data);
    return expanded;
  }

  if (!has_glob) {
    let copy = glob_field{scratch};
    copy.text.append(field.text.view());
    copy.glob_active = field.glob_active;
    expanded.push(steal(copy));
    return expanded;
  }

  /* The typed prefix is everything up to and through the last slash, such as
     dot-slash or a directory name with a slash. It is preserved on each match
     the way dash keeps the directory the user wrote, so a dot-slash glob yields
     a dot-slash result rather than a bare filename. A bare glob with no slash
     has an empty prefix and a synthetic dot parent, so the match carries the
     filename alone. */
  let const typed_prefix =
      has_slashes ? path.substring_of_length(0, stem_start) : StringView{};

  /* The no-glob field returned above, so the stem is a non-empty glob here and
     glob[0] reads a real byte. */
  ASSERT(has_glob);
  ASSERT(!glob.is_empty());

  for (const String &entry_name : *entries) {
    let const filename = entry_name.view();

    /* The full path joins the parent and the filename, the way the directory
       walk needs it for the is_directory test and the result text. */
    let full_path = parent_dir;
    full_path.push_component(filename);

    if (!should_expand_files && !full_path.is_directory()) continue;

    /* A hidden entry matches only a pattern that itself starts with a dot,
       the POSIX rule the dotglob tests record. */
    if (glob[0] != '.' && !filename.is_empty() && filename[0] == '.') continue;

    if (utils::glob_matches(glob, filename, field.glob_active, stem_start,
                            extglob_enabled()))
    {
      add_expansion();

      /* A real filename is literal, so the resulting field never globs again.
         The empty mask is the all-literal convention, so it carries no
         per-result allocation. The typed prefix joins the filename directly
         rather than through the Path join, so the user's exact "./" or "dir/"
         survives instead of a normalized form. */
      let result_field = glob_field{scratch};
      result_field.text.append(typed_prefix);
      result_field.text.append(filename);
      expanded.push(steal(result_field));
    }
  }

  return expanded;
}

namespace {

/* The index of the first active metacharacter that actually forms a glob. A '['
   without a later ']' is a literal bracket, not a glob, so a field such as the
   command word '[' needs no directory scan at all. Returns nullopt when the
   field is all literal. */
hot pure fn first_active_glob(StringView text, const ArrayList<bool> &mask,
                              bool extglob) wontthrow -> Maybe<usize>
{
  let open_bracket = Maybe<usize>{};
  for (usize i = 0; i < mask.count(); i++) {
    let const ch = text.data[i];
    /* An extended-glob opener such as @( forces a directory scan even though
       the opener byte is not a metacharacter in the mask, since the
       alternatives it holds match real names. The structure is read from the
       text the way the matcher reads it. */
    if (extglob && i + 1 < text.length &&
        (ch == '?' || ch == '*' || ch == '+' || ch == '@' || ch == '!') &&
        text.data[i + 1] == '(')
      return i;
    if (!mask[i]) continue;
    if (ch == '*' || ch == '?') return i;
    if (ch == '[') {
      if (!open_bracket) open_bracket = i;
    } else if (ch == ']' && open_bracket) {
      return open_bracket;
    }
  }
  return shit::None;
}

/* The recursion depth cap for a ** walk. A real directory tree is far
   shallower, so the cap only stops a symlink cycle from looping forever, since
   the path layer cannot tell a symlinked directory from a real one. */
constexpr usize GLOBSTAR_MAX_DEPTH = 256;

/* Collect the relative paths a ** component expands to, walking dir and its
   subdirectories. In directory position only subdirectories are collected and
   the base is added as the empty path so ** can match zero levels. As a
   trailing component every file and directory is collected, which are the final
   matches. A hidden entry is skipped the way bash globstar skips a dotfile. */
fn collect_globstar_paths(const Path &dir, StringView relative,
                          bool directories_only, bool include_base, usize depth,
                          Allocator allocator, ArrayList<String> &out) throws
    -> void
{
  LOG(verbosity::All,
      "collecting globstar paths under the relative path '%.*s', depth %zu",
      static_cast<int>(relative.length), relative.data, depth);
  if (directories_only && include_base) out.push(String{allocator, relative});
  if (depth >= GLOBSTAR_MAX_DEPTH) return;

  let const entries = Path::read_directory(dir);
  if (!entries.has_value()) return;

  for (const String &entry : *entries) {
    let const name = entry.view();
    if (!name.is_empty() && name[0] == '.') continue;

    let child_dir = dir;
    child_dir.push_component(name);
    let const is_dir = child_dir.is_directory();

    let child_relative = String{allocator};
    if (!relative.is_empty()) {
      child_relative.append(relative);
      child_relative += '/';
    }
    child_relative.append(name);

    if (!directories_only || is_dir)
      out.push(String{allocator, child_relative.view()});
    if (is_dir)
      collect_globstar_paths(child_dir, child_relative.view(), directories_only,
                             false, depth + 1, allocator, out);
  }
}

} /* namespace */

fn EvalContext::expand_path_recurse(ArrayList<glob_field> fields) throws
    -> ArrayList<glob_field>
{
  let const scratch = scratch_allocator();
  let result = ArrayList<glob_field>{scratch};

  for (glob_field &field : fields) {
    let const text = field.text.view();

    /* An empty mask is the all-literal convention, so a field without one holds
       no live glob metacharacter. */
    let const expand_ch =
        first_active_glob(text, field.glob_active, extglob_enabled());

    if (!expand_ch) {
      /* No glob remains. This field is a literal suffix appended after an
         earlier glob, so keep it only when it actually exists. A path produced
         purely by globbing came from a directory read and always exists, so it
         never reaches here and pays no stat. */
      if (Path{field.text.view()}.exists()) result.push(steal(field));
      continue;
    }

    /* An active glob index came from the mask, so it points inside the text and
       the field carries a mask parallel to the text. */
    ASSERT(*expand_ch < text.length);
    ASSERT(field.glob_active.count() == text.length);

    let slash_after = Maybe<usize>{};
    for (usize k = *expand_ch; k < text.length; k++) {
      if (text.data[k] == '/') {
        slash_after = k;
        break;
      }
    }

    /* A ** component matches across directory levels when globstar is on. The
       component runs from the slash before the glob to the slash after it, and
       it is the globstar form only when it is exactly two active stars. */
    usize component_start = 0;
    for (usize k = *expand_ch; k > 0; k--)
      if (text.data[k - 1] == '/') {
        component_start = k;
        break;
      }
    let const component_end = slash_after.value_or(text.length);
    /* The two-byte shape test runs first, so the common single-star field
       never pays the shopt lookup. */
    let const is_globstar_component = component_end - component_start == 2 &&
                                      text.data[component_start] == '*' &&
                                      text.data[component_start + 1] == '*' &&
                                      field.glob_active[component_start] &&
                                      field.glob_active[component_start + 1] &&
                                      is_shopt_enabled("globstar");

    if (is_globstar_component) {
      LOG(verbosity::All,
          "expanding a globstar component across directory levels");
      let const prefix = text.substring_of_length(0, component_start);
      let base = Path{StringView{"."}};
      if (component_start == 1)
        base = Path{StringView{"/"}};
      else if (component_start > 1)
        base = Path{text.substring_of_length(0, component_start - 1)};

      let const directory_position = slash_after.has_value();
      let relatives = ArrayList<String>{scratch};
      collect_globstar_paths(base, StringView{""}, directory_position, true, 0,
                             scratch, relatives);

      /* As a trailing ** each collected file or directory is a final match, the
         prefix the user wrote joined to the relative path. The base directory
         itself is the zero-level match, emitted as the bare prefix, and skipped
         when the prefix is empty so a bare ** does not yield the current
         directory. */
      if (!directory_position) {
        if (!prefix.is_empty()) {
          let base_field = glob_field{scratch};
          base_field.text.append(prefix);
          result.push(steal(base_field));
        }
        for (const String &relative : relatives) {
          let match_field = glob_field{scratch};
          match_field.text.append(prefix);
          match_field.text.append(relative.view());
          result.push(steal(match_field));
        }
        continue;
      }

      /* In a directory position the globstar stands in for zero or more levels,
         so the suffix after it is matched in the base directory and in every
         descendant directory. The empty relative is the zero-level case, which
         collapses the star pair and its slash to nothing rather than inserting
         a stray slash. */
      let const suffix = text.substring(*slash_after + 1);
      let rebuilt = ArrayList<glob_field>{scratch};
      for (const String &relative : relatives) {
        let candidate = glob_field{scratch};
        candidate.text.append(prefix);
        if (!relative.view().is_empty()) {
          candidate.text.append(relative.view());
          candidate.text += '/';
        }
        let const literal_length = candidate.text.count();
        candidate.text.append(suffix);
        if (candidate.text.is_empty()) continue;
        for (usize k = 0; k < literal_length; k++)
          candidate.glob_active.push(false);
        for (usize k = *slash_after + 1; k < field.glob_active.count(); k++)
          candidate.glob_active.push(field.glob_active[k]);
        rebuilt.push(steal(candidate));
      }
      let recursed = expand_path_recurse(steal(rebuilt));
      for (glob_field &f : recursed)
        result.push(steal(f));
      continue;
    }

    /* The glob is the last component, so expand it against files and emit the
       matches as is. */
    if (!slash_after) {
      let once = expand_path_once(field, true);
      for (glob_field &f : once)
        result.push(steal(f));
      continue;
    }

    /* Split off the first globbed directory component and the literal-or-glob
       suffix after it, building each from a substring rather than copying the
       whole field. */
    let const slash_offset = static_cast<std::ptrdiff_t>(*slash_after);
    let operating = glob_field{scratch};
    operating.text.append(StringView{text.data, *slash_after});
    for (std::ptrdiff_t k = 0; k < slash_offset; k++)
      operating.glob_active.push(field.glob_active[static_cast<usize>(k)]);
    let removed_suffix = glob_field{scratch};
    removed_suffix.text.append(
        StringView{text.data + *slash_after, text.length - *slash_after});
    for (usize k = static_cast<usize>(slash_offset);
         k < field.glob_active.count(); k++)
      removed_suffix.glob_active.push(field.glob_active[k]);

    let once = expand_path_once(operating, false);

    /* Bring back the removed suffix and recurse on the expanded entries. Each
       match came back all-literal with an empty mask, so restore its false
       entries before the suffix mask to keep the mask aligned with the text. */
    for (glob_field &f : once) {
      let const matched_length = f.text.count();
      f.text.append(removed_suffix.text.view());
      f.glob_active.clear();
      for (usize k = 0; k < matched_length; k++)
        f.glob_active.push(false);
      for (usize k = 0; k < removed_suffix.glob_active.count(); k++)
        f.glob_active.push(removed_suffix.glob_active[k]);
    }

    /* The recurse validates each level through the directory read or, for a
       literal suffix, the existence check above, so no extra stat is needed
       here. */
    let twice = expand_path_recurse(steal(once));
    for (glob_field &f : twice)
      result.push(steal(f));
  }

  return result;
}

fn EvalContext::expand_tilde(WordSegment &leading_segment,
                             bool word_continues) const throws -> void
{
  /* A tilde only expands when it is unquoted. An escaped or quoted tilde is a
     literal segment and stays as is. */
  if (!leading_segment.is_tilde_candidate()) return;

  let &text = leading_segment.text;
  if (text.is_empty() || text[0] != '~') return;

  /* The user name runs from after the ~ to the first / or the end of the word,
     so ~ and ~/path carry an empty name while ~user and ~user/path carry the
     name. */
  usize name_end = 1;
  while (name_end < text.length() && text[name_end] != '/')
    name_end++;
  let const name = text.view().substring_of_length(1, name_end - 1);

  /* A tilde prefix that runs to the segment's end while the word goes on in a
     later segment carries a quoted or escaped character inside the prefix, so
     bash leaves the whole word literal, the way ~ch\et and ~chet""/bar
     stay as written. */
  if (name_end == text.length() && word_continues) return;

  let const directory = resolve_tilde_prefix(name);
  if (name.is_empty() && !directory)
    throw Error{"Could not figure out home directory"};
  if (!directory) return;

  LOG(verbosity::All, "the tilde prefix '~%.*s' expands to '%.*s'",
      static_cast<int>(name.length), name.data,
      static_cast<int>(directory->view().length), directory->view().data);
  /* String has no in-place erase or insert, so the directory and the
     remainder after the name are joined into a fresh buffer and moved back. */
  let expanded = String{heap_allocator()};
  expanded.append(directory->view());
  expanded.append(text.view().substring(name_end));
  text = steal(expanded);
}

fn EvalContext::resolve_tilde_prefix(StringView name) const throws
    -> Maybe<String>
{
  /* ~+ is PWD and ~- is OLDPWD the way bash reads them, with an unset name
     leaving the prefix literal. */
  if (name == "+" || name == "-")
    return get_variable_value(name == "+" ? StringView{"PWD"}
                                          : StringView{"OLDPWD"});
  /* An empty name is the bare ~, which resolves to the current home. A named
     user resolves through the system database, and an unknown name yields
     nothing the way bash leaves ~baduser literal. */
  let const home =
      name.is_empty() ? os::get_home_directory() : os::get_home_for_user(name);
  if (!home) return shit::None;
  return String{heap_allocator(), home->text().view()};
}

fn EvalContext::expand_colon_tildes(WordSegment &segment,
                                    bool word_continues) const throws -> void
{
  if (!segment.is_tilde_candidate()) return;
  let const view = segment.text.view();
  let rewritten = String{heap_allocator()};
  let changed = false;
  usize i = 0;
  while (i < view.length) {
    if (view[i] == ':' && i + 1 < view.length && view[i + 1] == '~') {
      usize prefix_end = i + 2;
      while (prefix_end < view.length && view[prefix_end] != '/' &&
             view[prefix_end] != ':')
        prefix_end++;
      /* A prefix that runs to the segment's end while the word goes on in a
         later segment carries a quoted character, so it stays literal the way
         the leading prefix does. */
      if (!(prefix_end == view.length && word_continues)) {
        let const name = view.substring_of_length(i + 2, prefix_end - i - 2);
        if (let const directory = resolve_tilde_prefix(name)) {
          rewritten += ':';
          rewritten.append(directory->view());
          i = prefix_end;
          changed = true;
          continue;
        }
      }
    }
    rewritten += view[i];
    i++;
  }
  if (changed) {
    LOG(verbosity::All,
        "rewrote colon tilde prefixes in an assignment value");
    segment.text = steal(rewritten);
  }
}

hot fn EvalContext::expand_path(glob_field field,
                                SourceLocation location) throws
    -> ArrayList<String>
{
  let const scratch = scratch_allocator();

  /* Fast path. A field with no glob that actually matches paths is its own
     single result, so it skips the recursion, the directory scan, and every
     copy. A bare command word such as '[' lands here instead of scanning the
     current directory. */
  let const has_glob =
      m_enable_path_expansion &&
      first_active_glob(field.text.view(), field.glob_active, extglob_enabled())
          .has_value();

  if (!has_glob) {
    let single = ArrayList<String>{scratch};
    single.push(steal(field.text));
    return single;
  }

  /* The pattern is kept so a glob that matches None falls back to it, since
     the field moves into the recurse. */
  let pattern = String{scratch};
  pattern.append(field.text.view());

  let input = ArrayList<glob_field>{scratch};
  input.push(steal(field));
  let fields = expand_path_recurse(steal(input));

  let values = ArrayList<String>{scratch};
  for (glob_field &f : fields)
    values.push(steal(f.text));

  /* Sort the matches in byte order, which is the POSIX collating order in the C
     locale and what dash produces. A plain compare also keeps a large expansion
     from spending most of its time in the sort comparator. */
  utils::sort_ascending(values);

  LOG(verbosity::All, "the glob pattern '%s' matched %zu paths",
      pattern.c_str(), values.count());

  /* A glob that matches no file is a hard error by default, the typo-catching
     behavior. With failglob off the shell takes the POSIX fallback and expands
     the glob to its literal pattern as a single field, the way dash does. The
     caret points at the offending word. A test or [ command is exempt, so a
     glob used to probe whether a file exists keeps its literal text in silence
     and the probe returns false rather than aborting the command. */
  if (values.count() == 0) {
    /* The test exemption keeps the literal fallback in silence. Otherwise the
       failglob strictness throws, or -W downgrades it to a warning unless the
       set -o failglob was the script's explicit ask. */
    if (!m_glob_exempt_for_test)
      warn_or_throw(m_failglob, m_failglob_explicit, location,
                    "the glob pattern '" + pattern +
                        "' matched no file, it expands to its literal text, "
                        "which is rarely intended. Probe for matches with "
                        "compgen -G '" +
                        pattern + "' or relax with set +o failglob");
    values.push(steal(pattern));
  }

  return values;
}

/* The compgen -G probe, a glob expansion that reports matches and never trips
   failglob. A pattern with no metacharacter names one file, so it reports
   that file only when it exists, the way bash compgen -G does. set -f does not
   apply, since the caller asks for the expansion explicitly. The matches land
   on the scratch arena and die with the command. */
fn EvalContext::expand_glob_lenient(StringView pattern) throws
    -> ArrayList<String>
{
  let const scratch = scratch_allocator();
  let values = ArrayList<String>{scratch};

  let field = glob_field{scratch};
  field.text.append(pattern);
  for (usize i = 0; i < pattern.length; i++)
    field.glob_active.push(true);

  if (!first_active_glob(field.text.view(), field.glob_active,
                         extglob_enabled())
           .has_value())
  {
    LOG(verbosity::Debug,
        "compgen -G probe of '%.*s' has no glob, checking existence",
        static_cast<int>(pattern.length), pattern.data);
    if (Path{pattern}.exists()) values.push(String{scratch, pattern});
    return values;
  }

  let input = ArrayList<glob_field>{scratch};
  input.push(steal(field));
  for (glob_field &f : expand_path_recurse(steal(input)))
    values.push(steal(f.text));
  utils::sort_ascending(values);
  LOG(verbosity::Debug, "compgen -G probe matched %zu paths", values.count());
  return values;
}

namespace {

/* The count of leading bytes that are digits in the given radix, so a value
   with trailing non-digit bytes reads only its numeric prefix the way base-0
   strtoll did. A hexadecimal scan accepts both letter cases. */
pure fn count_leading_digits(StringView text, u32 radix) wontthrow -> usize
{
  usize length = 0;
  while (length < text.length) {
    let const c = text[length];
    u32 digit;
    if (c >= '0' && c <= '9')
      digit = static_cast<u32>(c - '0');
    else if (c >= 'a' && c <= 'f')
      digit = static_cast<u32>(c - 'a') + 10;
    else if (c >= 'A' && c <= 'F')
      digit = static_cast<u32>(c - 'A') + 10;
    else
      break;
    if (digit >= radix) break;
    length++;
  }
  return length;
}

/* Read a numeric operand the way base-0 strtoll did, detecting the radix from
   the prefix so a leading 0x reads as hexadecimal, a leading 0 reads as octal,
   and anything else reads as decimal. Only the leading run of valid digits is
   read, so a trailing non-digit suffix is ignored rather than rejected. A value
   with no leading digit or one that overflows reads as zero, the same result
   the old strtoll path produced after its throw was caught. The utils parsers
   take no base argument, so the radix is chosen here from the prefix and the
   matching parser runs on the scanned digit run. */
pure fn parse_arithmetic_operand(StringView text) wontthrow -> i64
{
  let body = text;
  let is_negative = false;
  if (body.length > 0 && (body[0] == '+' || body[0] == '-')) {
    is_negative = body[0] == '-';
    body = body.substring(1);
  }

  let const parsed = [&]() -> ErrorOr<i64> {
    if (body.length >= 2 && body[0] == '0' &&
        (body[1] == 'x' || body[1] == 'X'))
    {
      let const digits = body.substring(2);
      return utils::parse_hexadecimal_integer(
          digits.substring_of_length(0, count_leading_digits(digits, 16)));
    }
    if (body.length >= 1 && body[0] == '0')
      return utils::parse_octal_integer(
          body.substring_of_length(0, count_leading_digits(body, 8)));
    return utils::parse_decimal_integer(
        body.substring_of_length(0, count_leading_digits(body, 10)));
  }();

  if (parsed.is_error()) return 0;
  return is_negative ? -parsed.value() : parsed.value();
}

/* Signed arithmetic in $((...)) wraps two's-complement the way dash does, so
   the add, subtract, and multiply run in u64 where overflow is defined and the
   result casts back to i64. A direct i64 overflow would be undefined and trips
   UBSan in the dbg build. */
pure fn arithmetic_add(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) + static_cast<u64>(rhs));
}

pure fn arithmetic_subtract(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) - static_cast<u64>(rhs));
}

pure fn arithmetic_multiply(i64 lhs, i64 rhs) wontthrow -> i64
{
  return static_cast<i64>(static_cast<u64>(lhs) * static_cast<u64>(rhs));
}

/* Exponentiation by squaring in u64 so the result wraps in 64 bits the way the
   other operators do. The caller rejects a negative exponent, so the count is
   non-negative here. */
pure fn arithmetic_power(i64 base, i64 exponent) wontthrow -> i64
{
  let result = static_cast<u64>(1);
  let factor = static_cast<u64>(base);
  let remaining = static_cast<u64>(exponent);
  while (remaining > 0) {
    if ((remaining & 1u) != 0) result *= factor;
    factor *= factor;
    remaining >>= 1;
  }
  return static_cast<i64>(result);
}

/* INT64_MIN / -1 and INT64_MIN % -1 overflow the signed result and trap on
   x86, so the wrapped values are returned directly. Division yields INT64_MIN
   and modulo yields 0, which is the two's-complement wrap. */
pure fn arithmetic_divide(i64 lhs, i64 rhs) wontthrow -> i64
{
  if (lhs == INT64_MIN && rhs == -1) return INT64_MIN;
  return lhs / rhs;
}

pure fn arithmetic_modulo(i64 lhs, i64 rhs) wontthrow -> i64
{
  if (lhs == INT64_MIN && rhs == -1) return 0;
  return lhs % rhs;
}

/* dash masks the shift count to the low 6 bits, so a count of 64 shifts by 0
   and a negative count shifts by its low 6 bits. The shift runs in u64 where a
   shift by a value below the width is defined, and for the right shift the sign
   is carried by hand so a negative operand keeps its arithmetic-shift result.
 */
pure fn arithmetic_shift_left(i64 lhs, i64 rhs) wontthrow -> i64
{
  let const count = static_cast<u64>(rhs) & 63u;
  return static_cast<i64>(static_cast<u64>(lhs) << count);
}

pure fn arithmetic_shift_right(i64 lhs, i64 rhs) wontthrow -> i64
{
  let const count = static_cast<u64>(rhs) & 63u;
  let const is_negative = lhs < 0;
  let value = static_cast<u64>(lhs) >> count;
  if (is_negative && count > 0) value |= ~(~static_cast<u64>(0) >> count);
  return static_cast<i64>(value);
}

/* A recursive-descent evaluator for $((...)), following C operator precedence,
   that resolves and assigns shell variables through the context. */
class ArithmeticParser
{
public:
  /* Null only on the analyze-time constant fold, where the expression holds no
     variable and no assignment, so neither read_variable_value nor the
     assignment path that dereferences the context is ever reached. */
  EvalContext *context;
  StringView source;
  usize pos;

  /* A parenthesized subexpression descends through parse_primary, so a source
     such as thousands of open parentheses would overflow the native stack. The
     depth is counted at each primary and capped before the recursion. */
  usize depth{0};
  static constexpr usize MAX_DEPTH = 512;

  /* The dead operand of a short-circuited || or && and the untaken arm of a
     ternary are still parsed so their tokens are consumed, but an assignment
     inside them must not take effect. While this flag is set the assignment
     path skips the store, matching the side-effect semantics dash gives. */
  bool m_is_skipping{false};

  [[noreturn]] cold fn fail(StringView message) throws -> void
  {
    throw Error{"Arithmetic: " + message};
  }

  fn skip_spaces() wontthrow -> void
  {
    while (pos < source.length && (source[pos] == ' ' || source[pos] == '\t' ||
                                   source[pos] == '\n' || source[pos] == '\r'))
      pos++;
  }

  fn starts_with(StringView op) wontthrow -> bool
  {
    skip_spaces();
    if (pos + op.length > source.length) return false;
    /* The operator probes run hot with one to three bytes each, so the
       compare is an unrolled byte loop the compiler keeps inline rather than
       a memcmp call per probe. */
    for (usize k = 0; k < op.length; k++)
      if (source[pos + k] != op[k]) return false;
    return true;
  }

  fn consume(StringView op) wontthrow -> bool
  {
    if (!starts_with(op)) return false;
    pos += op.length;
    return true;
  }

  fn read_variable_value(StringView name) throws -> i64
  {
    /* A plain shell variable, the common operand, reads its digits straight
       from the stored value with no copy. The operand parser stops at the first
       non-digit and reads a non-numeric value as zero, which matches the old
       strtoll path. */
    ASSERT(context != nullptr);
    if (let const *stored = context->lookup_shell_variable(name)) {
      if (stored->count() == 0) return 0;
      return parse_arithmetic_operand(stored->view());
    }

    let const value = context->get_variable_value(name);
    if (!value.has_value()) {
      /* An unset name in arithmetic goes through the same reporter as an unset
         parameter expansion, so $((nope)) is not silently zero under the
         strict mood. A skipped ternary branch reads without effect the way its
         assignments are suppressed, so it never reports. */
      if (!m_is_skipping) context->report_unset_reference(name);
      return 0;
    }
    if (value->is_empty()) return 0;
    return parse_arithmetic_operand(value->view());
  }

  /* The name of the variable at the cursor, or an empty view when no name sits
     there. Used as the target of an increment or a decrement. */
  fn read_lvalue_name() wontthrow -> StringView
  {
    skip_spaces();
    if (pos >= source.length || !lexer::is_variable_name_start(source[pos]))
      return StringView{};
    let const name_start = pos;
    while (pos < source.length && lexer::is_variable_name(source[pos]))
      pos++;
    return source.substring_of_length(name_start, pos - name_start);
  }

  /* Store a new integer value for a name unless the parser is walking a skipped
     branch of a ternary, where the assignment must not take effect. */
  fn write_variable(StringView name, i64 value) throws -> void
  {
    if (m_is_skipping) return;
    ASSERT(context != nullptr);
    /* The store copies the value into its own heap String, so the conversion
       writes into a stack buffer and passes a view, with no transient heap
       allocation at all. */
    char buffer[24];
    context->set_shell_variable(
        name, utils::int_to_text_into(value, buffer, sizeof(buffer)));
  }

  /* An assignment or step target, a name and an optional [subscript] for an
     array element. The subscript is the raw bytes between the brackets, which
     the evaluator expands when the element is read or written. */
  struct lvalue
  {
    StringView name;
    Maybe<StringView> subscript;
  };

  /* A [subscript] right after a name, with the bracket content returned as a
     view, or None when no bracket sits there. Nested brackets are balanced so
     a[b[0]] reads the whole inner expression. */
  fn read_optional_subscript() throws -> Maybe<StringView>
  {
    if (pos >= source.length || source[pos] != '[') return None;
    pos++;
    let const inner_start = pos;
    usize depth = 1;
    while (pos < source.length && depth > 0) {
      if (source[pos] == '[')
        depth++;
      else if (source[pos] == ']' && --depth == 0)
        break;
      pos++;
    }
    if (depth != 0) fail("expected ']' after an array subscript");
    let const subscript =
        source.substring_of_length(inner_start, pos - inner_start);
    pos++;
    return subscript;
  }

  /* A name at the cursor with its optional array subscript. */
  fn read_lvalue() throws -> lvalue
  {
    let const name = read_lvalue_name();
    if (name.is_empty()) return lvalue{name, None};
    return lvalue{name, read_optional_subscript()};
  }

  fn read_lvalue_value(const lvalue &target) throws -> i64
  {
    if (target.subscript.has_value()) {
      ASSERT(context != nullptr);
      return context->read_array_element_integer(target.name,
                                                 *target.subscript);
    }
    return read_variable_value(target.name);
  }

  fn write_lvalue(const lvalue &target, i64 value) throws -> void
  {
    if (m_is_skipping) return;
    if (target.subscript.has_value()) {
      ASSERT(context != nullptr);
      char buffer[24];
      context->assign_array_element(
          target.name, *target.subscript,
          utils::int_to_text_into(value, buffer, sizeof(buffer)), false);
      return;
    }
    write_variable(target.name, value);
  }

  /* A prefix ++ or -- changes the variable and yields the new value. */
  fn prefix_step(i64 delta) throws -> i64
  {
    const lvalue target = read_lvalue();
    if (target.name.is_empty()) fail("expected a variable after '++' or '--'");
    const i64 updated = arithmetic_add(read_lvalue_value(target), delta);
    write_lvalue(target, updated);
    return updated;
  }

  /* A postfix ++ or -- yields the old value and then changes the variable. */
  fn postfix_step(const lvalue &target, i64 delta) throws -> i64
  {
    const i64 original = read_lvalue_value(target);
    write_lvalue(target, arithmetic_add(original, delta));
    return original;
  }

  fn parse() throws -> i64
  {
    /* An empty expression is zero, the way bash reads $(( )) and ${a[$unset]}
       with the subscript expanding to nothing. */
    skip_spaces();
    if (pos == source.length) return 0;
    let const result = parse_comma();
    skip_spaces();
    if (pos != source.length) fail("unexpected trailing characters");
    return result;
  }

  /* The comma operator evaluates each subexpression in order and yields the
     last, the lowest precedence so a C-style for clause such as i=0, j=10
     runs both assignments. */
  fn parse_comma() throws -> i64
  {
    i64 result = parse_assignment();
    while (consume(","))
      result = parse_assignment();
    return result;
  }

  fn apply_compound(i64 lhs, i64 rhs, char kind) throws -> i64
  {
    switch (kind) {
    case '+': return arithmetic_add(lhs, rhs);
    case '-': return arithmetic_subtract(lhs, rhs);
    case '*': return arithmetic_multiply(lhs, rhs);
    case '/':
      if (rhs == 0) fail("division by zero");
      return arithmetic_divide(lhs, rhs);
    case '%':
      if (rhs == 0) fail("division by zero");
      return arithmetic_modulo(lhs, rhs);
    case '&': return lhs & rhs;
    case '|': return lhs | rhs;
    case '^': return lhs ^ rhs;
    case 'L': return arithmetic_shift_left(lhs, rhs);
    case 'R': return arithmetic_shift_right(lhs, rhs);
    default: return rhs;
    }
  }

  fn parse_assignment() throws -> i64
  {
    /* An assignment has a bare variable name on the left, so try it and rewind
       when the name is not followed by an assignment operator. */
    let const save = pos;
    skip_spaces();
    if (pos < source.length && lexer::is_variable_name_start(source[pos])) {
      /* The name is a contiguous slice of the expression the parser holds for
         the whole evaluation, so a view into it avoids a per-read allocation.
       */
      let const name_start = pos;
      while (pos < source.length && lexer::is_variable_name(source[pos]))
        pos++;
      let const name = source.substring_of_length(name_start, pos - name_start);
      const lvalue target{name, read_optional_subscript()};

      struct compound_operator
      {
        StringView token;
        u8 kind;
      };
      static const compound_operator compound_operators[] = {
          {"<<=", 'L'},
          {">>=", 'R'},
          {"+=",  '+'},
          {"-=",  '-'},
          {"*=",  '*'},
          {"/=",  '/'},
          {"%=",  '%'},
          {"&=",  '&'},
          {"|=",  '|'},
          {"^=",  '^'},
      };
      /* The probe loop runs only when the next byte can open a compound
         operator, so the common plain assignment and the bare-name read skip
         the ten probes. */
      skip_spaces();
      let const next_byte = pos < source.length ? source[pos] : '\0';
      if (next_byte == '<' || next_byte == '>' || next_byte == '+' ||
          next_byte == '-' || next_byte == '*' || next_byte == '/' ||
          next_byte == '%' || next_byte == '&' || next_byte == '|' ||
          next_byte == '^')
      {
        for (const auto &[op, kind] : compound_operators) {
          if (consume(op)) {
            let const rhs = parse_assignment();
            let const result =
                apply_compound(read_lvalue_value(target), rhs, kind);
            write_lvalue(target, result);
            return result;
          }
        }
      }
      if (starts_with("=") && !starts_with("==")) {
        consume("=");
        let const rhs = parse_assignment();
        write_lvalue(target, rhs);
        return rhs;
      }
      pos = save;
    }
    return parse_ternary();
  }

  /* Parse an operand while suppressing its assignments so its tokens are
     consumed without taking effect. The flag is saved and restored, so a
     nested skip region inside an already-skipped one stays skipped. */
  fn parse_skipped(i64 (ArithmeticParser::*parse_branch)()) throws -> i64
  {
    let const was_skipping = m_is_skipping;
    m_is_skipping = true;
    defer { m_is_skipping = was_skipping; };
    return (this->*parse_branch)();
  }

  fn parse_ternary() throws -> i64
  {
    let const condition = parse_binary(1);
    if (consume("?")) {
      /* Only the taken arm runs, the other arm is parsed but suppressed so an
         assignment in the dead arm leaves the variable unchanged. */
      if (condition != 0) {
        let const if_true = parse_assignment();
        if (!consume(":")) fail("expected ':' in a conditional");
        let const if_false = parse_skipped(&ArithmeticParser::parse_ternary);
        unused(if_false);
        return if_true;
      }
      let const if_true = parse_skipped(&ArithmeticParser::parse_assignment);
      unused(if_true);
      if (!consume(":")) fail("expected ':' in a conditional");
      return parse_ternary();
    }
    return condition;
  }

  /* One binary operator at the cursor, its dispatch tag, its precedence with
     11 the tightest ** and 1 the loosest ||, and its token length. Precedence
     0 means no binary operator opens here. */
  struct binary_operator
  {
    char kind;
    u8 precedence;
    u8 length;
  };

  /* Reads the operator at the cursor without consuming it. The byte pair
     decides the doubled forms, so | against || and < against << need no
     rescans, and a compound assignment suffix such as += or <<= answers no
     operator since the assignment level above the ladder owns those. */
  fn peek_binary_operator() wontthrow -> binary_operator
  {
    skip_spaces();
    if (pos >= source.length) return {0, 0, 0};
    let const a = source[pos];
    let const b = pos + 1 < source.length ? source[pos + 1] : '\0';
    let const c = pos + 2 < source.length ? source[pos + 2] : '\0';
    switch (a) {
    case '*':
      if (b == '*') return {'P', 11, 2};
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'*', 10, 1};
    case '/':
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'/', 10, 1};
    case '%':
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'%', 10, 1};
    case '+':
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'+', 9, 1};
    case '-':
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'-', 9, 1};
    case '<':
      if (b == '<')
        return c == '=' ? binary_operator{0, 0, 0} : binary_operator{'L', 8, 2};
      if (b == '=') return {'l', 7, 2};
      return {'<', 7, 1};
    case '>':
      if (b == '>')
        return c == '=' ? binary_operator{0, 0, 0} : binary_operator{'R', 8, 2};
      if (b == '=') return {'g', 7, 2};
      return {'>', 7, 1};
    case '=':
      return b == '=' ? binary_operator{'e', 6, 2} : binary_operator{0, 0, 0};
    case '!':
      return b == '=' ? binary_operator{'n', 6, 2} : binary_operator{0, 0, 0};
    case '&':
      if (b == '&') return {'A', 2, 2};
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'&', 5, 1};
    case '^':
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'^', 4, 1};
    case '|':
      if (b == '|') return {'O', 1, 2};
      return b == '=' ? binary_operator{0, 0, 0} : binary_operator{'|', 3, 1};
    default: return {0, 0, 0};
    }
  }

  /* The binary ladder as one precedence-climbing loop, one frame for a whole
     run of operators instead of a call per precedence level, since the chain
     of nine cascade levels showed up whole in the profile. ** climbs right
     associatively by re-entering at its own precedence, the logical pair
     short-circuits by parsing the dead side under suppression the way the
     cascade did, and the ternary, assignment, and comma levels stay above. */
  fn parse_binary(u8 min_precedence) throws -> i64
  {
    let lhs = parse_unary();
    for (;;) {
      let const op = peek_binary_operator();
      if (op.precedence < min_precedence) return lhs;
      pos += op.length;

      if (op.kind == 'A' || op.kind == 'O') {
        /* The dead side is parsed under suppression so its tokens are
           consumed without its assignments taking effect. */
        let const lhs_decides = (op.kind == 'A') == (lhs == 0);
        i64 rhs = 0;
        if (lhs_decides) {
          let const was_skipping = m_is_skipping;
          m_is_skipping = true;
          defer { m_is_skipping = was_skipping; };
          rhs = parse_binary(op.precedence + 1);
        } else {
          rhs = parse_binary(op.precedence + 1);
        }
        lhs = op.kind == 'A' ? ((lhs != 0 && rhs != 0) ? 1 : 0)
                             : ((lhs != 0 || rhs != 0) ? 1 : 0);
        continue;
      }

      /* ** is right-associative, so it re-enters at its own precedence, and
         bash rejects a negative exponent in integer arithmetic. */
      let const rhs =
          parse_binary(op.kind == 'P' ? op.precedence : op.precedence + 1);
      switch (op.kind) {
      case 'P':
        if (rhs < 0) fail("exponent less than 0");
        lhs = arithmetic_power(lhs, rhs);
        break;
      case '*': lhs = arithmetic_multiply(lhs, rhs); break;
      case '/':
        if (rhs == 0) fail("division by zero");
        lhs = arithmetic_divide(lhs, rhs);
        break;
      case '%':
        if (rhs == 0) fail("division by zero");
        lhs = arithmetic_modulo(lhs, rhs);
        break;
      case '+': lhs = arithmetic_add(lhs, rhs); break;
      case '-': lhs = arithmetic_subtract(lhs, rhs); break;
      case 'L': lhs = arithmetic_shift_left(lhs, rhs); break;
      case 'R': lhs = arithmetic_shift_right(lhs, rhs); break;
      case '<': lhs = lhs < rhs ? 1 : 0; break;
      case 'l': lhs = lhs <= rhs ? 1 : 0; break;
      case '>': lhs = lhs > rhs ? 1 : 0; break;
      case 'g': lhs = lhs >= rhs ? 1 : 0; break;
      case 'e': lhs = lhs == rhs ? 1 : 0; break;
      case 'n': lhs = lhs != rhs ? 1 : 0; break;
      case '&': lhs = lhs & rhs; break;
      case '^': lhs = lhs ^ rhs; break;
      case '|': lhs = lhs | rhs; break;
      default: unreachable();
      }
    }
  }

  fn parse_unary() throws -> i64
  {
    /* The doubled operators are checked before the single + and - so a leading
       ++ or -- is read as one prefix step rather than two unary signs. The
       first byte gates the probes, so the common operand pays one read. */
    skip_spaces();
    let const first = pos < source.length ? source[pos] : '\0';
    if (first == '+') {
      if (consume("++")) return prefix_step(1);
      pos++;
      return parse_unary();
    }
    if (first == '-') {
      if (consume("--")) return prefix_step(-1);
      pos++;
      return arithmetic_subtract(0, parse_unary());
    }
    if (first == '!') {
      pos++;
      return parse_unary() == 0 ? 1 : 0;
    }
    if (first == '~') {
      pos++;
      return ~parse_unary();
    }
    return parse_primary();
  }

  fn parse_primary() throws -> i64
  {
    depth++;
    defer { depth--; };
    if (depth > MAX_DEPTH) fail("expression nested too deeply");

    skip_spaces();
    if (consume("(")) {
      let const value = parse_comma();
      if (!consume(")")) fail("expected ')'");
      return value;
    }
    if (pos < source.length && lexer::is_number(source[pos])) {
      /* The literal starts at pos and runs while its digits are valid in the
         radix the prefix selects, matching the prefix and the consumed length
         that base-0 strtoll reported. The utils parsers take no base and report
         no consumed length, so the run is measured here and the matching parser
         runs on the scanned slice. */
      let const rest = source.substring(pos);

      /* The base#digits form selects an explicit radix from 2 to 64, so 16#ff
         is 255 and 2#101 is 5. The decimal run before the # is the base and the
         digits after it read in that base, with a-z worth 10 to 35, A-Z worth
         36 to 61 above base 36 or the same as a-z at or below it, @ worth 62,
         and _ worth 63. */
      if (const usize base_length = count_leading_digits(rest, 10);
          base_length > 0 && base_length < rest.length &&
          rest[base_length] == '#')
      {
        const i64 base =
            parse_arithmetic_operand(rest.substring_of_length(0, base_length));
        if (base < 2 || base > 64)
          fail("an arithmetic base must be between 2 and 64");
        auto digit_value = [base](char c) -> i64 {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'a' && c <= 'z') return c - 'a' + 10;
          if (c >= 'A' && c <= 'Z')
            return base <= 36 ? c - 'A' + 10 : c - 'A' + 36;
          if (c == '@') return 62;
          if (c == '_') return 63;
          return -1;
        };
        i64 value = 0;
        usize i = base_length + 1;
        while (i < rest.length) {
          const i64 digit = digit_value(rest[i]);
          if (digit < 0 || digit >= base) break;
          value = value * base + digit;
          i++;
        }
        pos += i;
        return value;
      }

      usize consumed = 0;
      if (rest.length >= 2 && rest[0] == '0' &&
          (rest[1] == 'x' || rest[1] == 'X'))
        consumed = 2 + count_leading_digits(rest.substring(2), 16);
      else if (rest.length >= 1 && rest[0] == '0')
        consumed = count_leading_digits(rest, 8);
      else
        consumed = count_leading_digits(rest, 10);

      let const value =
          parse_arithmetic_operand(rest.substring_of_length(0, consumed));
      pos += consumed;
      return value;
    }
    if (pos < source.length && lexer::is_variable_name_start(source[pos])) {
      /* The name is a contiguous slice of the expression the parser holds for
         the whole evaluation, so a view into it avoids a per-read allocation. A
         trailing ++ or -- right after the name is a postfix step on it. */
      const lvalue target = read_lvalue();
      if (consume("++")) return postfix_step(target, 1);
      if (consume("--")) return postfix_step(target, -1);
      return read_lvalue_value(target);
    }
    fail("unexpected character");
  }
};

} /* namespace */

fn EvalContext::read_array_element_integer(StringView name,
                                           StringView subscript) throws -> i64
{
  return parse_arithmetic_operand(
      apply_array_subscript(name, subscript).view());
}

fn EvalContext::evaluate_arithmetic(StringView expression) throws -> i64
{
  LOG(verbosity::All, "evaluating the arithmetic expression of %zu bytes",
      expression.length);
  /* Parameter expansion runs first, so a $1, a $x, or a ${...} inside the
     arithmetic becomes its value before the expression is parsed. A bare name
     is still resolved during evaluation. When the source holds no parameter to
     expand, which the d=$((d+1)) hot loop hits every iteration, the expansion
     copy is skipped and the original is parsed directly. */
  if (!expression.find_character('$').has_value() &&
      !expression.find_character('`').has_value())
  {
    let parser = ArithmeticParser{this, expression, 0};
    return parser.parse();
  }

  /* The expanded word owns the bytes the parser views, so it outlives the
     parser below. */
  LOG(verbosity::All,
      "expanding parameters inside the arithmetic before the parse");
  let const expanded_word = expand_modifier_word(expression);
  let parser = ArithmeticParser{this, expanded_word.view(), 0};
  return parser.parse();
}

fn evaluate_constant_arithmetic(StringView expression) throws -> i64
{
  /* The optimizer has already proven the expression holds no variable and no
     assignment, so the parser never dereferences its context and a null one is
     safe. */
  let parser = ArithmeticParser{nullptr, expression, 0};
  return parser.parse();
}

hot fn EvalContext::expand_word(const Word &word) throws
    -> ArrayList<glob_field>
{
  LOG(verbosity::All, "expanding a word of %zu segments into fields",
      word.segments.count());
  let const scratch = scratch_allocator();

  /* Only copy the segments when a leading tilde must be rewritten. The common
     word has no tilde and reads its segments in place. The copy, when it
     happens, lives only until this word finishes expanding, so it goes on the
     scratch arena the command reclaims rather than the heap. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{scratch};
  if (!word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front(),
                 tilde_expanded_segments.count() > 1);
    segments = &tilde_expanded_segments;
  }

  let fields = ArrayList<glob_field>{scratch};
  let current = glob_field{scratch};
  let has_current = false;

  auto flush = [&]() {
    if (has_current) {
      fields.push(steal(current));
      current = glob_field{scratch};
      has_current = false;
    }
  };

  auto append_run = [&](StringView text, bool glob_active) {
    current.text.append(text);
    current.glob_active.reserve(current.glob_active.count() + text.length);
    for (usize k = 0; k < text.length; k++)
      current.glob_active.push(glob_active);
    has_current = true;
  };

  /* A field with no bytes is still pushed, which a non-whitespace IFS delimiter
     run needs so that an empty field between two delimiters survives. flush
     alone emits only a started field and so cannot stand in here. */
  auto emit_empty_field = [&]() { fields.push(glob_field{scratch}); };

  /* IFS whitespace folds and a non-whitespace IFS byte delimits one field each,
     matching dash. A single forward pass classifies every byte as a field byte,
     a whitespace separator, or a delimiter separator, and emits one field per
     run. A whitespace run that holds no delimiter ends the current field. A run
     that holds k delimiters ends the current field and emits k minus one empty
     fields, so a:b yields two fields and a::b yields an empty between them. A
     leading delimiter forces a leading empty field, and a trailing whitespace
     run or a trailing single delimiter leaves no empty field behind. */
  auto append_split_run = [&](StringView text, bool glob_active) {
    usize i = 0;
    while (i < text.length) {
      const char byte = text.data[i];
      if (!is_field_separator(byte)) {
        usize start = i;
        while (i < text.length && !is_field_separator(text.data[i]))
          i++;
        append_run(StringView{text.data + start, i - start}, glob_active);
        continue;
      }

      /* Count the delimiters inside the maximal separator run, since each one
         past the first marks an empty field. A whitespace byte only folds. */
      const bool was_field_started = has_current;
      usize delimiter_count = 0;
      while (i < text.length && is_field_separator(text.data[i])) {
        const char separator = text.data[i];
        if (separator != ' ' && separator != '\t' && separator != '\n')
          delimiter_count++;
        i++;
      }

      /* The accumulated field ends here whether the run folds or delimits. */
      flush();
      if (delimiter_count == 0) continue;

      /* The first delimiter that follows an empty field forces that empty field
         out, so a leading delimiter and a delimiter after another delimiter
         both keep their empty. A delimiter that closes a non-empty field adds
         no extra empty, since flush already emitted that field. Each further
         delimiter in the run marks one more empty field. */
      if (!was_field_started) emit_empty_field();
      for (usize k = 1; k < delimiter_count; k++)
        emit_empty_field();
    }
  };

  for (const WordSegment &segment : *segments) {
    let const segment_text =
        StringView{segment.text.data(), segment.text.count()};
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      append_run(segment_text, false);
      break;
    case WordSegment::Kind::UnquotedText:
      append_split_run(segment_text, true);
      break;
    case WordSegment::Kind::VariableReference: {
      /* "$@" expands to one field per positional parameter. The first joins any
         preceding text, the last leaves its field open for following text. */
      if (segment.text == "@" && segment.is_in_double_quotes) {
        for (usize i = 0; i < m_positional_params.count(); i++) {
          if (i > 0) flush();
          append_run(StringView{m_positional_params[i].data(),
                                m_positional_params[i].count()},
                     false);
        }
        break;
      }
      /* An unquoted $@ or $* keeps each positional parameter as its own field
         boundary, then field splits each parameter's own text under IFS.
         Routing it through a single joined string instead would lose the
         boundary, so a custom or an empty IFS would merge or mis-split the
         parameters. The quoted "$*" join by the first IFS character stays in
         the default branch below. */
      if ((segment.text == "@" || segment.text == "*") &&
          !segment.is_in_double_quotes)
      {
        for (usize i = 0; i < m_positional_params.count(); i++) {
          if (i > 0) flush();
          append_split_run(StringView{m_positional_params[i].data(),
                                      m_positional_params[i].count()},
                           true);
        }
        break;
      }
      /* "${!prefix@}" emits one field per matching variable name, the way "$@"
         and "${a[@]}" do, while "${!prefix*}" joins them by the first IFS
         character into one field. The general path returns a single
         space-joined string, which loses the per-name boundary, so the name
         listing is emitted here. The form is a leading '!' and a trailing '@'
         or '*', which the indirect ${!ref} and the array-key ${!a[@]} do not
         take. */
      if (segment_text.length >= 2 && segment_text[0] == '!' &&
          (segment_text[segment_text.length - 1] == '@' ||
           segment_text[segment_text.length - 1] == '*'))
      {
        const StringView prefix =
            segment_text.substring_of_length(1, segment_text.length - 2);
        let const is_star = segment_text[segment_text.length - 1] == '*';
        let const names = matching_prefix_names(prefix);
        if (segment.is_in_double_quotes && is_star) {
          let const ifs = m_field_separators.view();
          let joined = String{scratch_allocator()};
          for (usize i = 0; i < names.count(); i++) {
            if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
            joined.append(names[i].view());
          }
          append_run(joined, false);
        } else if (segment.is_in_double_quotes) {
          for (usize i = 0; i < names.count(); i++) {
            if (i > 0) flush();
            append_run(names[i].view(), false);
          }
        } else {
          for (usize i = 0; i < names.count(); i++) {
            if (i > 0) flush();
            append_split_run(names[i].view(), true);
          }
        }
        break;
      }
      /* "${!a[@]}" emits one field per subscript, the way "${a[@]}" emits one
         per element, while "${!a[*]}" joins them by the first IFS character.
         The joined string path loses the per-subscript boundary, so the field
         form is produced here. The body is a leading '!' and a trailing [@] or
         [*]. */
      if (segment_text.length >= 5 && segment_text[0] == '!' &&
          segment_text[segment_text.length - 1] == ']' &&
          segment_text[segment_text.length - 3] == '[' &&
          (segment_text[segment_text.length - 2] == '@' ||
           segment_text[segment_text.length - 2] == '*') &&
          lexer::is_variable_name_start(segment_text[1]))
      {
        const StringView array_name =
            segment_text.substring_of_length(1, segment_text.length - 4);
        let const is_star = segment_text[segment_text.length - 2] == '*';
        let const subscripts = collect_array_subscripts(array_name);
        if (segment.is_in_double_quotes && is_star) {
          let const ifs = m_field_separators.view();
          let joined = String{scratch_allocator()};
          for (usize i = 0; i < subscripts.count(); i++) {
            if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
            joined.append(subscripts[i].view());
          }
          append_run(joined, false);
        } else if (segment.is_in_double_quotes) {
          for (usize i = 0; i < subscripts.count(); i++) {
            if (i > 0) flush();
            append_run(subscripts[i].view(), false);
          }
        } else {
          for (usize i = 0; i < subscripts.count(); i++) {
            if (i > 0) flush();
            append_split_run(subscripts[i].view(), true);
          }
        }
        break;
      }
      /* "${a[@]}" emits one field per array element, the way "$@" does for the
         positional parameters, while "${a[*]}" joins them by the first IFS
         character into one field. An unquoted ${a[@]} or ${a[*]} keeps each
         element its own field and splits it under IFS. The general path below
         joins to a single string, which would lose the per-element boundary, so
         the array @ and * forms are emitted here. A spec with a trailing
         modifier does not end in ']' and falls through. */
      if (segment_text.length >= 4 &&
          segment_text[segment_text.length - 1] == ']' &&
          segment_text[segment_text.length - 3] == '[' &&
          (segment_text[segment_text.length - 2] == '@' ||
           segment_text[segment_text.length - 2] == '*') &&
          lexer::is_variable_name_start(segment_text[0]))
      {
        let const array_name =
            segment_text.substring_of_length(0, segment_text.length - 3);
        let is_plain_array_name = true;
        for (usize i = 0; i < array_name.length; i++)
          if (!lexer::is_variable_name(array_name[i])) {
            is_plain_array_name = false;
            break;
          }
        if (is_plain_array_name) {
          let const is_star = segment_text[segment_text.length - 2] == '*';
          let const elements = collect_array_elements(array_name);
          if (segment.is_in_double_quotes && is_star) {
            let const ifs = m_field_separators.view();
            let joined = String{scratch_allocator()};
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
              joined.append(elements[i].view());
            }
            append_run(joined, false);
          } else if (segment.is_in_double_quotes) {
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0) flush();
              append_run(elements[i].view(), false);
            }
          } else {
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0) flush();
              append_split_run(elements[i].view(), true);
            }
          }
          break;
        }
      }
      /* "${@:off:len}" and "${*:off:len}" slice the positional parameters the
         way the array slice below slices elements, with index zero naming the
         shell itself the way bash counts $0 into the slice. The @ form keeps
         each parameter its own field, the * form joins them. */
      if ((segment_text[0] == '@' || segment_text[0] == '*') &&
          segment_text.length > 1 && segment_text[1] == ':')
      {
        let const is_star = segment_text[0] == '*';
        let const slice = segment_text.substring(2);
        let const param_count = m_positional_params.count();
        const i64 total = static_cast<i64>(param_count) + 1;
        auto positional_at = [&](i64 index) wontthrow -> StringView {
          return index == 0 ? m_shell_name.view()
                            : m_positional_params[static_cast<usize>(index - 1)]
                                  .view();
        };

        const usize sep = find_substring_length_separator(slice);
        const StringView offset_text = slice.substring_of_length(0, sep);
        const i64 offset =
            offset_text.is_empty() ? 0 : evaluate_arithmetic(offset_text);
        i64 start = offset < 0 ? total + offset : offset;
        if (start < 0) start = 0;
        if (start > total) start = total;
        i64 end = total;
        if (sep < slice.length) {
          const StringView length_text = slice.substring(sep + 1);
          const i64 length =
              length_text.is_empty() ? 0 : evaluate_arithmetic(length_text);
          if (length < 0)
            throw Error{"Unable to take the substring because the length names "
                        "a point before the offset"};
          end = start + length;
        }
        if (end > total) end = total;
        if (end < start) end = start;

        if (segment.is_in_double_quotes && is_star) {
          let const ifs = m_field_separators.view();
          let joined = String{scratch_allocator()};
          for (i64 j = start; j < end; j++) {
            if (j > start && !ifs.is_empty()) joined.push(ifs[0]);
            joined.append(positional_at(j));
          }
          append_run(joined, false);
        } else if (segment.is_in_double_quotes) {
          for (i64 j = start; j < end; j++) {
            if (j > start) flush();
            append_run(positional_at(j), false);
          }
        } else {
          for (i64 j = start; j < end; j++) {
            if (j > start) flush();
            append_split_run(positional_at(j), true);
          }
        }
        break;
      }
      /* "${a[@]:off:len}" and "${a[*]:off:len}" slice the element list, off
         naming the first element and len the count, with a negative off counted
         from the end. The @ form keeps each sliced element its own field, the *
         form joins them. */
      if (lexer::is_variable_name_start(segment_text[0])) {
        usize name_end = 1;
        while (name_end < segment_text.length &&
               lexer::is_variable_name(segment_text[name_end]))
          name_end++;
        if (name_end + 4 <= segment_text.length &&
            segment_text[name_end] == '[' &&
            (segment_text[name_end + 1] == '@' ||
             segment_text[name_end + 1] == '*') &&
            segment_text[name_end + 2] == ']' &&
            name_end + 3 < segment_text.length &&
            segment_text[name_end + 3] == ':')
        {
          let const array_name = segment_text.substring_of_length(0, name_end);
          let const is_star = segment_text[name_end + 1] == '*';
          let const slice = segment_text.substring(name_end + 4);
          let const elements = collect_array_elements(array_name);
          const i64 total = static_cast<i64>(elements.count());

          const usize sep = find_substring_length_separator(slice);
          const StringView offset_text = slice.substring_of_length(0, sep);
          const i64 offset =
              offset_text.is_empty() ? 0 : evaluate_arithmetic(offset_text);
          i64 start = offset < 0 ? total + offset : offset;
          if (start < 0) start = 0;
          if (start > total) start = total;
          i64 end = total;
          if (sep < slice.length) {
            const StringView length_text = slice.substring(sep + 1);
            const i64 length =
                length_text.is_empty() ? 0 : evaluate_arithmetic(length_text);
            /* Unlike a string substring, an array slice rejects a negative
               length the way bash does rather than counting from the end. */
            if (length < 0)
              throw Error{
                  "Unable to take the substring because the length names "
                  "a point before the offset"};
            end = start + length;
          }
          if (end > total) end = total;
          if (end < start) end = start;

          if (segment.is_in_double_quotes && is_star) {
            let const ifs = m_field_separators.view();
            let joined = String{scratch_allocator()};
            for (i64 j = start; j < end; j++) {
              if (j > start && !ifs.is_empty()) joined.push(ifs[0]);
              joined.append(elements[static_cast<usize>(j)].view());
            }
            append_run(joined, false);
          } else if (segment.is_in_double_quotes) {
            for (i64 j = start; j < end; j++) {
              if (j > start) flush();
              append_run(elements[static_cast<usize>(j)].view(), false);
            }
          } else {
            for (i64 j = start; j < end; j++) {
              if (j > start) flush();
              append_split_run(elements[static_cast<usize>(j)].view(), true);
            }
          }
          break;
        }
      }
      /* "${a[@]MOD}" maps a value-transform modifier over each element, one
         field per element the way "${a[@]}" does, while "${a[*]MOD}" joins the
         modified elements. The / replacement, the # and % trims, and the ^ and
         , case changes all map here, a different modifier falls through to the
         general scalar path. */
      if (lexer::is_variable_name_start(segment_text[0])) {
        usize name_end = 1;
        while (name_end < segment_text.length &&
               lexer::is_variable_name(segment_text[name_end]))
          name_end++;
        const char field_modifier_op = name_end + 3 < segment_text.length
                                           ? segment_text[name_end + 3]
                                           : '\0';
        if (name_end + 3 < segment_text.length &&
            segment_text[name_end] == '[' &&
            (segment_text[name_end + 1] == '@' ||
             segment_text[name_end + 1] == '*') &&
            segment_text[name_end + 2] == ']' &&
            (field_modifier_op == '/' || field_modifier_op == '#' ||
             field_modifier_op == '%' || field_modifier_op == '^' ||
             field_modifier_op == ','))
        {
          let const array_name = segment_text.substring_of_length(0, name_end);
          let const modifier = segment_text.substring(name_end + 3);
          let const is_star = segment_text[name_end + 1] == '*';
          let const elements = collect_array_elements(array_name);
          if (segment.is_in_double_quotes && is_star) {
            let const ifs = m_field_separators.view();
            let joined = String{scratch_allocator()};
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
              joined.append(
                  apply_value_modifier(elements[i].view(), modifier).view());
            }
            append_run(joined, false);
          } else {
            for (usize i = 0; i < elements.count(); i++) {
              if (i > 0) flush();
              let const modified =
                  apply_value_modifier(elements[i].view(), modifier);
              if (segment.is_in_double_quotes)
                append_run(modified.view(), false);
              else
                append_split_run(modified.view(), true);
            }
          }
          break;
        }
      }
      /* "${a[@]+word}" and "${a[@]-word}" pick between the word and the
         elements with field fidelity, the nounset-safe array expansion idiom
         bash-completion writes as ${a[@]+"${a[@]}"}. The dominant word shape,
         one quoted or bare array expansion, emits one field per element the
         way the plain "${a[@]}" does, and any other word shape falls through
         to the general scalar path below. */
      if (lexer::is_variable_name_start(segment_text[0])) {
        usize name_end = 1;
        while (name_end < segment_text.length &&
               lexer::is_variable_name(segment_text[name_end]))
          name_end++;
        if (name_end + 3 < segment_text.length &&
            segment_text[name_end] == '[' &&
            (segment_text[name_end + 1] == '@' ||
             segment_text[name_end + 1] == '*') &&
            segment_text[name_end + 2] == ']')
        {
          let const rest = segment_text.substring(name_end + 3);
          let const is_colon_form = !rest.is_empty() && rest[0] == ':';
          let const op_index = is_colon_form ? usize{1} : usize{0};
          if (op_index < rest.length &&
              (rest[op_index] == '+' || rest[op_index] == '-'))
          {
            let const array_name =
                segment_text.substring_of_length(0, name_end);
            let const modifier_op = rest[op_index];
            let const modifier_word = rest.substring(op_index + 1);
            let const is_star = segment_text[name_end + 1] == '*';
            let const elements = collect_array_elements(array_name);
            let is_every_element_empty = true;
            for (const String &element : elements)
              if (!element.is_empty()) {
                is_every_element_empty = false;
                break;
              }
            let const treat_as_unset =
                is_colon_form ? is_every_element_empty : elements.is_empty();
            let const should_expand_word =
                modifier_op == '+' ? !treat_as_unset : treat_as_unset;

            /* The per-element emitter the plain "${a[@]}" cases use, one
               field per element when quoted with @, an IFS join with *. The
               quoting follows the expanded text, so "${arr[@]}" stays one
               field per element even when the outer modifier is unquoted, the
               way bash keeps the inner quotes. */
            auto emit_elements = [&](const ArrayList<String> &values,
                                     bool quoted) throws {
              if (quoted && is_star) {
                let const ifs = m_field_separators.view();
                let joined = String{scratch_allocator()};
                for (usize i = 0; i < values.count(); i++) {
                  if (i > 0 && !ifs.is_empty()) joined.push(ifs[0]);
                  joined.append(values[i].view());
                }
                append_run(joined, false);
                return;
              }
              for (usize i = 0; i < values.count(); i++) {
                if (i > 0) flush();
                if (quoted)
                  append_run(values[i].view(), false);
                else
                  append_split_run(values[i].view(), true);
              }
            };

            if (!should_expand_word) {
              /* + with an unset array contributes no field at all, and - with
                 a set array reads the elements themselves under the outer
                 quoting. */
              if (modifier_op == '-')
                emit_elements(elements, segment.is_in_double_quotes);
              break;
            }

            /* The word shape "${name[@]}" or its bare or starred forms, the
               only shapes the idiom uses, expands to that array's elements. */
            let word = modifier_word;
            let const is_word_quoted = word.length >= 2 && word[0] == '"' &&
                                       word[word.length - 1] == '"';
            if (is_word_quoted)
              word = word.substring_of_length(1, word.length - 2);
            if (word.length >= 6 && word[0] == '$' && word[1] == '{' &&
                word[word.length - 1] == '}')
            {
              let const inner = word.substring_of_length(2, word.length - 3);
              usize inner_name_end = 0;
              while (inner_name_end < inner.length &&
                     lexer::is_variable_name(inner[inner_name_end]))
                inner_name_end++;
              if (inner_name_end > 0 && inner_name_end + 3 == inner.length &&
                  inner[inner_name_end] == '[' &&
                  (inner[inner_name_end + 1] == '@' ||
                   inner[inner_name_end + 1] == '*') &&
                  inner[inner_name_end + 2] == ']')
              {
                /* The inner word's own quoting governs the split, so a quoted
                   "${arr[@]}" keeps each element whole even though the outer
                   modifier here is unquoted. */
                emit_elements(collect_array_elements(
                                  inner.substring_of_length(0, inner_name_end)),
                              is_word_quoted || segment.is_in_double_quotes);
                break;
              }
            }
            /* Any other word shape keeps the scalar expansion. */
            let const value = apply_parameter_expansion(segment.text.view());
            if (segment.is_in_double_quotes)
              append_run(value, false);
            else
              append_split_run(value, true);
            break;
          }
        }
      }
      /* A plain $name that names a set scalar appends the stored value by view,
         with no copy, since the full parameter expansion would read the same
         string. A spec with a modifier, an unset name, or a synthesized name is
         not found in the store and falls through to the general path. */
      if (!segment_text.is_empty() &&
          lexer::is_variable_name_start(segment_text[0]))
      {
        let is_plain_name = true;
        for (usize i = 1; i < segment_text.length; i++)
          if (!lexer::is_variable_name(segment_text[i])) {
            is_plain_name = false;
            break;
          }
        if (is_plain_name)
          if (let const *stored = lookup_shell_variable(segment_text)) {
            if (segment.is_in_double_quotes)
              append_run(stored->view(), false);
            else
              append_split_run(stored->view(), true);
            break;
          }
      }
      /* apply_parameter_expansion already returns an owned String, so it is
         bound directly rather than copied into a second allocation. */
      let const value = apply_parameter_expansion(segment.text.view());
      if (segment.is_in_double_quotes)
        append_run(value, false);
      else
        /* An unquoted expansion undergoes field splitting and then pathname
           expansion, so a * or ? from the value is an active glob. */
        append_split_run(value, true);
    } break;

    case WordSegment::Kind::CommandSubstitution: {
      let const output = capture_command_substitution(segment);
      if (segment.is_in_double_quotes)
        append_run(output, false);
      else
        append_split_run(output, true);
    } break;

    case WordSegment::Kind::ProcessSubstitution: {
      /* The /dev/fd path is a single literal field, so it neither splits on IFS
         nor globs, the way bash substitutes the process substitution. */
      let const path = setup_process_substitution(segment.text.view());
      append_run(path, false);
    } break;

    case WordSegment::Kind::ArithmeticExpansion: {
      /* A constant arithmetic segment was folded at analyze time, so the result
         is read straight from the cache rather than re-parsed here. */
      let const result = segment.folded_arithmetic_result.has_value()
                             ? *segment.folded_arithmetic_result
                             : evaluate_arithmetic(segment.text.view());
      /* The field copies the digits in, so the conversion writes into a stack
         buffer and appends a view, with no heap allocation. */
      char buffer[24];
      let const value = utils::int_to_text_into(result, buffer, sizeof(buffer));
      if (segment.is_in_double_quotes)
        append_run(value, false);
      else
        append_split_run(value, false);
    } break;
    }
  }

  flush();

  return fields;
}

hot fn EvalContext::expand_word_for_assignment(const Word &word) throws
    -> String
{
  LOG(verbosity::All, "expanding an assignment word of %zu segments",
      word.segments.count());
  /* Only copy the segments when a tilde must be rewritten, the leading one or
     one after an unquoted colon, the assignment-only rule bash applies to
     PATH=~/bin:~/tmp. The common assignment reads its segments in place with
     no per-command copy. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{scratch_allocator()};
  let const has_leading_tilde =
      !word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~';
  let has_colon_tilde = false;
  for (const WordSegment &segment : word.segments) {
    if (!segment.is_tilde_candidate()) continue;
    let const view = segment.text.view();
    for (usize i = 0; i + 1 < view.length; i++)
      if (view[i] == ':' && view[i + 1] == '~') {
        has_colon_tilde = true;
        break;
      }
    if (has_colon_tilde) break;
  }
  if (has_leading_tilde || has_colon_tilde) {
    tilde_expanded_segments = word.segments;
    if (has_leading_tilde)
      expand_tilde(tilde_expanded_segments.front(),
                   tilde_expanded_segments.count() > 1);
    if (has_colon_tilde)
      for (usize i = 0; i < tilde_expanded_segments.count(); i++)
        expand_colon_tildes(tilde_expanded_segments[i],
                            i + 1 < tilde_expanded_segments.count());
    segments = &tilde_expanded_segments;
  }

  let result = String{heap_allocator()};
  for (const WordSegment &segment : *segments) {
    let const segment_text = segment.text.view();
    switch (segment.kind) {
    case WordSegment::Kind::VariableReference:
      result += apply_parameter_expansion(segment_text);
      break;
    case WordSegment::Kind::CommandSubstitution:
      result += capture_command_substitution(segment);
      break;
    case WordSegment::Kind::ArithmeticExpansion:
      result += utils::int_to_text(segment.folded_arithmetic_result.has_value()
                                       ? *segment.folded_arithmetic_result
                                       : evaluate_arithmetic(segment_text));
      break;
    default: result += segment_text; break;
    }
  }
  return result;
}

fn EvalContext::expand_case_pattern_masked(const Word &word,
                                           ArrayList<bool> &active_out) throws
    -> String
{
  /* Only copy the segments when a leading tilde must be rewritten, mirroring
     the assignment expansion the case word otherwise shares. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{scratch_allocator()};
  if (!word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front(),
                 tilde_expanded_segments.count() > 1);
    segments = &tilde_expanded_segments;
  }

  let result = String{heap_allocator()};

  /* Append a run of bytes that share one glob-active state, so the mask stays
     parallel to the result the way expand_word builds it. */
  auto emit_run = [&](StringView bytes, bool is_active) {
    result.append(bytes);
    for (usize k = 0; k < bytes.length; k++)
      active_out.push(is_active);
  };

  for (const WordSegment &segment : *segments) {
    let const segment_text = segment.text.view();
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      /* A quoted or double-quoted region is a literal member, so its
         metacharacters never act as wildcards. */
      emit_run(segment_text, false);
      break;
    case WordSegment::Kind::UnquotedText: emit_run(segment_text, true); break;
    case WordSegment::Kind::VariableReference: {
      let const value = apply_parameter_expansion(segment_text);
      emit_run(value.view(), !segment.is_in_double_quotes);
    } break;
    case WordSegment::Kind::CommandSubstitution: {
      let const output = capture_command_substitution(segment);
      emit_run(output.view(), !segment.is_in_double_quotes);
    } break;
    case WordSegment::Kind::ProcessSubstitution: {
      /* The /dev/fd path is a literal that does not glob. */
      let const path = setup_process_substitution(segment.text.view());
      emit_run(path.view(), false);
    } break;
    case WordSegment::Kind::ArithmeticExpansion: {
      /* An arithmetic result is decimal digits and a sign, so it carries no
         glob metacharacter and stays inactive. */
      let const number = segment.folded_arithmetic_result.has_value()
                             ? *segment.folded_arithmetic_result
                             : evaluate_arithmetic(segment_text);
      emit_run(utils::int_to_text(number).view(), false);
    } break;
    }
  }
  return result;
}

/* The drain thread reads the pipe into captured while the inner command writes
   the other end, so output larger than the pipe buffer cannot deadlock. */
struct command_substitution_drain_context
{
  String *captured;
  os::descriptor read_fd;
};

fn drain_command_substitution_pipe(void *raw_context) wontthrow -> void
{
  let const drain =
      static_cast<command_substitution_drain_context *>(raw_context);
  /* A failed allocation here must not escape the thread and call terminate. */
  try {
    char buffer[4096];
    for (;;) {
      let const n = os::read_fd(drain->read_fd, buffer, sizeof(buffer));
      if (!n.has_value() || *n == 0) break;
      drain->captured->append(StringView{buffer, static_cast<usize>(*n)});
    }
  } catch (...) {
    LOG(verbosity::Debug,
        "the command substitution drain thread swallowed a failure while "
        "capturing");
  }
}

fn EvalContext::read_redirect_substitution(StringView source) throws
    -> Maybe<String>
{
  usize i = 0;
  while (i < source.length &&
         (source[i] == ' ' || source[i] == '\t' || source[i] == '\n'))
    i++;
  if (i >= source.length || source[i] != '<') return None;
  i++;

  if (AST_ARENA == nullptr) return None;
  let lexer = Lexer{String{source.substring_of_length(i, source.length - i)},
                    *AST_ARENA, false, None, mood()};
  Token *name = lexer.next_shell_token();
  if (name == nullptr || name->kind() != Token::Kind::Word) return None;
  /* Anything after the single filename means this is not the bare read form, so
     the normal parse-and-run path handles it. */
  Token *after = lexer.next_shell_token();
  if (after != nullptr && after->kind() != Token::Kind::EndOfFile &&
      after->kind() != Token::Kind::Newline)
    return None;

  let const filename = expand_word_for_assignment(
      static_cast<const tokens::WordToken *>(name)->word());
  LOG(verbosity::Debug, "the substitution is a bare file read of '%s'",
      filename.c_str());
  let content = utils::read_entire_file(filename.view());
  /* An unreadable file yields an empty substitution, the way bash leaves
     COMPREPLY-style reads empty rather than aborting. */
  if (!content.has_value()) {
    LOG(verbosity::Debug,
        "the file read substitution of '%s' failed, expanding to empty",
        filename.c_str());
    return String{};
  }
  let result = steal(*content);
  while (!result.is_empty() && result.back() == '\n')
    result.pop_back();
  return result;
}

fn EvalContext::capture_command_substitution(const String &source) throws
    -> String
{
  LOG(verbosity::Debug, "capturing a command substitution of %zu bytes",
      source.count());
  if (Maybe<String> file = read_redirect_substitution(source.view());
      file.has_value())
    return steal(*file);

  /* Parse the inner command into the active parse arena. It coexists with the
     outer tree and is reclaimed when the arena resets. */
  if (AST_ARENA == nullptr)
    throw Error{"Command substitution outside of a parse"};

  let parser = Parser{
      Lexer{String{source.view()}, *AST_ARENA, false, None, mood()}
  };
  let const ast = parser.construct_ast();
  ASSERT(ast != nullptr);

  return run_captured_substitution(ast, source);
}

fn EvalContext::setup_process_substitution(StringView text) throws -> String
{
  if (AST_ARENA == nullptr)
    throw Error{"Process substitution outside of a parse"};
  ASSERT(!text.is_empty());

  /* The first byte is the direction marker the lexer wrote, the rest is the
     inner command source the child runs. */
  const char direction = text[0];
  const bool command_writes_the_pipe = direction == '<';
  LOG(verbosity::Debug,
      "setting up a process substitution where the command %s the pipe",
      command_writes_the_pipe ? "writes" : "reads");

#if SHIT_PLATFORM_IS WIN32
  /* Windows has no fork, so the substitution runs in a fresh shell that writes
     its output to a temp file the consuming command reads by path. The <(cmd)
     form is supported. The >(cmd) form would need the inner shell to run after
     the outer command writes the file, an ordering the synchronous spawn here
     cannot provide. */
  if (!command_writes_the_pipe)
    throw Error{"Unable to run a >(cmd) process substitution because it is not "
                "supported on this platform"};
  if (Maybe<String> substitution_path =
          os::run_substitution_to_temp(text.substring(1), is_bash_compatible());
      substitution_path.has_value())
  {
    /* The temp file is read by the consuming command after this returns, so it
       is tracked for deletion once that command finishes rather than removed
       now. */
    m_substitution_temp_files.track(Path{substitution_path->view()});
    return steal(*substitution_path);
  }
  throw Error{"Unable to run the process substitution because the inner shell "
              "could not be spawned: " +
              os::last_system_error_message()};
#else
  let parser = Parser{
      Lexer{String{text.substring(1)}, *AST_ARENA, false, None, mood()}
  };
  let const ast = parser.construct_ast();
  ASSERT(ast != nullptr);

  let const pipe = os::make_pipe();
  if (!pipe.has_value())
    throw Error{"Could not open a pipe for the process substitution: " +
                os::last_system_error_message()};

  /* For <(cmd) the command writes its standard output into the pipe and the
     shell reads the other end. For >(cmd) the command reads its standard input
     from the pipe and the shell writes the other end. */
  const os::process child = command_writes_the_pipe
                                ? os::fork_compound_stage(None, pipe->out, None)
                                : os::fork_compound_stage(pipe->in, None, None);

  if (child == 0) {
    /* The child does not need the shell's end, so it closes it before running
       the inner command and exits without returning into the parent evaluator
       inside the duplicated process. */
    os::close_fd(command_writes_the_pipe ? pipe->in : pipe->out);
    i32 status = 0;
    try {
      ast->evaluate(*this);
      status = last_exit_status();
    } catch (...) {
      LOG(verbosity::Debug,
          "the process substitution child swallowed an error, exiting with "
          "status 1");
      status = 1;
    }
    os::exit_process_immediately(status);
  }

  /* The shell keeps the end it reads or writes and closes the child's end. The
     kept end must survive an exec so the consuming command inherits it and a
     read of /dev/fd/N reaches this pipe. */
  const os::descriptor shell_fd =
      command_writes_the_pipe ? pipe->in : pipe->out;
  os::close_fd(command_writes_the_pipe ? pipe->out : pipe->in);
  os::make_fd_inheritable(shell_fd);
  /* The command currently evaluating is where this substitution was written, so
     its location points a later reap warning at the right word. */
  let const location = SourceLocation{m_current_location_position, 1};
  const StringView source =
      m_current_source != nullptr ? m_current_source->view() : StringView{};
  m_pending_process_substitutions.push(
      process_substitution{shell_fd, child, location, source});

  let path = String{"/dev/fd/"};
  path += utils::int_to_text(static_cast<i64>(shell_fd));
  LOG(verbosity::Debug, "the process substitution is reachable at '%s'",
      path.c_str());
  return path;
#endif
}

fn EvalContext::mark_process_substitutions() const wontthrow
    -> process_substitution_mark
{
  return {m_pending_process_substitutions.count(),
          m_substitution_temp_files.count()};
}

fn EvalContext::cleanup_process_substitutions(
    process_substitution_mark mark) wontthrow -> void
{
  LOG(verbosity::Debug, "cleaning up %zu pending process substitutions",
      m_pending_process_substitutions.count() - mark.pending);
  for (usize i = mark.pending; i < m_pending_process_substitutions.count(); i++)
  {
    process_substitution &sub = m_pending_process_substitutions[i];
    /* Closing the shell end first sends SIGPIPE to a producer that still has
       output queued, so it ends rather than blocking the wait below. */
    os::close_fd(sub.shell_fd);
    try {
      os::reap_process_quietly(sub.child);
    } catch (const Error &e) {
      LOG(verbosity::Debug,
          "a process substitution reap failed and was swallowed: %s",
          e.message().c_str());
      /* The child is reaped on a best-effort basis, so a wait failure is shown
         as a warning and swallowed rather than propagated out of this no-throw
         cleanup. bash stays silent here, so the warning is suppressed in bash
         mode, and the show is guarded so a failure to print cannot escape. The
         warning points a caret at the command when its source is known. */
      if (!is_bash_compatible()) {
        try {
          const String text =
              "A process substitution child could not be reaped. " +
              e.message();
          show_message(sub.source.is_empty()
                           ? Warning{text}.to_string()
                           : WarningWithLocation{sub.location, text}.to_string(
                                 sub.source));
        } catch (...) {
          LOG(verbosity::Debug,
              "showing the reap warning failed, the error is swallowed");
        }
      }
    } catch (...) {
      LOG(verbosity::Debug,
          "a process substitution reap failed with an unknown error, "
          "swallowed");
      if (!is_bash_compatible()) {
        try {
          const StringView text =
              "A process substitution child could not be reaped.";
          show_message(sub.source.is_empty()
                           ? Warning{text}.to_string()
                           : WarningWithLocation{sub.location, text}.to_string(
                                 sub.source));
        } catch (...) {
          LOG(verbosity::Debug,
              "showing the fallback reap warning failed, the error is "
              "swallowed");
        }
      }
    }
  }
  while (m_pending_process_substitutions.count() > mark.pending)
    m_pending_process_substitutions.remove(
        m_pending_process_substitutions.count() - 1);
  m_substitution_temp_files.cleanup_from(mark.temp);
}

fn EvalContext::capture_command_substitution(const WordSegment &segment) throws
    -> String
{
  if (Maybe<String> file = read_redirect_substitution(segment.text.view());
      file.has_value())
    return steal(*file);

  if (AST_ARENA == nullptr)
    throw Error{"Command substitution outside of a parse"};

  /* The segment text and its escape state never change between iterations, so
     the inner command is lexed and parsed once and the tree is reused while the
     arena that holds it is unreset. A cached tree from an earlier generation
     points into reclaimed storage, so it is reparsed. */
  const usize generation = AST_ARENA->reset_generation();
  if (segment.cached_substitution_ast == nullptr ||
      segment.cached_substitution_generation != generation)
  {
    LOG(verbosity::Debug,
        "command substitution ast cache miss for generation %zu, reparsing",
        generation);
    let parser = Parser{
        Lexer{String{segment.text.view()}, *AST_ARENA, false, None, mood()}
    };
    segment.cached_substitution_ast = parser.construct_ast();
    segment.cached_substitution_generation = generation;
  }
  ASSERT(segment.cached_substitution_ast != nullptr);

  return run_captured_substitution(segment.cached_substitution_ast,
                                   segment.text);
}

fn EvalContext::run_captured_substitution(const Expression *ast,
                                          const String &source) throws -> String
{
  ASSERT(ast != nullptr);
  LOG(verbosity::Debug, "running a captured substitution body of %zu bytes",
      source.count());

  /* A cd or an assignment inside the substitution must not leak. */
  let snapshot = snapshot_state();

  /* The inner evaluation's transient scratch is reclaimed at the substitution
     boundary, so a $(...) inside a loop does not grow the arena across
     iterations. The captured output is heap and escapes, and restore_state
     reverts every inner side effect, so nothing the release frees is still
     read. */
  let const substitution_mark = m_scratch_arena.mark();
  defer { m_scratch_arena.release(substitution_mark); };

  /* The substitution body is its own source, so a located error inside it
     carries an offset into that text. The current source is pointed at it for
     the run, so an error rendered inline, such as a command not found, marks
     the right byte, and the error caught below is formatted against it too. */
  let const previous_source = m_current_source;
  let const previous_origin = m_current_origin;
  let const previous_location_position = m_current_location_position;
  set_current_source(&source, String{"command substitution"});
  defer
  {
    set_current_source(previous_source, previous_origin);
    m_current_location_position = previous_location_position;
  };

  let const pipe = os::make_pipe();
  if (!pipe) throw Error{"Could not open a pipe for command substitution"};

  /* Drain the read end on a thread so output larger than the pipe buffer cannot
     deadlock the commands writing into it. */
  let captured = String{heap_allocator()};
  let drain_context = command_substitution_drain_context{&captured, pipe->in};
  let const reader =
      os::start_thread(drain_command_substitution_pipe, &drain_context);
  if (!reader) {
    os::close_fd(pipe->in);
    os::close_fd(pipe->out);
    throw Error{"Could not start a thread for command substitution"};
  }

  shit::flush();
  let const saved = os::redirect_stdout(pipe->out);

  /* The inner commands write to the pipe, not the terminal, so suppress the
     interactive title updates while the substitution runs. */
  let const was_interactive = m_shell_is_interactive;
  m_shell_is_interactive = false;

  /* Run the inner command, then always tear down, even on an error. A break,
     continue, return, or exit inside a substitution acts only within it and
     must not escape into the enclosing loop, function, or shell. */
  enter_subshell();
  /* The inherited EXIT action belongs to the parent and does not fire at the
     substitution's end. An EXIT action the inner code sets survives and fires
     below, its output captured into the substitution like any other. */
  clear_inherited_exit_trap();
  std::exception_ptr error;
  try {
    ast->evaluate(*this);
  } catch (...) {
    error = std::current_exception();
  }
  /* A break, continue, return, or exit inside the substitution acts only within
     it, so consume any pending jump here. An exit supplies the status. */
  if (has_pending_control_flow()) {
    if (pending_control_flow().kind == control_flow::Kind::Exit)
      set_last_exit_status(static_cast<i32>(pending_control_flow().value));
    clear_control_flow();
  }
  /* The substitution ends here, so its own EXIT action runs while stdout still
     points at the pipe, so its output joins the captured value. A throw from
     the action is kept and rethrown after teardown, like a throw from the body.
   */
  if (!error) {
    try {
      run_subshell_exit_trap();
    } catch (...) {
      error = std::current_exception();
    }
  }
  leave_subshell();

  m_shell_is_interactive = was_interactive;

  shit::flush();
  os::restore_stdout(saved);
  os::close_fd(pipe->out);
  os::join_thread(*reader);
  os::close_fd(pipe->in);
  restore_state(steal(snapshot));

  if (error) {
    /* A throw inside the substitution exits its subshell, the way bash contains
       a fatal expansion error such as ${x?} to the command substitution rather
       than aborting the parent. The error is rendered here against the
       substitution source so its caret marks the right byte, with the source
       backtrace under it, then the parent continues with the partial output and
       a failing status. */
    LOG(verbosity::Debug,
        "the command substitution failed, containing the error with status 1");
    try {
      std::rethrow_exception(error);
    } catch (const ErrorWithLocationAndDetails &e) {
      show_message(e.to_string(source.view()));
      show_message(e.details_to_string(source.view()));
      print_source_backtrace();
    } catch (const ErrorWithLocation &e) {
      show_message(e.to_string(source.view()));
      print_source_backtrace();
    } catch (const Error &e) {
      show_message(e.to_string());
      print_source_backtrace();
    }
    set_last_exit_status(1);
  }

  while (!captured.is_empty() && captured.back() == '\n')
    captured.pop_back();
  return captured;
}

fn EvalContext::run_mimicked_script(ExecContext &ec, mimic_mood mode,
                                    bool isolated) throws -> i32
{
  if (m_mimicry_depth >= MAX_MIMICRY_DEPTH)
    throw Error{"Unable to mimic '" + ec.program() +
                "' because the script nesting is too deep"};
  if (AST_ARENA == nullptr)
    throw Error{"Unable to mimic '" + ec.program() + "' outside of a parse"};

  let contents = utils::read_entire_file(ec.program_path().text());
  if (!contents.has_value())
    throw Error{"Unable to mimic '" + ec.program() +
                "' because the script could not be read"};

  /* A NUL byte in the leading bytes marks a binary file rather than a text
     script, so it is reported the way bash does for an unrunnable binary, with
     status 126, instead of being parsed as shell source and spewing garbage
     commands. bash samples only the head, so a script carrying a NUL on a later
     line still runs, and a binary's header NUL sits well inside this window. */
  const usize binary_scan_limit = 128;
  let const head = contents->view();
  let const scan_length =
      head.length < binary_scan_limit ? head.length : binary_scan_limit;
  if (head.substring_of_length(0, scan_length).find_character('\0').has_value())
  {
    LOG(verbosity::Debug,
        "a NUL byte in the leading bytes marks '%s' as a binary file",
        ec.program().c_str());
    shit::print_error("shit: " + ec.program_path().text() +
                      ": cannot execute binary file\n");
    return 126;
  }

  /* The mimic mode decides the lexing and the evaluation, so it is set before
     the parse. The parent's mode is put back when the run is isolated, while
     the terminal run leaves it since the shell exits next. */
  let const previous_mood = m_mood;
  m_mood = mode;
  LOG(verbosity::Debug, "mimicking the script '%s'%s", ec.program().c_str(),
      isolated ? " in an isolated subshell" : "");
  /* A mimicked script is a script-file run, so its FUNCNAME bottoms out at
     "main" the way the direct file invocation marks it. */
  let const previous_script_run = m_is_script_run;
  m_is_script_run = true;

  /* The strict interactive defaults shit turns on at its own prompt, nounset
     and pipefail and failglob, do not belong to a real bash or sh running a
     file, so a mimicked script clears them for its run and the isolated case
     puts them back. This is why a mimicked declare -A array literal does not
     abort on the unmatched [k]=v glob, and an unset parameter expands empty
     rather than tripping nounset, the way the named shell runs the script. */
  let const previous_error_unset = error_unset();
  let const previous_pipefail = pipefail();
  let const previous_failglob = failglob();
  set_error_unset(false);
  set_pipefail(false);
  set_failglob(false);
  LOG(verbosity::Debug,
      "cleared the interactive strict options for the mimicked run");

  let parser = Parser{
      Lexer{String{contents->view()}, *AST_ARENA, false, None, mood()}
  };
  const Expression *ast = parser.construct_ast();
  ASSERT(ast != nullptr);

  /* The script reads $0 as its path and $1 upward as the rest of the command.
   */
  let previous_shell_name = String{m_shell_name};
  let params = ArrayList<String>{};
  for (usize i = 1; i < ec.args().count(); i++)
    params.push_managed(ec.args()[i].view());

  /* The script body is its own source, so an error inside it renders against
     this text. */
  let const previous_source = m_current_source;
  let const previous_origin = m_current_origin;
  let const previous_location_position = m_current_location_position;

  /* The redirections the spawn would have applied are applied to the standard
     descriptors for the in-process run, then put back. A file redirect to
     stdout or stderr is already staged on the real shell fd by the simple
     command, so only the descriptors carried on the context are applied here.
   */
  let saved_fds = ArrayList<os::saved_descriptor>{};
  if (ec.in_fd.has_value())
    saved_fds.push(os::save_and_replace_descriptor(0, *ec.in_fd));
  if (ec.out_fd.has_value())
    saved_fds.push(os::save_and_replace_descriptor(1, *ec.out_fd));
  if (ec.err_fd.has_value())
    saved_fds.push(os::save_and_replace_descriptor(2, *ec.err_fd));
  let const restore_fds = [&]() {
    for (usize i = saved_fds.count(); i > 0; i--)
      os::restore_descriptor(saved_fds[i - 1]);
  };
  /* The descriptors carried on the context were dup'd onto the standard fds
     above, so the originals are closed when this run ends. Nothing else owns
     them, since this path replaces the fork-and-exec that would otherwise have
     closed them, and close_fds resets each Maybe so a later close is a no-op.
   */
  defer { ec.close_fds(); };

  let const render_error = [&](std::exception_ptr error) {
    try {
      std::rethrow_exception(error);
    } catch (const ErrorWithLocation &e) {
      show_message(e.to_string(contents->view()));
      print_source_backtrace();
    } catch (const Error &e) {
      show_message(e.to_string());
      print_source_backtrace();
    }
  };

  m_shell_name = String{heap_allocator(), ec.program().view()};
  set_current_source(&*contents, String{ec.program().view()});
  m_current_location_position = 0;
  m_mimicry_depth++;

  /* The terminal command the shell exits with needs no isolation, so the script
     runs against the current state with no snapshot and the shell exits with
     its status, the way exec'ing the shell would. A return, break, or exit
     inside it propagates the way it would from a real script. */
  /* A mimicked bash advertises BASH_VERSION the way the bash invocation does,
     so a script that detects bash through it takes its bash path. The set lands
     after the isolated snapshot so the restore drops it. */
  if (!isolated) {
    set_positional_params(steal(params));
    seed_shell_identity_variables(mode == mimic_mood::Bash);
    std::exception_ptr error;
    try {
      ast->evaluate(*this);
    } catch (...) {
      error = std::current_exception();
    }
    m_mimicry_depth--;
    restore_fds();
    if (error) {
      render_error(error);
      return 1;
    }
    return last_exit_status();
  }

  /* The isolated run snapshots the mutable state and runs in a subshell, so the
     script's cd, exports, functions, and exit do not leak to the parent. */
  let snapshot = snapshot_state();
  set_positional_params(steal(params));
  seed_shell_identity_variables(mode == mimic_mood::Bash);
  enter_subshell();
  clear_inherited_exit_trap();
  std::exception_ptr error;
  try {
    ast->evaluate(*this);
  } catch (...) {
    error = std::current_exception();
  }
  if (has_pending_control_flow()) {
    if (pending_control_flow().kind == control_flow::Kind::Exit)
      set_last_exit_status(static_cast<i32>(pending_control_flow().value));
    clear_control_flow();
  }
  if (!error) {
    try {
      run_subshell_exit_trap();
    } catch (...) {
      error = std::current_exception();
    }
  }
  leave_subshell();
  m_mimicry_depth--;
  restore_fds();

  let const status = last_exit_status();
  restore_state(steal(snapshot));
  set_current_source(previous_source, previous_origin);
  m_current_location_position = previous_location_position;
  m_mood = previous_mood;
  m_is_script_run = previous_script_run;
  set_error_unset(previous_error_unset);
  set_pipefail(previous_pipefail);
  set_failglob(previous_failglob);
  m_shell_name = steal(previous_shell_name);

  if (error) {
    render_error(error);
    return 1;
  }
  return status;
}

pure fn EvalContext::shopt_default_is_on(StringView name) wontthrow -> bool
{
  /* The shopt names bash ships enabled. globstar stays off the way bash
     ships it, and the glob engine reads its live value. */
  static const StringView DEFAULT_ON_SHOPT_NAMES[] = {
      "progcomp",     "promptvars",        "sourcepath",
      "extquote",     "complete_fullquote", "hostcomplete",
      "cmdhist",      "checkwinsize",       "force_fignore",
      "globasciiranges", "expand_aliases",  "interactive_comments",
  };
  for (const StringView default_name : DEFAULT_ON_SHOPT_NAMES)
    if (name == default_name) return true;
  return false;
}

fn EvalContext::expand_wordlist_to_fields(StringView wordlist,
                                          bool allow_expansion) throws
    -> ArrayList<String>
{
  auto split_plain = [&]() throws -> ArrayList<String> {
    let words = ArrayList<String>{};
    usize start = 0;
    for (usize i = 0; i <= wordlist.length; i++) {
      const char c = i < wordlist.length ? wordlist[i] : ' ';
      if (c == ' ' || c == '\t' || c == '\n') {
        if (i > start)
          words.push(String{wordlist.substring_of_length(start, i - start)});
        start = i + 1;
      }
    }
    return words;
  };

  /* The ghost path never parses, so it skips even the metacharacter scan. */
  if (!allow_expansion) return split_plain();

  /* A literal list, no expansion or quoting byte anywhere, splits with no
     parse at all, the common -W shape. */
  let needs_expansion = false;
  for (usize i = 0; i < wordlist.length && !needs_expansion; i++) {
    const char c = wordlist[i];
    needs_expansion = c == '$' || c == '`' || c == '"' || c == '\'' ||
                      c == '\\' || c == '~' || c == '{';
  }
  if (!needs_expansion) return split_plain();

  /* The list expands by wrapping it in an array literal, so a structural byte
     that would close the literal early and run the rest as a command, a
     top-level ')' or ';' or '|' or '&' or '(' or a comment '#', is a break-out
     a malicious or careless -W list could carry. Such a list degrades to the
     plain split rather than executing the tail. The scan tracks quotes and the
     $(...) and ${...} and backtick nesting so a paren inside an expansion is
     not mistaken for a top-level one. */
  auto is_array_literal_safe = [&]() wontthrow -> bool {
    char quote = 0;
    usize paren_depth = 0;
    usize brace_depth = 0;
    bool in_backtick = false;
    bool at_word_start = true;
    for (usize i = 0; i < wordlist.length; i++) {
      const char c = wordlist[i];
      if (quote != 0) {
        if (c == quote) quote = 0;
        at_word_start = false;
        continue;
      }
      if (c == '\\') {
        i++;
        at_word_start = false;
        continue;
      }
      if (c == '\'' || c == '"') {
        quote = c;
      } else if (c == '`') {
        in_backtick = !in_backtick;
      } else if (c == '$' && i + 1 < wordlist.length && wordlist[i + 1] == '(') {
        paren_depth++;
        i++;
      } else if (c == '$' && i + 1 < wordlist.length && wordlist[i + 1] == '{') {
        brace_depth++;
        i++;
      } else if (c == ')' && paren_depth > 0) {
        paren_depth--;
      } else if (c == '}' && brace_depth > 0) {
        brace_depth--;
      } else if (!in_backtick && paren_depth == 0 && brace_depth == 0) {
        if (c == ')' || c == '(' || c == ';' || c == '|' || c == '&' ||
            c == '<' || c == '>' || c == '\n')
          return false;
        if (c == '#' && at_word_start) return false;
      }
      at_word_start = c == ' ' || c == '\t';
    }
    return quote == 0 && !in_backtick && paren_depth == 0 && brace_depth == 0;
  };
  if (!is_array_literal_safe()) {
    LOG(verbosity::Debug, "-W list is not array-literal safe, splitting plain");
    return split_plain();
  }

  /* The list expands as an array literal in the current context, so a word
     such as "${options[@]}" reaches the caller's array. The defer drops the
     temp name on the success and the failure path alike. */
  defer
  {
    m_indexed_arrays.erase("t__wordlist_fields");
    force_unset_shell_variable("t__wordlist_fields");
  };
  let fields = ArrayList<String>{};
  try {
    let expansion_source = String{"t__wordlist_fields=("};
    expansion_source.append(wordlist);
    expansion_source.push(')');
    run_source(expansion_source.view(), "a -W word list", false);
    if (const ArrayList<String> *expanded =
            lookup_indexed_array("t__wordlist_fields");
        expanded != nullptr)
    {
      fields.reserve(expanded->count());
      for (const String &word : *expanded)
        fields.push_managed(word.view());
    }
  } catch (const ErrorBase &error) {
    LOG(verbosity::Debug, "-W expansion failed, splitting plain: %s",
        error.message().c_str());
    return split_plain();
  }
  return fields;
}

fn EvalContext::register_completion_spec(StringView command,
                                         completion_spec spec) throws -> void
{
  LOG(verbosity::Debug,
      "registering a completion spec for '%.*s' with function '%s' and %zu "
      "word-list bytes",
      static_cast<int>(command.length), command.data,
      spec.function_name.c_str(), spec.word_list.length());
  m_completion_specs.set(command, steal(spec));
}

fn EvalContext::register_default_completion_spec(completion_spec spec) throws
    -> void
{
  LOG(verbosity::Debug, "registering the default completion spec");
  m_default_completion_spec = steal(spec);
}

pure fn EvalContext::default_completion_spec() const wontthrow
    -> const completion_spec *
{
  return m_default_completion_spec.has_value() ? &*m_default_completion_spec
                                               : nullptr;
}

pure fn EvalContext::lookup_completion_spec(StringView command) const wontthrow
    -> const completion_spec *
{
  return m_completion_specs.find(command);
}

fn EvalContext::run_completion_function(StringView function_name,
                                        const ArrayList<String> &words,
                                        usize cword, StringView line,
                                        usize point,
                                        i32 *out_exit_status) throws
    -> ArrayList<String>
{
  const Expression *body =
      has_functions() ? find_function(function_name) : nullptr;
  if (body == nullptr) return ArrayList<String>{};

  LOG(verbosity::Info,
      "running the completion function '%.*s' with %zu words, cursor word %zu",
      static_cast<int>(function_name.length), function_name.data,
      words.count(), cword);

  /* A completion function is bash code that reads and writes arrays, COMP_WORDS
     and COMPREPLY above all, so the call evaluates in bash mode whatever the
     interactive session's mode, then the mode is put back. */
  let const saved_mood = m_mood;
  m_mood = mimic_mood::Bash;
  defer { m_mood = saved_mood; };

  /* bash-completion is written for bash's lax defaults and reads unset names
     such as SHELLOPTS freely, so the mood-seeded strictness relaxes for the
     function run the way the mood does. An explicit set -u or set -o failglob
     is the user's own ask and stays fatal, the same rule -W follows. */
  let const saved_error_unset = m_error_unset;
  let const saved_failglob = m_failglob;
  if (!m_error_unset_explicit) m_error_unset = false;
  if (!m_failglob_explicit) m_failglob = false;
  defer
  {
    m_error_unset = saved_error_unset;
    m_failglob = saved_failglob;
  };

  /* The completion variables bash exposes to the function, the words of the
     line, the index of the word under the cursor, and the raw line and byte. */
  let comp_words = ArrayList<String>{};
  comp_words.reserve(words.count());
  for (const String &word : words)
    comp_words.push_managed(word.view());
  set_indexed_array("COMP_WORDS", steal(comp_words));
  set_shell_variable("COMP_CWORD", utils::int_to_text(static_cast<i64>(cword)));
  set_shell_variable("COMP_LINE", line);
  set_shell_variable("COMP_POINT", utils::int_to_text(static_cast<i64>(point)));

  /* bash invokes the function with the command, the current word, and the
     previous word as its first three positional parameters. */
  let call_params = ArrayList<String>{};
  call_params.push(
      words.is_empty() ? String{} : String{heap_allocator(), words[0].view()});
  call_params.push(cword < words.count()
                       ? String{heap_allocator(), words[cword].view()}
                       : String{});
  call_params.push(cword > 0 && cword - 1 < words.count()
                       ? String{heap_allocator(), words[cword - 1].view()}
                       : String{});

  let saved_params = take_positional_params();
  set_positional_params(steal(call_params));
  defer { set_positional_params(steal(saved_params)); };

  enter_function_call(SourceLocation{});
  defer { leave_function_call(); };
  let const saved_loop_depth = loop_depth();
  set_loop_depth(0);
  defer { set_loop_depth(saved_loop_depth); };
  enter_function_scope();
  /* The completion function reads its own name through FUNCNAME the way any
     called function does, so the call name rides the scope. */
  push_function_call_name(function_name);
  defer
  {
    pop_function_call_name();
    leave_function_scope();
  };
  let const saved_terminal_exec = terminal_exec_allowed();
  set_terminal_exec_allowed(false);
  defer { set_terminal_exec_allowed(saved_terminal_exec); };
  /* The retitle gate reads this, so a command a completion runs internally,
     the man fork or a loader's child, never renames the window. */
  set_completion_function_running(true);
  defer { set_completion_function_running(false); };

  /* A completion function that errors must not abort the prompt, so any error
     is swallowed and yields no candidates, and a stray break or return is
     consumed so it does not escape into the line editor. The swallow logs the
     error, since a completion that silently produces nothing is otherwise
     undebuggable. */
  try {
    body->evaluate(*this);
  } catch (const ErrorBase &error) {
    LOG(verbosity::Debug, "completion function '%.*s' threw: %s",
        static_cast<int>(function_name.length), function_name.data,
        error.message().c_str());
  }
  /* The function's return status is read before the control flow is cleared, so
     a dynamic loader that returns 124 to request a retry is seen by the caller.
   */
  if (out_exit_status != nullptr) *out_exit_status = last_exit_status();
  if (has_pending_control_flow()) clear_control_flow();

  let result = ArrayList<String>{};
  if (const ArrayList<String> *reply = lookup_indexed_array("COMPREPLY");
      reply != nullptr)
  {
    result.reserve(reply->count());
    for (const String &entry : *reply)
      result.push_managed(entry.view());
  }
  LOG(verbosity::Info,
      "completion function '%.*s' returned %zu candidates with status %d",
      static_cast<int>(function_name.length), function_name.data,
      result.count(),
      out_exit_status != nullptr ? *out_exit_status : last_exit_status());
  return result;
}

fn EvalContext::run_source(StringView source, StringView origin,
                           bool consume_return, Maybe<SourceLocation> call_site,
                           Maybe<StringView> filename) throws -> i32
{
  /* Parse into the active arena, coexisting with the outer tree, the same way a
     command substitution does. The control-flow exceptions are not caught here,
     so a return or a break inside the evaluated source reaches the caller. */
  if (AST_ARENA == nullptr) throw Error{"Cannot run source outside of a parse"};

  LOG(verbosity::Debug, "running source '%.*s' of %zu bytes at depth %zu",
      static_cast<int>(origin.length), origin.data, source.length,
      m_source_depth);

  /* Bound the source and eval nesting so a file that sources itself, or an eval
     that re-evals forever, errors here rather than growing the arena and the
     backtrace stack until memory is exhausted. The cap is checked against the
     call site so the caret points at the dot or eval, falling back to a zero
     location when no call site is known. The leave runs at function scope on
     every unwind path. */
  enter_source(call_site ? *call_site : SourceLocation{0, 0});
  defer { leave_source(); };

  /* The source the call site lives in, captured before set_current_source below
     changes it, so a backtrace caret renders the dot or eval against the parent
     text rather than the source about to run. It is nullptr when no call site
     is known, which sends the backtrace to the plain origin message. */
  let const parent_source = call_site ? m_current_source : nullptr;

  /* The frame joins the backtrace stack for the length of this call, so an
     error deep in a nested source prints every call site. The pop runs at
     function scope, after the catch below has read the stack. A frame with no
     call site stores a zero location, unused because parent_source is nullptr.
   */
  m_source_frames.push(source_frame{
      String{origin},
      call_site ? *call_site : SourceLocation{0, 0},
      parent_source, filename.has_value() ? String{*filename}
      : String{}
  });
  /* The sourced-file counter rides the frame stack, a file frame carries its
     path while an eval frame carries none, so the FUNCNAME classification
     stays a constant-time read. */
  let const frame_is_sourced_file =
      filename.has_value() && !filename->is_empty();
  if (frame_is_sourced_file) m_sourced_file_frames++;
  defer
  {
    if (frame_is_sourced_file) m_sourced_file_frames--;
    m_source_frames.pop_back();
  };

  /* The whole chain from the innermost source out to the outermost is printed
     when an error is caught, so every nested call site is named, not only the
     one running now. A frame whose parent source is known renders a caret at
     its call site, otherwise it falls back to naming the origin. */

  /* Retain an owned copy of the filename, so the views the lexer stamps onto
     every location stay valid after this call returns. The caller passes a view
     into transient storage, such as the dot builtin's local path, while a
     control-flow jump can carry a stamped location out to the top level where
     that storage is already gone. The copy lives as long as the retained
     source, freed together at the next top-level command. */
  Maybe<StringView> stable_filename = None;
  if (filename.has_value()) {
    let const retained_filename = new String{*filename};
    m_retained_sources.push(retained_filename);
    stable_filename = retained_filename->view();
  }

  /* A located error from the sourced text carries an offset into that text, not
     into the caller's command, so it is formatted here against the source and
     marked with its origin. Otherwise the caller would print the caret against
     the wrong line. */
  try {
    let parser = Parser{
        Lexer{String{source}, *AST_ARENA, false, stable_filename, mood()}
    };

    /* Retain the AST before evaluating, so a function it defines outlives this
       call and a control-flow exception thrown inside still leaves it owned.
       The destructor runs at the next top-level command, freeing the node
       members while the arena storage is reclaimed by the reset. */
    let const ast = parser.construct_ast();
    ASSERT(ast != nullptr);
    m_retained_source_asts.push(ast);

    /* Keep a copy of the source alive for as long as the AST, so a control-flow
       jump made inside it can point a caret at the right text even after this
       call returns and the jump propagates to the caller. The pointer below
       indexes this retained buffer, which survives until clear_retained_sources
       runs at the next top-level command. */
    let const retained_source = new String{source};
    m_retained_sources.push(retained_source);

    let const previous_source = m_current_source;
    let const previous_origin = m_current_origin;
    let const previous_location_position = m_current_location_position;
    set_current_source(retained_source, String{origin});
    /* The sourced text has its own line numbering, so $LINENO inside it counts
       from its first line. The parent position is restored on return so the
       caller's $LINENO resumes against the caller's source. */
    m_current_location_position = 0;
    defer
    {
      set_current_source(previous_source, previous_origin);
      m_current_location_position = previous_location_position;
    };

    ast->evaluate(*this);
    /* A return at the top of a sourced file or an eval returns from that source
       with its status, the way a return ends a function. Break, continue, and
       exit keep propagating, so an enclosing loop or the shell consumes them.
     */
    if (consume_return && has_pending_control_flow() &&
        pending_control_flow().kind == control_flow::Kind::Return)
    {
      let const source_status = static_cast<i32>(pending_control_flow().value);
      clear_control_flow();
      set_last_exit_status(source_status);
      return source_status;
    }
    return last_exit_status();
  } catch (const ErrorWithLocationAndDetails &e) {
    show_message(e.to_string(source));
    show_message(e.details_to_string(source));
    print_source_backtrace();
    return 1;
  } catch (const ErrorWithLocation &e) {
    show_message(e.to_string(source));
    print_source_backtrace();
    return 1;
  } catch (const Error &e) {
    show_message(e.to_string());
    print_source_backtrace();
    return 1;
  }
}

fn EvalContext::clear_retained_sources() wontthrow -> void
{
  LOG(verbosity::All, "dropping %zu retained sources and %zu retained asts",
      m_retained_sources.count(), m_retained_source_asts.count());
  /* The retained AST nodes live in the arena, which runs every node's
     destructor on the reset that follows, so this only drops the references. */
  m_retained_source_asts.clear();

  /* The retained source buffers and filenames are heap String copies owned
     here, so they are freed explicitly. */
  for (String *source : m_retained_sources)
    delete source;
  m_retained_sources.clear();

  /* The located-error formatter caches a line index keyed on the source address
     and length. A just-freed buffer can be reissued at the same address with
     the same length, so the cache is dropped here to keep it from serving the
     stale index of the freed source. */
  invalidate_source_line_index();

  /* The $LINENO line lookup caches a newline table keyed the same way on the
     source address and length, so it is dropped here for the same reason. */
  utils::invalidate_line_number_cache();

  /* The current source frame may point at a retained copy just freed, so reset
     it to None until the next run sets it. */
  m_current_source = nullptr;
  m_current_origin.clear();
}

fn EvalContext::retain_ast(Expression *ast) throws -> void
{
  m_retained_source_asts.push(ast);
}

fn EvalContext::expand_heredoc_body(StringView body) throws -> String
{
  LOG(verbosity::Debug, "expanding a heredoc body of %zu bytes", body.length);
  /* A heredoc body keeps its quote characters literally. */
  return expand_modifier_word(body, false);
}

namespace {

/* The byte that stands in for an opaque segment in the brace-expansion
   template, a quoted run or a variable reference whose braces and commas must
   not act as brace structure. It is followed by the segment's index. */
constexpr char BRACE_OPAQUE_MARKER = '\x01';

/* True when a word carries a { in an unquoted segment, the only place brace
   structure can appear. The scan is cheap so the common brace-free word skips
   the expansion entirely. */
pure fn word_has_brace_candidate(const Word &word) wontthrow -> bool
{
  for (const WordSegment &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::UnquotedText) continue;
    for (usize i = 0; i < segment.text.count(); i++)
      if (segment.text[i] == '{') return true;
  }
  return false;
}

/* The leftmost brace group that holds a top-level comma, the open and close
   indices with the comma offsets. A { with no matching } or no top-level comma
   is literal, so the search moves to the next {. */
/* Parse a signed integer and report the width of its digit run, so a sequence
   such as {01..10} can pad its output to the wider operand. None when the text
   is not a plain optionally-signed integer. */
struct sequence_integer
{
  i64 value;
  usize digit_width;
  bool has_leading_zero;
};

fn parse_sequence_integer(StringView text) wontthrow -> Maybe<sequence_integer>
{
  if (text.is_empty()) return None;
  usize i = 0;
  if (text[0] == '-' || text[0] == '+') i++;
  const usize digit_start = i;
  for (; i < text.length; i++)
    if (text[i] < '0' || text[i] > '9') return None;
  if (i == digit_start) return None;

  i64 magnitude = 0;
  for (usize j = digit_start; j < text.length; j++) {
    const i64 digit = text[j] - '0';
    /* A bound past the signed range is not a usable sequence, so the brace is
       left literal rather than overflowing during the parse. */
    if (magnitude > (9223372036854775807LL - digit) / 10) return None;
    magnitude = magnitude * 10 + digit;
  }
  const i64 value = text[0] == '-' ? -magnitude : magnitude;
  const usize width = text.length - digit_start;
  const bool leading_zero = width > 1 && text[digit_start] == '0';
  return sequence_integer{value, width, leading_zero};
}

/* Split a sequence body on its .. separators into two or three parts, the
   start, the end, and an optional step. */
fn split_sequence_parts(StringView content, Allocator alloc) throws
    -> ArrayList<StringView>
{
  let parts = ArrayList<StringView>{alloc};
  usize start = 0;
  usize i = 0;
  while (i + 1 < content.length) {
    if (content[i] == '.' && content[i + 1] == '.') {
      parts.push(content.substring_of_length(start, i - start));
      i += 2;
      start = i;
      continue;
    }
    i++;
  }
  parts.push(content.substring(start));
  return parts;
}

/* The elements of a {start..end} or {start..end..step} sequence, numeric or
   single-letter, or None when the body is not a sequence. */
fn parse_brace_sequence(StringView content, Allocator alloc) throws
    -> Maybe<ArrayList<String>>
{
  let const parts = split_sequence_parts(content, alloc);
  if (parts.count() != 2 && parts.count() != 3) return None;

  i64 step = 1;
  if (parts.count() == 3) {
    let const parsed_step = parse_sequence_integer(parts[2]);
    if (!parsed_step.has_value()) return None;
    step = parsed_step->value;
  }
  if (step == 0) step = 1;
  const i64 magnitude = step < 0 ? -step : step;

  let const start_int = parse_sequence_integer(parts[0]);
  let const end_int = parse_sequence_integer(parts[1]);
  if (start_int.has_value() && end_int.has_value()) {
    const i64 from = start_int->value;
    const i64 to = end_int->value;
    const i64 increment = from <= to ? magnitude : -magnitude;
    const bool pad = start_int->has_leading_zero || end_int->has_leading_zero;
    const usize width = pad ? (start_int->digit_width > end_int->digit_width
                                   ? start_int->digit_width
                                   : end_int->digit_width)
                            : 0;
    let elements = ArrayList<String>{alloc};
    for (i64 v = from; increment > 0 ? v <= to : v >= to; v += increment) {
      String number = utils::int_to_text(v);
      if (pad) {
        const bool negative = !number.is_empty() && number.view()[0] == '-';
        const StringView digits = number.view().substring(negative ? 1 : 0);
        if (digits.length < width) {
          let padded = String{alloc};
          if (negative) padded.push('-');
          for (usize z = digits.length; z < width; z++)
            padded.push('0');
          padded.append(digits);
          number = steal(padded);
        }
      }
      elements.push(steal(number));
    }
    return elements;
  }

  /* A single-letter range counts through the alphabet. */
  if (parts[0].length == 1 && parts[1].length == 1) {
    const char from = parts[0][0];
    const char to = parts[1][0];
    const bool from_alpha =
        (from >= 'a' && from <= 'z') || (from >= 'A' && from <= 'Z');
    const bool to_alpha = (to >= 'a' && to <= 'z') || (to >= 'A' && to <= 'Z');
    if (from_alpha && to_alpha) {
      const i64 increment = from <= to ? magnitude : -magnitude;
      let elements = ArrayList<String>{alloc};
      for (i64 c = from; increment > 0 ? c <= to : c >= to; c += increment) {
        let element = String{alloc};
        element.push(static_cast<char>(c));
        elements.push(steal(element));
      }
      return elements;
    }
  }
  return None;
}

/* The alternatives of a brace group body, the comma-separated list or the
   sequence it spells, or None when the body is neither and the braces are
   literal. */
fn brace_group_alternatives(StringView content, Allocator alloc) throws
    -> Maybe<ArrayList<String>>
{
  usize depth = 0;
  let comma_positions = ArrayList<usize>{alloc};
  for (usize i = 0; i < content.length; i++) {
    const char c = content[i];
    if (c == '{') {
      depth++;
    } else if (c == '}') {
      if (depth > 0) depth--;
    } else if (c == ',' && depth == 0) {
      comma_positions.push(i);
    }
  }

  if (!comma_positions.is_empty()) {
    let alternatives = ArrayList<String>{alloc};
    usize start = 0;
    for (const usize comma : comma_positions) {
      alternatives.push(
          String{alloc, content.substring_of_length(start, comma - start)});
      start = comma + 1;
    }
    alternatives.push_managed(content.substring(start));
    return alternatives;
  }

  return parse_brace_sequence(content, alloc);
}

struct brace_group
{
  usize open;
  usize close;
  ArrayList<String> alternatives{heap_allocator()};
};

fn find_brace_group(StringView text, Allocator alloc) throws
    -> Maybe<brace_group>
{
  for (usize open = 0; open < text.length; open++) {
    if (text[open] != '{') continue;
    usize depth = 0;
    for (usize j = open; j < text.length; j++) {
      const char c = text[j];
      if (c == '{') {
        depth++;
      } else if (c == '}') {
        depth--;
        if (depth == 0) {
          let alternatives = brace_group_alternatives(
              text.substring_of_length(open + 1, j - open - 1), alloc);
          if (alternatives.has_value()) {
            let group = brace_group{open, j, {}};
            group.alternatives = steal(*alternatives);
            return group;
          }
          break;
        }
      }
    }
  }
  return None;
}

/* The deepest brace nesting expanded before the recursion is cut, so a
   pathological input such as {a,{b,{c,...}}} cannot overflow the native stack.
   The cap matches the globstar one for one consistent bound on user-driven
   recursion. */
constexpr usize MAX_BRACE_DEPTH = 256;

/* Expand the brace structure in a template string, leaving any opaque marker
   untouched. The recursion handles a nested group inside an alternative and a
   further group after the close, so the result is the cartesian product. A
   nesting past the cap leaves the remaining text literal rather than recursing
   further, the way a runaway expansion is bounded instead of crashing. */
fn brace_expand_text(StringView text, Allocator alloc, usize depth = 0) throws
    -> ArrayList<String>
{
  let results = ArrayList<String>{alloc};
  let const group = find_brace_group(text, alloc);
  if (!group.has_value() || depth >= MAX_BRACE_DEPTH) {
    results.push_managed(text);
    return results;
  }

  const StringView preamble = text.substring_of_length(0, group->open);
  const StringView postamble = text.substring(group->close + 1);
  let const post_expansions = brace_expand_text(postamble, alloc, depth + 1);

  for (const String &alternative : group->alternatives) {
    for (const String &expanded_alt :
         brace_expand_text(alternative.view(), alloc, depth + 1))
    {
      for (const String &expanded_post : post_expansions) {
        let combined = String{alloc, preamble};
        combined.append(expanded_alt.view());
        combined.append(expanded_post.view());
        results.push(steal(combined));
      }
    }
  }
  return results;
}

/* Expand the brace structure of a word into the words it spells. The unquoted
   segments contribute their text to a template while every other segment is
   recorded as an opaque marker, so a quoted brace or a variable stays intact.
   Each expanded template is rebuilt into a word. */
fn expand_braces(const Word &word, Allocator alloc) throws -> ArrayList<Word>
{
  let opaque_segments = ArrayList<const WordSegment *>{alloc};
  let word_template = String{alloc};
  for (const WordSegment &segment : word.segments) {
    if (segment.kind == WordSegment::Kind::UnquotedText) {
      word_template.append(segment.text.view());
    } else {
      ASSERT(opaque_segments.count() < 256);
      word_template.push(BRACE_OPAQUE_MARKER);
      word_template.push(static_cast<char>(opaque_segments.count()));
      opaque_segments.push(&segment);
    }
  }

  let const expanded = brace_expand_text(word_template.view(), alloc);

  let words = ArrayList<Word>{alloc};
  for (const String &produced : expanded) {
    let out = Word{};
    let run = String{alloc};
    for (usize i = 0; i < produced.count(); i++) {
      const char c = produced[i];
      /* The marker is followed by an in-range segment index only when this
         scanner inserted it. A literal 0x01 byte that reached the text from
         $'\x01' is not a marker, so the index bound is checked at run time and
         a failed check copies the byte verbatim rather than reading past the
         segment list. */
      const bool is_opaque_marker =
          c == BRACE_OPAQUE_MARKER && i + 1 < produced.count() &&
          static_cast<u8>(produced[i + 1]) < opaque_segments.count();
      if (is_opaque_marker) {
        if (!run.is_empty()) {
          out.segments.push(
              WordSegment{WordSegment::Kind::UnquotedText, steal(run), false});
          run = String{alloc};
        }
        const u8 index = static_cast<u8>(produced[++i]);
        out.segments.push(*opaque_segments[index]);
      } else {
        run.push(c);
      }
    }
    if (!run.is_empty()) {
      out.segments.push(
          WordSegment{WordSegment::Kind::UnquotedText, steal(run), false});
    }
    words.push(steal(out));
  }
  LOG(verbosity::Debug, "brace expansion produced %zu words", words.count());
  return words;
}

} /* namespace */

hot fn EvalContext::process_args(const ArrayList<const Token *> &args,
                                 bool args_are_transient) throws
    -> ArrayList<String>
{
  LOG(verbosity::Debug, "expanding %zu argument tokens", args.count());
  /* The argument vector is built first, on the scratch arena for a transient
     request the caller scopes and frees, or on the heap otherwise. The per-word
     expansion fields are reclaimed on return only for the heap form, since the
     transient form leaves the fields on the caller's scratch region to be freed
     with the vector after the command. The mark nests, so a command
     substitution inside one of these words reclaims only its own fields. */
  let expanded_args = args_are_transient
                          ? ArrayList<String>{scratch_allocator()}
                          : ArrayList<String>{};
  expanded_args.reserve(args.count());

  let const fields_mark = m_scratch_arena.mark();
  defer
  {
    if (!args_are_transient) m_scratch_arena.release(fields_mark);
  };

  /* A declaration builtin, such as local or export, treats a name=value
     argument as an assignment, so its value expands with no field splitting or
     globbing. The command word is the first argument, and only its plain
     literal form is treated this way, the same form bash decides on before any
     expansion. */
  let is_declaration_command = false;
  let is_local_command = false;
  let is_declare_command = false;
  let is_test_command = false;
  if (!args.is_empty() && args[0]->kind() == Token::Kind::Word) {
    const Word &command_word =
        static_cast<const tokens::WordToken *>(args[0])->word();
    if (command_word.plain_literal_kind() != Word::PlainLiteral::NotPlain) {
      /* Nearly every command word is one literal segment, so its view serves
         directly and the joined copy is built only for the rare split word. */
      let joined_name = String{scratch_allocator()};
      StringView name;
      if (command_word.segments.count() == 1) {
        name = command_word.segments[0].text.view();
      } else {
        for (const WordSegment &segment : command_word.segments)
          joined_name.append(segment.text.view());
        name = joined_name.view();
      }
      is_local_command = name == "local";
      is_declare_command = name == "declare" || name == "typeset";
      is_declaration_command = is_local_command || is_declare_command ||
                               name == "export" || name == "readonly";
      is_test_command = name == "test";
    }
    /* The lone bracket carries a glob metacharacter, so it never classifies as
       a plain literal above, while as a command word it is the test builtin
       and earns the same glob exemption. */
    else if (command_word.segments.count() == 1 &&
             command_word.segments[0].kind == WordSegment::Kind::UnquotedText &&
             command_word.segments[0].text.view() == "[")
    {
      is_test_command = true;
    }
  }

  /* A test or [ command reads its arguments to probe the filesystem, so an
     unmatched glob there stays literal in silence and the probe returns false
     naturally, rather than tripping failglob on the check that asks whether a
     file exists. A user function named test keeps the exemption, the cost of
     deciding before expansion. */
  let const previous_glob_exempt = m_glob_exempt_for_test;
  m_glob_exempt_for_test = is_test_command;
  defer { m_glob_exempt_for_test = previous_glob_exempt; };

  for (const Token *t : args) {
    let const l = t->source_location();
    try {
      /* A word token is expanded in place. Any other token is wrapped as one
         unquoted literal word, which is the only case that needs a temporary.
       */
      let fallback_word = Word{};
      const Word *word = nullptr;
      if (t->kind() == Token::Kind::Word) {
        word = &static_cast<const tokens::WordToken *>(t)->word();
      } else if (t->kind() == Token::Kind::Assignment) {
        let const a = static_cast<const tokens::Assignment *>(t);
        ASSERT(a != nullptr);
        if (is_declaration_command) {
          /* A declaration builtin treats name=value as an assignment, so the
             value expands with no field splitting or globbing, the way a plain
             x=$1 does, rather than splitting into several arguments. */
          let assignment = String{expanded_args.allocator()};
          assignment.append(a->key().view());
          if (a->is_append() && (is_local_command || is_declare_command)) {
            /* local creates a fresh local that shadows an outer name, and
               declare may apply the -i attribute on the same command, so the
               name+=value form passes through literally and the builtin
               computes the append after its own effects exist. */
            assignment += '+';
            assignment += '=';
            assignment.append(
                expand_word_for_assignment(a->value_word()).view());
          } else {
            assignment += '=';
            /* The append form name+=value concatenates onto the name's current
               value, so the string the builtin stores already carries it, the
               way a plain x+=y assignment prepends the prior value. export and
               readonly do not shadow, so the current value is read here
               correctly. */
            if (a->is_append())
              assignment.append(
                  get_variable_value(a->key()).value_or(String{}).view());
            let const expanded_value =
                expand_word_for_assignment(a->value_word());
            /* An integer name adds rather than concatenates, so the join wraps
               the appended expression for the arithmetic in the store. */
            if (a->is_append() && is_integer_variable(a->key()))
              append_integer_expression(assignment, expanded_value.view());
            else
              assignment.append(expanded_value.view());
          }
          expanded_args.push(steal(assignment));
          continue;
        }
        /* An assignment that appears as an argument, like echo k=$v, is an
           ordinary word. Rebuild it as the literal key, an equals sign, and the
           value segments, so the value still expands instead of staying
           literal. */
        let key_literal = String{StringView{a->key()}};
        /* A non-declaration command keeps the literal text, so an append form
           such as echo k+=v stays k+=v rather than losing the plus. */
        if (a->is_append()) key_literal += "+";
        key_literal += "=";
        fallback_word.segments.push(WordSegment{WordSegment::Kind::LiteralText,
                                                steal(key_literal), false});
        let const &value = a->value_word();
        for (const WordSegment &value_segment : value.segments)
          fallback_word.segments.push(value_segment);
        word = &fallback_word;
      } else {
        fallback_word.segments.push(WordSegment{WordSegment::Kind::UnquotedText,
                                                t->raw_string(), false});
        word = &fallback_word;
      }

      /* The plain-literal fast path pushes a word that needs no expansion,
         splitting, or globbing straight to the heap argument vector. The common
         literal argument such as '-lt', '200000', 'echo', or a plain filename
         takes this path and never enters expand_word or expand_path. */
      auto expand_one_word = [&](const Word &expandable) throws -> void {
        let const plain_kind = expandable.plain_literal_kind();
        let took_fast_path = false;
        if (plain_kind != Word::PlainLiteral::NotPlain) {
          let literal = String{expanded_args.allocator()};
          for (const WordSegment &segment : expandable.segments)
            literal.append(segment.text.view());

          /* A single unquoted segment still needs the IFS check, since an IFS
             byte in its text would split it into more than one field. With no
             IFS byte it is one field. */
          let needs_split = false;
          if (plain_kind == Word::PlainLiteral::PlainUnquotedOneSegment) {
            for (usize i = 0; i < literal.count(); i++)
              if (is_field_separator(literal[i])) {
                needs_split = true;
                break;
              }
          }

          if (!needs_split) {
            expanded_args.push(steal(literal));
            took_fast_path = true;
          }
        }

        if (!took_fast_path) {
          for (glob_field &field : expand_word(expandable)) {
            for (String &g : expand_path(steal(field), l))
              expanded_args.push_managed(StringView{g.c_str(), g.count()});
          }
        }
      };

      /* Brace expansion runs first in bash mode, turning one word into the
         several the braces spell, each then taking the path above. The brace
         scan is skipped when no { is present, so a brace-free word pays nothing
         beyond the cheap check. */
      if (is_bash_compatible() && word_has_brace_candidate(*word)) {
        for (const Word &brace_word : expand_braces(*word, scratch_allocator()))
          expand_one_word(brace_word);
      } else {
        expand_one_word(*word);
      }
    } catch (const Error &e) {
      throw relocate_error(e, l);
    }
  }

  /* The trace goes to standard error, the way bash does it, so it stays out of
     a command substitution's captured output. The plus is repeated once per
     enclosing subshell, so the top shell shows '+', a substitution '++', and a
     nested one '+++'. */
  if (should_echo_expanded()) {
    let trace = String{};
    for (usize i = 0; i < m_subshell_depth + 1; i++)
      trace.push('+');
    trace.push(' ');
    trace.append(utils::merge_args_to_string(expanded_args));
    trace.push('\n');
    shit::print_error(trace);
  }

  return expanded_args;
}

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
