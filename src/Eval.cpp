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

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <exception>

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
  /* An exported name lives in the process environment, where export moved it and
     cleared the shell copy. A plain reassignment writes the shell store above,
     so an in-process read sees the new value, but a child still inherits the
     stale environment entry. The environment is refreshed here when the name is
     already present there, so a child of `export FOO=1; FOO=2` sees 2. A
     non-exported name is absent from the environment, so the common case only
     pays the getenv scan the read miss already pays. */
  if (os::get_environment_variable(name).has_value())
    os::set_environment_variable(name, value);
}

fn EvalContext::set_field_separators(StringView value) throws -> void
{
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
    throw Error{"'" + name + "' is read only and cannot be assigned"};

  assign_variable(name, value);
}

fn EvalContext::unset_shell_variable(StringView name) throws -> void
{
  /* A read-only variable rejects removal the same way it rejects assignment,
     so unset cannot defeat readonly. */
  if (is_readonly(name))
    throw Error{"'" + name + "' is read only and cannot be unset"};

  force_unset_shell_variable(name);
}

fn EvalContext::force_unset_shell_variable(StringView name) throws -> void
{
  m_shell_variables.erase(name);
  /* An exported variable also lives in the process environment, so it is
     removed there too. Otherwise a later lookup falls back to the stale
     environment value and the variable appears still set, which dash does not
     do. */
  os::unset_environment_variable(name);
  if (name == "IFS") set_field_separators(" \t\n");
  /* An unset PATH drops the search order, so the resolver falls back to the
     process environment's PATH, which this just removed and so reads None. An
     export PATH=... routes through here to drop the bare copy, then sets the
     environment and refreshes the resolver itself, so the None set here is
     transient on that path. */
  if (name == "PATH")
    utils::set_path_for_resolution(os::get_environment_variable("PATH"));
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
    case '?':
      return String{heap_allocator(), utils::int_to_text(m_last_exit_status)};
    case '$':
      return String{heap_allocator(),
                    utils::int_to_text(os::get_shell_process_id())};
    case '!':
      return m_last_background_pid
                 ? String{heap_allocator(),
                          utils::int_to_text(*m_last_background_pid)}
                 : String{};
    case '-': return String{heap_allocator(), option_flags_string()};
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
      if (name.count() > 9) return String{};
      let const parsed_index = utils::parse_decimal_integer(name);
      if (parsed_index.is_error()) return String{};
      let const index = static_cast<usize>(parsed_index.value());
      if (index >= 1 && index <= m_positional_params.count()) {
        ASSERT(index - 1 < m_positional_params.count());
        return m_positional_params[index - 1];
      }
      return String{};
    }
  }

  if (let const *stored = m_shell_variables.find(name)) return *stored;

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
    return String{heap_allocator(), utils::uint_to_text(line)};
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

fn EvalContext::set_last_background_pid(i64 pid) wontthrow -> void
{
  m_last_background_pid = pid;
}

fn EvalContext::register_job(os::process pid, StringView command) throws -> int
{
  let new_job = job{};
  new_job.id = m_next_job_id++;
  new_job.pid = pid;
  new_job.command = command;
  new_job.state = job::State::Running;
  m_jobs.push(steal(new_job));
  ASSERT(!m_jobs.is_empty());
  LOG(verbosity::Debug, "registered job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn EvalContext::update_jobs() throws -> void
{
  for (job &job : m_jobs) {
    if (job.state == job::State::Done) continue;

    i32 status = 0;
    let const state = os::poll_process(job.pid, status);
    if (state == os::process_state::Exited) {
      job.state = job::State::Done;
      job.last_status = status;
    } else if (state == os::process_state::Stopped) {
      job.state = job::State::Stopped;
    } else {
      job.state = job::State::Running;
    }
  }
}

fn EvalContext::jobs() wontthrow -> ArrayList<job> & { return m_jobs; }

fn EvalContext::find_job(int id) wontthrow -> job *
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

    String line{};
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
  m_monitor = enabled;
}

pure fn EvalContext::monitor() const wontthrow -> bool { return m_monitor; }

fn EvalContext::register_function(StringView name,
                                  const Expression *body) throws -> void
{
  m_functions.set(name, body);
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
  m_functions.erase(name);
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

fn EvalContext::variable_names() const throws -> HashSet
{
  let names = HashSet{heap_allocator()};
  m_shell_variables.for_each([&](StringView name, const String &value) {
    unused(value);
    names.add(name);
  });
  return names;
}

fn EvalContext::set_trap(StringView condition, StringView action) throws -> void
{
  m_traps.set(condition, action);
}

fn EvalContext::remove_trap(StringView condition) throws -> void
{
  m_traps.erase(condition);
}

pure fn EvalContext::traps() const wontthrow -> const HashMap<String> &
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
    if (action->count() > 0) run_source(action->view(), "the EXIT trap");
}

fn EvalContext::has_exit_trap() const wontthrow -> bool
{
  if (let const *action = m_traps.find(StringView{"EXIT", 4}))
    return action->count() > 0;
  return false;
}

fn EvalContext::mark_readonly(StringView name) throws -> void
{
  if (is_readonly(name)) return;
  m_readonly_names.push(String{heap_allocator(), name});
}

fn EvalContext::is_readonly(StringView name) const wontthrow -> bool
{
  if (m_readonly_names.count() == 0) return false;
  for (const String &readonly_name : m_readonly_names)
    if (StringView{readonly_name.c_str(), readonly_name.count()} == name)
      return true;
  return false;
}

fn EvalContext::readonly_names() const throws -> ArrayList<String>
{
  let out = ArrayList<String>{};
  for (const String &name : m_readonly_names)
    out.push(String{
        heap_allocator(), StringView{name.c_str(), name.count()}
    });
  utils::sort_ascending(out);
  return out;
}

fn EvalContext::enter_function_scope() throws -> void
{
  m_local_scopes.push(ArrayList<local_binding>{});
}

fn EvalContext::leave_function_scope() throws -> void
{
  if (m_local_scopes.is_empty()) return;

  /* Restore each shadowed binding in reverse, so a name declared local twice
     ends with the value it held before the function ran. */
  ASSERT(!m_local_scopes.is_empty());
  let &scope = m_local_scopes.back();
  for (usize i = scope.count(); i > 0; i--) {
    ASSERT(i - 1 < scope.count());
    let &binding = scope[i - 1];
    /* Restore through assign_variable, not set_shell_variable, since this runs
       inside a noexcept defer and a readonly name would otherwise throw from a
       destructor and terminate the shell. A local marked readonly in the body
       is being torn down here, so the restore of the outer value is valid. */
    if (binding.previous_value.has_value())
      assign_variable(binding.name, *binding.previous_value);
    else
      force_unset_shell_variable(binding.name);
  }
  let kept = ArrayList<ArrayList<local_binding>>{};
  for (usize i = 0; i + 1 < m_local_scopes.count(); i++)
    kept.push(steal(m_local_scopes[i]));
  m_local_scopes = steal(kept);
}

pure fn EvalContext::in_function_scope() const wontthrow -> bool
{
  return !m_local_scopes.is_empty();
}

fn EvalContext::declare_local(StringView name) throws -> void
{
  if (m_local_scopes.is_empty()) return;
  ASSERT(!m_local_scopes.is_empty());
  m_local_scopes.back().push(
      local_binding{String{name}, get_variable_value(name)});
}

fn EvalContext::set_alias(StringView name, StringView value) throws -> void
{
  m_aliases.set(name, value);
}

fn EvalContext::remove_alias(StringView name) throws -> bool
{
  if (m_aliases.find(name) == nullptr) return false;
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
    definition.append(StringView{value.c_str(), value.count()});
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

fn EvalContext::enter_subshell() wontthrow -> void { m_subshell_depth++; }

fn EvalContext::leave_subshell() wontthrow -> void
{
  ASSERT(m_subshell_depth > 0);
  m_subshell_depth--;
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
  LOG(verbosity::Debug, "break requested, level %lld of depth %zu",
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
  LOG(verbosity::Debug, "continue requested, level %lld of depth %zu",
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
}

fn EvalContext::leave_function_call() wontthrow -> void
{
  ASSERT(m_function_call_depth > 0);
  m_function_call_depth--;
}

fn EvalContext::set_error_exit(bool enabled) wontthrow -> void
{
  m_error_exit = enabled;
}

pure fn EvalContext::error_exit() const wontthrow -> bool
{
  return m_error_exit;
}

fn EvalContext::set_echo_expanded(bool enabled) wontthrow -> void
{
  m_enable_echo_expanded = enabled;
}

fn EvalContext::set_error_unset(bool enabled) wontthrow -> void
{
  m_error_unset = enabled;
}

pure fn EvalContext::error_unset() const wontthrow -> bool
{
  return m_error_unset;
}

fn EvalContext::set_no_clobber(bool enabled) wontthrow -> void
{
  m_no_clobber = enabled;
}

pure fn EvalContext::no_clobber() const wontthrow -> bool
{
  return m_no_clobber;
}

fn EvalContext::set_export_all(bool enabled) wontthrow -> void
{
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
  m_enable_path_expansion = !enabled;
}

pure fn EvalContext::no_glob() const wontthrow -> bool
{
  return !m_enable_path_expansion;
}

fn EvalContext::set_no_exec(bool enabled) wontthrow -> void
{
  m_no_exec = enabled;
}

pure fn EvalContext::no_exec() const wontthrow -> bool { return m_no_exec; }

fn EvalContext::set_failglob(bool enabled) wontthrow -> void
{
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
    entry.append(StringView{value.c_str(), value.count()});
    assignments.push(steal(entry));
  });
  utils::sort_ascending(assignments);
  return assignments;
}

fn EvalContext::clear_functions() wontthrow -> void { m_functions.clear(); }

fn EvalContext::snapshot_state() const throws -> eval_state_snapshot
{
  return eval_state_snapshot{m_shell_variables, m_functions, m_aliases,
                             m_positional_params, Path::current_directory()};
}

fn EvalContext::restore_state(eval_state_snapshot snapshot) throws -> void
{
  m_shell_variables = steal(snapshot.shell_variables);
  m_functions = steal(snapshot.functions);
  /* An alias defined or removed inside a subshell or a command substitution
     stays inside it, the way bash isolates an alias change in a subshell. */
  m_aliases = steal(snapshot.aliases);
  m_positional_params = steal(snapshot.positional_params);
  /* set_current_directory reports an error through ErrorOr, ignored here to
     match the prior void call that swallowed a failed chdir. */
  (void) Path::set_current_directory(snapshot.working_directory);

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

pure fn EvalContext::last_exit_status() const wontthrow -> i32
{
  return m_last_exit_status;
}

hot fn EvalContext::expand_variable(StringView name) const throws -> String
{
  return get_variable_value(name).value_or(String{});
}

namespace {

enum class TrimEnd
{
  Prefix,
  Suffix,
};

/* Remove the shortest or longest prefix or suffix of value that matches pattern
   as a glob, returning the surviving part. The candidate substrings are views
   over value, so only the returned remainder allocates. The active mask runs
   parallel to pattern and marks which pattern bytes may act as glob
   metacharacters, so a quoted or escaped * or ? matches itself. */
fn trim_matching(StringView value, StringView pattern,
                 const ArrayList<bool> &active, TrimEnd end,
                 bool longest) throws -> String
{
  ASSERT(active.count() == pattern.length);

  if (end == TrimEnd::Prefix) {
    /* The longest match scans down from the whole string and the shortest
       scans up from the empty prefix, so the first hit is the wanted length. */
    if (longest) {
      for (usize length = value.length;; length--) {
        if (utils::glob_matches(pattern, value.substring_of_length(0, length),
                                active, 0))
          return String{heap_allocator(), value.substring(length)};
        if (length == 0) break;
      }
    } else {
      for (usize length = 0; length <= value.length; length++) {
        if (utils::glob_matches(pattern, value.substring_of_length(0, length),
                                active, 0))
          return String{heap_allocator(), value.substring(length)};
      }
    }
  } else {
    /* The longest match scans the suffix start up from byte zero and the
       shortest scans down from the end, so the first hit is the wanted start.
     */
    if (longest) {
      for (usize start = 0; start <= value.length; start++) {
        if (utils::glob_matches(pattern, value.substring(start), active, 0))
          return String{heap_allocator(), value.substring_of_length(0, start)};
      }
    } else {
      for (usize start = value.length;; start--) {
        if (utils::glob_matches(pattern, value.substring(start), active, 0))
          return String{heap_allocator(), value.substring_of_length(0, start)};
        if (start == 0) break;
      }
    }
  }
  return String{heap_allocator(), value};
}

} /* namespace */

fn EvalContext::expand_modifier_word(StringView word, bool remove_quotes) throws
    -> String
{
  /* The default, assign, alternate, error, and arithmetic forms never glob, so
     the mask the worker fills is discarded here. The default word keeps a
     backslash that sits before an ordinary character, so the pattern-only
     unescape stays off. */
  let discarded_mask = ArrayList<bool>{heap_allocator()};
  return expand_modifier_word_worker(word, discarded_mask, remove_quotes, false);
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
                                            bool is_pattern_word) throws -> String
{
  let out = String{heap_allocator()};

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
         the byte is emitted literally and inactive and the backslash is dropped,
         which makes a quoted glob character such as \* match itself. A trailing
         backslash with no following byte is kept literally. */
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
      String inner{heap_allocator()};
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
      String inner{heap_allocator()};
      usize j = i + 2;
      i32 depth = 1;
      while (j < word.length) {
        if (word[j] == '{') {
          depth++;
        } else if (word[j] == '}') {
          depth--;
          if (depth == 0) break;
        }
        inner += word[j];
        j++;
      }
      emit_run(apply_parameter_expansion(inner), !in_double_quote);
      i = j;
    } else if (lexer::is_variable_name_start(next)) {
      String name{heap_allocator()};
      usize j = i + 1;
      while (j < word.length && lexer::is_variable_name(word[j]))
        name += word[j++];
      /* A nested reference inside a default or alternate word, or a heredoc
         body, obeys set -u the same way a top level reference does, so an unset
         name here aborts rather than expanding to nothing. */
      if (m_error_unset && !get_variable_value(name).has_value())
        throw Error{name + ": parameter not set"};
      emit_run(expand_variable(name), !in_double_quote);
      i = j - 1;
    } else if (next == '(' && i + 2 < word.length && word[i + 2] == '(') {
      /* Arithmetic $((...)), scanned to the matching )). A quote run and a
         backslash escape keep their bytes literal so a ) inside a string is text
         and does not count toward the grouping depth or terminate the
         expansion. */
      String inner{heap_allocator()};
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
      String inner{heap_allocator()};
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
      if (m_error_unset && !get_variable_value(special_name).has_value())
        throw Error{special_name + ": parameter not set"};
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
  if (spec.is_empty()) return String{heap_allocator()};

  /* ${#name} is the length of the value, distinct from $# which is the count of
     positional parameters. */
  if (spec.length > 1 && spec[0] == '#') {
    let const name = spec.substring(1);
    if (name == "@" || name == "*")
      return String{heap_allocator(),
                    utils::uint_to_text(m_positional_params.count())};
    let const value = get_variable_value(name);
    if (m_error_unset && !value.has_value())
      throw Error{name + ": parameter not set"};
    return String{heap_allocator(),
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
  if (rest.is_empty()) {
    /* Under set -u a plain reference to a variable that is not set is an error,
       while a form with a modifier such as ${x:-w} handles the unset case
       itself. */
    if (m_error_unset && !get_variable_value(name).has_value())
      throw Error{name + ": parameter not set"};
    return expand_variable(name);
  }

  /* A leading colon makes the default, assign, alternate, and error forms treat
     an empty value as unset. */
  let const is_colon_form = rest[0] == ':';
  const usize op_index = is_colon_form ? 1 : 0;
  if (op_index >= rest.length) return expand_variable(name);

  let const op = rest[op_index];
  let const is_doubled = (op_index + 1 < rest.length &&
                          rest[op_index + 1] == op && (op == '#' || op == '%'));
  let const word = rest.substring(op_index + (is_doubled ? 2 : 1));

  let const current = get_variable_value(name);
  let const is_set = current.has_value();
  let const is_empty = !is_set || current->is_empty();
  let const treat_as_unset = is_colon_form ? is_empty : !is_set;

  switch (op) {
  case '-':
    if (treat_as_unset) return expand_modifier_word(word);
    ASSERT(current.has_value());
    return String{heap_allocator(), current->view()};
  case '=':
    if (treat_as_unset) {
      let const assigned = expand_modifier_word(word);
      set_shell_variable(name, assigned);
      return assigned;
    }
    ASSERT(current.has_value());
    return String{heap_allocator(), current->view()};
  case '+':
    if (treat_as_unset) return String{heap_allocator()};
    return expand_modifier_word(word);
  case '?':
    if (treat_as_unset) {
      if (word.is_empty()) throw Error{name + ": parameter not set or empty"};
      throw Error{expand_modifier_word(word)};
    }
    ASSERT(current.has_value());
    return String{heap_allocator(), current->view()};

  case '#': {
    let const value = current.value_or(String{});
    let pattern_active = ArrayList<bool>{heap_allocator()};
    let const pattern = expand_modifier_word_masked(word, pattern_active);
    return trim_matching(value.view(), pattern.view(), pattern_active,
                         TrimEnd::Prefix, is_doubled);
  }

  case '%': {
    let const value = current.value_or(String{});
    let pattern_active = ArrayList<bool>{heap_allocator()};
    let const pattern = expand_modifier_word_masked(word, pattern_active);
    return trim_matching(value.view(), pattern.view(), pattern_active,
                         TrimEnd::Suffix, is_doubled);
  }

  default: return expand_variable(name);
  }
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

/* TODO: Test symlinks. */
/* TODO: What the fuck is happening. */
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
  if (!entries.has_value()) return expanded;

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

    /* TODO: Figure the rules of hidden file expansion. */
    if (glob[0] != '.' && !filename.is_empty() && filename[0] == '.') continue;

    if (utils::glob_matches(glob, filename, field.glob_active, stem_start)) {
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
hot pure fn first_active_glob(StringView text,
                              const ArrayList<bool> &mask) wontthrow
    -> Maybe<usize>
{
  let open_bracket = Maybe<usize>{};
  for (usize i = 0; i < mask.count(); i++) {
    if (!mask[i]) continue;
    let const ch = text.data[i];
    if (ch == '*' || ch == '?') return i;
    if (ch == '[') {
      if (!open_bracket) open_bracket = i;
    } else if (ch == ']' && open_bracket) {
      return open_bracket;
    }
  }
  return shit::None;
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
    let const expand_ch = first_active_glob(text, field.glob_active);

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

fn EvalContext::expand_tilde(WordSegment &leading_segment) const throws -> void
{
  /* A tilde only expands when it is unquoted. An escaped or quoted tilde is a
     literal segment and stays as is. */
  if (!leading_segment.is_tilde_candidate()) return;

  let &text = leading_segment.text;
  if (text.is_empty() || text[0] != '~') return;

  /* TODO: There may be several separators supported. */
  /* Only a bare ~ or a ~/ prefix expands. ~user is left alone for now. */
  if (text.length() > 1 && text[1] != '/') return;

  let const home = os::get_home_directory();
  if (!home) throw Error{"Could not figure out home directory"};

  /* String has no in-place erase or insert, so the home path and the remainder
     after the tilde are joined into a fresh buffer and moved back. */
  let expanded = String{heap_allocator()};
  expanded.append(home->text().view());
  expanded.append(text.substring(1));
  text = steal(expanded);
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
      first_active_glob(field.text.view(), field.glob_active).has_value();

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

  /* A glob that matches no file is a hard error by default, the typo-catching
     behavior. With failglob off the shell takes the POSIX fallback and expands
     the glob to its literal pattern as a single field, the way dash does. The
     caret points at the offending word. */
  if (values.count() == 0) {
    if (m_failglob)
      throw ErrorWithLocation{location, "No matches for the glob pattern '" +
                                            pattern + "'"};
    values.push(steal(pattern));
  }

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
    return pos + op.length <= source.length &&
           source.substring_of_length(pos, op.length) == op;
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

    let const value = context->get_variable_value(name).value_or(String{});
    if (value.is_empty()) return 0;
    return parse_arithmetic_operand(value.view());
  }

  fn parse() throws -> i64
  {
    let const result = parse_assignment();
    skip_spaces();
    if (pos != source.length) fail("unexpected trailing characters");
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
      for (const auto &[op, kind] : compound_operators) {
        if (consume(op)) {
          let const rhs = parse_assignment();
          let const result =
              apply_compound(read_variable_value(name), rhs, kind);
          ASSERT(context != nullptr);
          context->set_shell_variable(name, utils::int_to_text(result));
          return result;
        }
      }
      if (starts_with("=") && !starts_with("==")) {
        consume("=");
        let const rhs = parse_assignment();
        ASSERT(context != nullptr);
        context->set_shell_variable(name, utils::int_to_text(rhs));
        return rhs;
      }
      pos = save;
    }
    return parse_ternary();
  }

  fn parse_ternary() throws -> i64
  {
    let const condition = parse_logical_or();
    if (consume("?")) {
      let const if_true = parse_assignment();
      if (!consume(":")) fail("expected ':' in a conditional");
      let const if_false = parse_ternary();
      return condition != 0 ? if_true : if_false;
    }
    return condition;
  }

  fn parse_logical_or() throws -> i64
  {
    let lhs = parse_logical_and();
    while (consume("||"))
      lhs = (lhs != 0 || parse_logical_and() != 0) ? 1 : 0;
    return lhs;
  }

  fn parse_logical_and() throws -> i64
  {
    let lhs = parse_bitwise_or();
    while (consume("&&"))
      lhs = (lhs != 0 && parse_bitwise_or() != 0) ? 1 : 0;
    return lhs;
  }

  fn parse_bitwise_or() throws -> i64
  {
    let lhs = parse_bitwise_xor();
    while (starts_with("|") && !starts_with("||")) {
      consume("|");
      lhs |= parse_bitwise_xor();
    }
    return lhs;
  }

  fn parse_bitwise_xor() throws -> i64
  {
    let lhs = parse_bitwise_and();
    while (consume("^"))
      lhs ^= parse_bitwise_and();
    return lhs;
  }

  fn parse_bitwise_and() throws -> i64
  {
    let lhs = parse_equality();
    while (starts_with("&") && !starts_with("&&")) {
      consume("&");
      lhs &= parse_equality();
    }
    return lhs;
  }

  fn parse_equality() throws -> i64
  {
    let lhs = parse_relational();
    for (;;) {
      if (consume("=="))
        lhs = (lhs == parse_relational()) ? 1 : 0;
      else if (consume("!="))
        lhs = (lhs != parse_relational()) ? 1 : 0;
      else
        break;
    }
    return lhs;
  }

  fn parse_relational() throws -> i64
  {
    let lhs = parse_shift();
    for (;;) {
      if (consume("<="))
        lhs = (lhs <= parse_shift()) ? 1 : 0;
      else if (consume(">="))
        lhs = (lhs >= parse_shift()) ? 1 : 0;
      else if (starts_with("<") && !starts_with("<<")) {
        consume("<");
        lhs = (lhs < parse_shift()) ? 1 : 0;
      } else if (starts_with(">") && !starts_with(">>")) {
        consume(">");
        lhs = (lhs > parse_shift()) ? 1 : 0;
      } else
        break;
    }
    return lhs;
  }

  fn parse_shift() throws -> i64
  {
    let lhs = parse_additive();
    for (;;) {
      if (consume("<<"))
        lhs = arithmetic_shift_left(lhs, parse_additive());
      else if (consume(">>"))
        lhs = arithmetic_shift_right(lhs, parse_additive());
      else
        break;
    }
    return lhs;
  }

  fn parse_additive() throws -> i64
  {
    let lhs = parse_multiplicative();
    for (;;) {
      if (consume("+"))
        lhs = arithmetic_add(lhs, parse_multiplicative());
      else if (consume("-"))
        lhs = arithmetic_subtract(lhs, parse_multiplicative());
      else
        break;
    }
    return lhs;
  }

  fn parse_multiplicative() throws -> i64
  {
    let lhs = parse_unary();
    for (;;) {
      if (consume("*"))
        lhs = arithmetic_multiply(lhs, parse_unary());
      else if (consume("/")) {
        let const divisor = parse_unary();
        if (divisor == 0) fail("division by zero");
        lhs = arithmetic_divide(lhs, divisor);
      } else if (consume("%")) {
        let const divisor = parse_unary();
        if (divisor == 0) fail("division by zero");
        lhs = arithmetic_modulo(lhs, divisor);
      } else
        break;
    }
    return lhs;
  }

  fn parse_unary() throws -> i64
  {
    if (consume("!")) return parse_unary() == 0 ? 1 : 0;
    if (consume("~")) return ~parse_unary();
    if (consume("-")) return arithmetic_subtract(0, parse_unary());
    if (consume("+")) return parse_unary();
    return parse_primary();
  }

  fn parse_primary() throws -> i64
  {
    depth++;
    defer { depth--; };
    if (depth > MAX_DEPTH) fail("expression nested too deeply");

    skip_spaces();
    if (consume("(")) {
      let const value = parse_assignment();
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
         the whole evaluation, so a view into it avoids a per-read allocation.
       */
      let const name_start = pos;
      while (pos < source.length && lexer::is_variable_name(source[pos]))
        pos++;
      return read_variable_value(
          source.substring_of_length(name_start, pos - name_start));
    }
    fail("unexpected character");
  }
};

} /* namespace */

fn EvalContext::evaluate_arithmetic(StringView expression) throws -> i64
{
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
  let const scratch = scratch_allocator();

  /* Only copy the segments when a leading tilde must be rewritten. The common
     word has no tilde and reads its segments in place. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{heap_allocator()};
  if (!word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front());
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
      let const value = String{heap_allocator(),
                               apply_parameter_expansion(segment.text.view())};
      if (segment.is_in_double_quotes)
        append_run(value, false);
      else
        /* An unquoted expansion undergoes field splitting and then pathname
           expansion, so a * or ? from the value is an active glob. */
        append_split_run(value, true);
    } break;

    case WordSegment::Kind::CommandSubstitution: {
      let const output =
          String{heap_allocator(), capture_command_substitution(segment)};
      if (segment.is_in_double_quotes)
        append_run(output, false);
      else
        append_split_run(output, true);
    } break;

    case WordSegment::Kind::ArithmeticExpansion: {
      /* A constant arithmetic segment was folded at analyze time, so the result
         is read straight from the cache rather than re-parsed here. */
      let const result = segment.folded_arithmetic_result.has_value()
                             ? *segment.folded_arithmetic_result
                             : evaluate_arithmetic(segment.text.view());
      let const value = String{heap_allocator(), utils::int_to_text(result)};
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
  /* Only copy the segments when a leading tilde must be rewritten, so the
     common assignment reads its segments in place with no per-command copy. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{heap_allocator()};
  if (!word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front());
    segments = &tilde_expanded_segments;
  }

  let result = String{heap_allocator()};
  for (const WordSegment &segment : *segments) {
    let const segment_text = segment.text.view();
    if (segment.kind == WordSegment::Kind::VariableReference)
      result += apply_parameter_expansion(segment_text);
    else if (segment.kind == WordSegment::Kind::CommandSubstitution)
      result += capture_command_substitution(segment);
    else if (segment.kind == WordSegment::Kind::ArithmeticExpansion)
      result += utils::int_to_text(segment.folded_arithmetic_result.has_value()
                                       ? *segment.folded_arithmetic_result
                                       : evaluate_arithmetic(segment_text));
    else
      result += segment_text;
  }
  return result;
}

fn EvalContext::expand_case_pattern_masked(const Word &word,
                                           ArrayList<bool> &active_out) throws
    -> String
{
  /* Only copy the segments when a leading tilde must be rewritten, mirroring the
     assignment expansion the case word otherwise shares. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{heap_allocator()};
  if (!word.segments.is_empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.is_empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front());
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
    case WordSegment::Kind::UnquotedText:
      emit_run(segment_text, true);
      break;
    case WordSegment::Kind::VariableReference: {
      let const value =
          String{heap_allocator(), apply_parameter_expansion(segment_text)};
      emit_run(value.view(), !segment.is_in_double_quotes);
    } break;
    case WordSegment::Kind::CommandSubstitution: {
      let const output =
          String{heap_allocator(), capture_command_substitution(segment)};
      emit_run(output.view(), !segment.is_in_double_quotes);
    } break;
    case WordSegment::Kind::ArithmeticExpansion: {
      /* An arithmetic result is decimal digits and a sign, so it carries no glob
         metacharacter and stays inactive. */
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
  } catch (...) {}
}

fn EvalContext::capture_command_substitution(const String &source) throws
    -> String
{
  /* Parse the inner command into the active parse arena. It coexists with the
     outer tree and is reclaimed when the arena resets. */
  if (AST_ARENA == nullptr)
    throw Error{"Command substitution outside of a parse"};

  let parser = Parser{
      Lexer{String{source.view()}, *AST_ARENA}
  };
  let const ast = parser.construct_ast();
  ASSERT(ast != nullptr);

  return run_captured_substitution(ast);
}

fn EvalContext::capture_command_substitution(const WordSegment &segment) throws
    -> String
{
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
    let parser = Parser{
        Lexer{String{segment.text.view()}, *AST_ARENA}
    };
    segment.cached_substitution_ast = parser.construct_ast();
    segment.cached_substitution_generation = generation;
  }
  ASSERT(segment.cached_substitution_ast != nullptr);

  return run_captured_substitution(segment.cached_substitution_ast);
}

fn EvalContext::run_captured_substitution(const Expression *ast) throws
    -> String
{
  ASSERT(ast != nullptr);

  /* A cd or an assignment inside the substitution must not leak. */
  let snapshot = snapshot_state();

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
  leave_subshell();

  m_shell_is_interactive = was_interactive;

  shit::flush();
  os::restore_stdout(saved);
  os::close_fd(pipe->out);
  os::join_thread(*reader);
  os::close_fd(pipe->in);
  restore_state(steal(snapshot));

  if (error) std::rethrow_exception(error);

  while (!captured.is_empty() && captured.back() == '\n')
    captured.pop_back();
  return captured;
}

fn EvalContext::run_source(StringView source, StringView origin,
                           bool consume_return, Maybe<SourceLocation> call_site,
                           Maybe<StringView> filename) throws -> i32
{
  /* Parse into the active arena, coexisting with the outer tree, the same way a
     command substitution does. The control-flow exceptions are not caught here,
     so a return or a break inside the evaluated source reaches the caller. */
  if (AST_ARENA == nullptr) throw Error{"Cannot run source outside of a parse"};

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
      parent_source
  });
  defer { m_source_frames.pop_back(); };

  /* The whole chain from the innermost source out to the outermost is printed
     when an error is caught, so every nested call site is named, not only the
     one running now. A frame whose parent source is known renders a caret at
     its call site, otherwise it falls back to naming the origin. */
  let const print_backtrace = [this]() {
    for (usize i = m_source_frames.count(); i > 0; i--) {
      const source_frame &frame = m_source_frames[i - 1];
      if (frame.parent_source != nullptr) {
        /* A frame is context under the primary error, not an error of its own,
           so it prints with the Trace severity rather than Error. */
        let const sourced_here = TraceWithLocation{frame.call_site};
        show_message(sourced_here.to_string(*frame.parent_source));
      } else {
        /* The origin line is context under the primary error, so it carries
           the note severity word rather than printing bare. */
        show_message(Note{"This error was raised while running " + frame.origin}
                         .to_string());
      }
    }
  };

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
        Lexer{String{source}, *AST_ARENA, false, stable_filename}
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
    print_backtrace();
    return 1;
  } catch (const ErrorWithLocation &e) {
    show_message(e.to_string(source));
    print_backtrace();
    return 1;
  } catch (const Error &e) {
    show_message(e.to_string());
    print_backtrace();
    return 1;
  }
}

fn EvalContext::clear_retained_sources() wontthrow -> void
{
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
  /* A heredoc body keeps its quote characters literally. */
  return expand_modifier_word(body, false);
}

hot fn EvalContext::process_args(const ArrayList<const Token *> &args) throws
    -> ArrayList<String>
{
  /* The expansion fields live on the scratch arena only until the heap argument
     vector is built, so the arena is released back to here on return. The mark
     nests, so a command substitution inside one of these words reclaims only
     its own fields and leaves this word's in-progress fields alone. */
  let const scratch_mark = m_scratch_arena.mark();
  defer { m_scratch_arena.release(scratch_mark); };

  let expanded_args = ArrayList<String>{};
  expanded_args.reserve(args.count());

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
        /* An assignment that appears as an argument, like echo k=$v, is an
           ordinary word. Rebuild it as the literal key, an equals sign, and the
           value segments, so the value still expands instead of staying
           literal. */
        let const a = static_cast<const tokens::Assignment *>(t);
        ASSERT(a != nullptr);
        let key_literal = String{StringView{a->key()}};
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
      let const plain_kind = word->plain_literal_kind();
      let took_fast_path = false;
      if (plain_kind != Word::PlainLiteral::NotPlain) {
        let literal = String{heap_allocator()};
        for (const WordSegment &segment : word->segments)
          literal.append(segment.text.view());

        /* A single unquoted segment still needs the IFS check, since an IFS
           byte in its text would split it into more than one field. With no IFS
           byte it is one field. */
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
        for (glob_field &field : expand_word(*word)) {
          for (String &g : expand_path(steal(field), l))
            expanded_args.push(String{
                heap_allocator(), StringView{g.c_str(), g.count()}
            });
        }
      }
    } catch (const Error &e) {
      throw ErrorWithLocation{l, e.message()};
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
    /* TODO: Sanitize extensions here too. */
    p = utils::canonicalize_path(program.view());
  }

  /* Builtins take precedence over programs. */
  ResolvedCommand kind;
  if (!bk) {
    if (p.has_value()) {
      kind = ResolvedCommand::from_program(steal(*p));
    } else {
      throw CommandNotFound{location, "Program '" + program + "' wasn't found"};
    }
  } else {
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

} /* namespace shit */
