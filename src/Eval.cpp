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
#include <thread>

namespace shit {

EvalContext::EvalContext(bool should_disable_path_expansion, bool should_echo,
                         bool should_echo_expanded, bool shell_is_interactive,
                         bool should_error_exit, String shell_name,
                         ArrayList<String> positional_params)
    : m_shell_name(std::move(shell_name)),
      m_positional_params(std::move(positional_params)),
      m_enable_path_expansion(!should_disable_path_expansion),
      m_enable_echo(should_echo), m_enable_echo_expanded(should_echo_expanded),
      m_shell_is_interactive(shell_is_interactive),
      m_error_exit(should_error_exit)
{
  /* Seed the separator table from the default IFS, since the table starts
     empty while m_field_separators is initialized to whitespace. */
  set_field_separators(m_field_separators.view());
}

fn EvalContext::add_evaluated_expression() -> void
{
  m_expressions_executed_last++;
}

fn EvalContext::add_expansion() -> void { m_expansions_last++; }

fn EvalContext::end_command() -> void
{
  m_expansions_total += m_expansions_last;
  m_expressions_executed_total += m_expressions_executed_last;

  m_expansions_last = m_expressions_executed_last = 0;
}

fn EvalContext::assign_variable(StringView name, StringView value) -> void
{
  /* The field separators are read once per expanded word, so the live value is
     cached here to keep that path off the map and the environment. */
  if (name == "IFS") set_field_separators(value);
  m_shell_variables.set(name, value);
}

fn EvalContext::set_field_separators(StringView value) -> void
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

fn EvalContext::is_field_separator(char c) const -> bool
{
  return m_field_separator_table[static_cast<u8>(c)];
}

fn EvalContext::set_shell_variable(StringView name, StringView value) -> void
{
  /* A read-only variable rejects the assignment. The common case has no
     read-only names, so the scan is skipped entirely. */
  if (is_readonly(name))
    throw Error{"'" + name + "' is read only and cannot be assigned"};

  assign_variable(name, value);
}

fn EvalContext::unset_shell_variable(StringView name) -> void
{
  m_shell_variables.erase(name);
  /* An exported variable also lives in the process environment, so it is
     removed there too. Otherwise a later lookup falls back to the stale
     environment value and the variable appears still set, which dash does not
     do. */
  os::unset_environment_variable(name);
  if (name == "IFS") set_field_separators(" \t\n");
}

fn EvalContext::get_variable_value(StringView name) const -> Maybe<String>
{
  if (name == "?")
    return String{heap_allocator(),
                  utils::integer_to_string(m_last_exit_status)};
  if (name == "$")
    return String{heap_allocator(),
                  utils::integer_to_string(os::get_shell_process_id())};
  if (name == "!")
    return m_last_background_pid
               ? String{heap_allocator(),
                        utils::integer_to_string(*m_last_background_pid)}
               : String{};
  if (name == "-") return String{heap_allocator(), option_flags_string()};
  if (name == "#")
    return String{heap_allocator(), utils::unsigned_integer_to_string(
                                        m_positional_params.size())};
  if (name == "0") return String{heap_allocator(), m_shell_name};

  /* A purely numeric name selects a positional parameter, $1 upward. An index
     too large to fit, or beyond the count, has no value. */
  let is_all_digits = !name.empty();
  for (usize i = 0; i < name.size(); i++)
    if (std::isdigit(static_cast<unsigned char>(name[i])) == 0) {
      is_all_digits = false;
      break;
    }
  if (is_all_digits) {
    if (name.size() > 9) return String{};
    let const parsed_index = utils::parse_decimal_integer(name);
    if (parsed_index.is_error()) return String{};
    let const index = static_cast<usize>(parsed_index.value());
    if (index >= 1 && index <= m_positional_params.size()) {
      ASSERT(index - 1 < m_positional_params.size());
      return m_positional_params[index - 1];
    }
    return String{};
  }

  /* $* and $@ outside the special quoted handling join into a single word. $*
     joins with the first IFS character, $@ joins with a space. */
  if (name == "*" || name == "@") {
    let separator = ' ';
    let has_separator = true;
    if (name == "*") {
      let const &ifs = m_field_separators;
      has_separator = !ifs.empty();
      if (has_separator) separator = ifs.first_character();
    }
    let joined = String{};
    for (usize i = 0; i < m_positional_params.size(); i++) {
      if (i > 0 && has_separator) joined.push(separator);
      joined.append(m_positional_params[i].view());
    }
    return joined;
  }

  if (let const *stored = m_shell_variables.find(name)) return *stored;

  if (let const env = os::get_environment_variable(name))
    return String{heap_allocator(), env->view()};
  return shit::None;
}

fn EvalContext::positional_params() const -> const ArrayList<String> &
{
  return m_positional_params;
}

fn EvalContext::set_positional_params(ArrayList<String> params) -> void
{
  m_positional_params = std::move(params);
}

fn EvalContext::set_last_background_pid(i64 pid) -> void
{
  m_last_background_pid = pid;
}

fn EvalContext::register_job(os::process pid, StringView command) -> int
{
  let job = Job{};
  job.id = m_next_job_id++;
  job.pid = pid;
  job.command = command;
  job.state = Job::State::Running;
  m_jobs.push(std::move(job));
  ASSERT(!m_jobs.empty());
  LOG(Verbosity::Debug, "registered job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn EvalContext::update_jobs() -> void
{
  for (Job &job : m_jobs) {
    if (job.state == Job::State::Done) continue;

    i32 status = 0;
    let const state = os::poll_process(job.pid, status);
    if (state == os::ProcessState::Exited) {
      job.state = Job::State::Done;
      job.last_status = status;
    } else if (state == os::ProcessState::Stopped) {
      job.state = Job::State::Stopped;
    } else {
      job.state = Job::State::Running;
    }
  }
}

fn EvalContext::jobs() -> ArrayList<Job> & { return m_jobs; }

fn EvalContext::find_job(int id) -> Job *
{
  for (Job &job : m_jobs)
    if (job.id == id) return &job;
  return nullptr;
}

fn EvalContext::most_recent_job() -> Job *
{
  /* Skip a finished job, so a bare fg or bg acts on a job that is still
     running or stopped rather than a dead pid. */
  for (usize i = m_jobs.size(); i > 0; i--) {
    ASSERT(i - 1 < m_jobs.size());
    if (m_jobs[i - 1].state != Job::State::Done) return &m_jobs[i - 1];
  }
  return nullptr;
}

fn EvalContext::forget_done_jobs() -> void
{
  let kept = ArrayList<Job>{};
  for (Job &job : m_jobs) {
    if (job.state == Job::State::Done) continue;
    kept.push(std::move(job));
  }
  m_jobs = std::move(kept);
}

fn EvalContext::set_monitor(bool enabled) -> void { m_monitor = enabled; }

fn EvalContext::monitor() const -> bool { return m_monitor; }

fn EvalContext::register_function(StringView name, const Expression *body)
    -> void
{
  m_functions.set(name, body);
}

fn EvalContext::find_function(StringView name) const -> const Expression *
{
  if (let const *const *slot = m_functions.find(name)) return *slot;
  return nullptr;
}

fn EvalContext::has_functions() const -> bool
{
  return m_functions.size() != 0;
}

fn EvalContext::unset_function(StringView name) -> void
{
  m_functions.erase(name);
}

fn EvalContext::function_names() const -> HashSet
{
  let names = HashSet{heap_allocator()};
  m_functions.for_each([&](StringView name, const Expression *body) {
    unused(body);
    names.add(name);
  });
  return names;
}

fn EvalContext::set_trap(StringView condition, StringView action) -> void
{
  m_traps.set(condition, action);
}

fn EvalContext::remove_trap(StringView condition) -> void
{
  m_traps.erase(condition);
}

fn EvalContext::traps() const -> const HashMap<String> & { return m_traps; }

fn EvalContext::run_exit_trap() -> void
{
  if (m_exit_trap_ran) return;
  m_exit_trap_ran = true;

  /* A Ctrl-C that ended the last command leaves the interrupt flag set. The exit
     trap runs as the shell winds down and must not be aborted by it, so the flag
     is dropped before the action evaluates. */
  os::INTERRUPT_REQUESTED = 0;

  if (let const *action = m_traps.find(StringView{"EXIT", 4}))
    if (action->size() > 0) run_source(action->view(), "the EXIT trap");
}

fn EvalContext::mark_readonly(StringView name) -> void
{
  if (is_readonly(name)) return;
  m_readonly_names.push(String{heap_allocator(), name});
}

fn EvalContext::is_readonly(StringView name) const -> bool
{
  if (m_readonly_names.size() == 0) return false;
  for (const String &readonly_name : m_readonly_names)
    if (StringView{readonly_name.c_str(), readonly_name.size()} == name)
      return true;
  return false;
}

fn EvalContext::readonly_names() const -> ArrayList<String>
{
  let out = ArrayList<String>{};
  for (const String &name : m_readonly_names)
    out.push(String{
        heap_allocator(), StringView{name.c_str(), name.size()}
    });
  std::sort(out.begin(), out.end());
  return out;
}

fn EvalContext::enter_function_scope() -> void
{
  m_local_scopes.push(ArrayList<LocalBinding>{});
}

fn EvalContext::leave_function_scope() -> void
{
  if (m_local_scopes.empty()) return;

  /* Restore each shadowed binding in reverse, so a name declared local twice
     ends with the value it held before the function ran. */
  ASSERT(!m_local_scopes.empty());
  let &scope = m_local_scopes.back();
  for (usize i = scope.size(); i > 0; i--) {
    ASSERT(i - 1 < scope.size());
    let &binding = scope[i - 1];
    /* Restore through assign_variable, not set_shell_variable, since this runs
       inside a noexcept defer and a readonly name would otherwise throw from a
       destructor and terminate the shell. A local marked readonly in the body
       is being torn down here, so the restore of the outer value is valid. */
    if (binding.previous_value.has_value())
      assign_variable(binding.name, *binding.previous_value);
    else
      unset_shell_variable(binding.name);
  }
  let kept = ArrayList<ArrayList<LocalBinding>>{};
  for (usize i = 0; i + 1 < m_local_scopes.size(); i++)
    kept.push(std::move(m_local_scopes[i]));
  m_local_scopes = std::move(kept);
}

fn EvalContext::in_function_scope() const -> bool
{
  return !m_local_scopes.empty();
}

fn EvalContext::declare_local(StringView name) -> void
{
  if (m_local_scopes.empty()) return;
  ASSERT(!m_local_scopes.empty());
  m_local_scopes.back().push(
      LocalBinding{String{name}, get_variable_value(name)});
}

fn EvalContext::set_alias(StringView name, StringView value) -> void
{
  m_aliases.set(name, value);
}

fn EvalContext::remove_alias(StringView name) -> bool
{
  if (m_aliases.find(name) == nullptr) return false;
  m_aliases.erase(name);
  return true;
}

fn EvalContext::get_alias(StringView name) const -> Maybe<String>
{
  if (let const *value = m_aliases.find(name))
    return String{heap_allocator(), value->view()};
  return None;
}

fn EvalContext::alias_definitions() const -> ArrayList<String>
{
  let out = ArrayList<String>{};
  m_aliases.for_each([&out](StringView key, const String &value) {
    let definition = String{heap_allocator(), key};
    definition.append(StringView{"='", 2});
    definition.append(StringView{value.c_str(), value.size()});
    definition.push('\'');
    out.push(std::move(definition));
  });
  std::sort(out.begin(), out.end());
  return out;
}

fn EvalContext::alias_names() const -> HashSet
{
  let out = HashSet{heap_allocator()};
  m_aliases.for_each([&out](StringView key, const String &value) {
    unused(value);
    out.add(key);
  });
  return out;
}

fn EvalContext::enter_subshell() -> void { m_subshell_depth++; }

fn EvalContext::leave_subshell() -> void
{
  ASSERT(m_subshell_depth > 0);
  m_subshell_depth--;
}

fn EvalContext::in_subshell() const -> bool { return m_subshell_depth > 0; }

fn EvalContext::request_break(i64 level, SourceLocation location) -> void
{
  LOG(Verbosity::Debug, "break requested, level %lld", (long long) level);
  m_control_flow = ControlFlow{ControlFlow::Kind::Break, level, location,
                               m_current_source, String{m_current_origin}};
}

fn EvalContext::request_continue(i64 level, SourceLocation location) -> void
{
  LOG(Verbosity::Debug, "continue requested, level %lld",
           (long long) level);
  m_control_flow = ControlFlow{ControlFlow::Kind::Continue, level, location,
                               m_current_source, String{m_current_origin}};
}

fn EvalContext::request_return(i64 status, SourceLocation location) -> void
{
  LOG(Verbosity::Debug, "return requested, status %lld",
           (long long) status);
  m_control_flow = ControlFlow{ControlFlow::Kind::Return, status, location,
                               m_current_source, String{m_current_origin}};
}

fn EvalContext::request_exit(i64 status, SourceLocation location) -> void
{
  LOG(Verbosity::Debug, "exit requested, status %lld", (long long) status);
  m_control_flow = ControlFlow{ControlFlow::Kind::Exit, status, location,
                               m_current_source, String{m_current_origin}};
}

fn EvalContext::has_pending_control_flow() const -> bool
{
  return m_control_flow.kind != ControlFlow::Kind::Normal;
}

fn EvalContext::pending_control_flow() -> ControlFlow &
{
  return m_control_flow;
}

fn EvalContext::pending_control_flow() const -> const ControlFlow &
{
  return m_control_flow;
}

fn EvalContext::clear_control_flow() -> void
{
  m_control_flow.kind = ControlFlow::Kind::Normal;
}

fn EvalContext::set_current_source(const String *source, String origin) -> void
{
  m_current_source = source;
  m_current_origin = std::move(origin);
}

fn EvalContext::current_source() const -> const String *
{
  return m_current_source;
}

fn EvalContext::current_origin() const -> const String &
{
  return m_current_origin;
}

fn EvalContext::set_error_exit(bool enabled) -> void { m_error_exit = enabled; }

fn EvalContext::error_exit() const -> bool { return m_error_exit; }

fn EvalContext::set_echo_expanded(bool enabled) -> void
{
  m_enable_echo_expanded = enabled;
}

fn EvalContext::set_error_unset(bool enabled) -> void
{
  m_error_unset = enabled;
}

fn EvalContext::error_unset() const -> bool { return m_error_unset; }

fn EvalContext::set_no_clobber(bool enabled) -> void { m_no_clobber = enabled; }

fn EvalContext::no_clobber() const -> bool { return m_no_clobber; }

fn EvalContext::set_export_all(bool enabled) -> void { m_export_all = enabled; }

fn EvalContext::export_all() const -> bool { return m_export_all; }

fn EvalContext::set_no_glob(bool enabled) -> void
{
  m_enable_path_expansion = !enabled;
}

fn EvalContext::no_glob() const -> bool { return !m_enable_path_expansion; }

fn EvalContext::set_no_exec(bool enabled) -> void { m_no_exec = enabled; }

fn EvalContext::no_exec() const -> bool { return m_no_exec; }

fn EvalContext::enter_condition() -> void { m_condition_depth++; }

fn EvalContext::leave_condition() -> void
{
  ASSERT(m_condition_depth > 0);
  m_condition_depth--;
}

fn EvalContext::in_condition() const -> bool { return m_condition_depth > 0; }

fn EvalContext::getopts_char_index() const -> usize
{
  return m_getopts_char_index;
}

fn EvalContext::set_getopts_char_index(usize index) -> void
{
  m_getopts_char_index = index;
}

fn EvalContext::getopts_last_optind() const -> i64
{
  return m_getopts_last_optind;
}

fn EvalContext::set_getopts_last_optind(i64 optind) -> void
{
  m_getopts_last_optind = optind;
}

fn EvalContext::sorted_variable_assignments() const -> ArrayList<String>
{
  let assignments = ArrayList<String>{};
  assignments.reserve(m_shell_variables.size());
  m_shell_variables.for_each([&](StringView name, const String &value) {
    let entry = String{heap_allocator(), name};
    entry.push('=');
    entry.append(StringView{value.c_str(), value.size()});
    assignments.push(std::move(entry));
  });
  std::sort(assignments.begin(), assignments.end());
  return assignments;
}

fn EvalContext::clear_functions() -> void { m_functions.clear(); }

fn EvalContext::snapshot_state() const -> EvalStateSnapshot
{
  return EvalStateSnapshot{m_shell_variables, m_functions, m_positional_params,
                           Path::current_directory()};
}

fn EvalContext::restore_state(EvalStateSnapshot snapshot) -> void
{
  m_shell_variables = std::move(snapshot.shell_variables);
  m_functions = std::move(snapshot.functions);
  m_positional_params = std::move(snapshot.positional_params);
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

  /* The exit status is intentionally not restored. A subshell and a command
     substitution propagate the status of their last command to the parent. */
}

fn EvalContext::option_flags_string() const -> String
{
  let flags = String{};
  if (m_error_exit) flags += 'e';
  if (!m_enable_path_expansion) flags += 'f';
  if (m_enable_echo) flags += 'v';
  if (m_enable_echo_expanded) flags += 'x';
  if (m_shell_is_interactive) flags += 'i';
  return flags;
}

fn EvalContext::set_last_exit_status(i32 status) -> void
{
  m_last_exit_status = status;
}

fn EvalContext::last_exit_status() const -> i32 { return m_last_exit_status; }

fn EvalContext::expand_variable(StringView name) const -> String
{
  return get_variable_value(name).value_or(String{});
}

namespace {

/* Remove the shortest or longest prefix of value that matches pattern as a
   glob, returning the remainder. */
fn trim_matching_prefix(StringView value, StringView pattern, bool longest)
    -> String
{
  let active = ArrayList<bool>{heap_allocator()};
  for (usize k = 0; k < pattern.length; k++)
    active.push(true);
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
  return String{heap_allocator(), value};
}

/* Remove the shortest or longest suffix of value that matches pattern as a
   glob, returning the head. */
fn trim_matching_suffix(StringView value, StringView pattern, bool longest)
    -> String
{
  let active = ArrayList<bool>{heap_allocator()};
  for (usize k = 0; k < pattern.length; k++)
    active.push(true);
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
  return String{heap_allocator(), value};
}

} /* namespace */

fn EvalContext::expand_modifier_word(StringView word, bool remove_quotes)
    -> String
{
  let out = String{heap_allocator()};
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
      out += word[i];
      continue;
    }

    if (word[i] != '$') {
      out += word[i];
      continue;
    }
    if (i + 1 >= word.length) {
      out += '$';
      break;
    }

    const char next = word[i + 1];
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
      out += apply_parameter_expansion(inner);
      i = j;
    } else if (lexer::is_variable_name_start(next)) {
      String name{heap_allocator()};
      usize j = i + 1;
      while (j < word.length && lexer::is_variable_name(word[j]))
        name += word[j++];
      out += expand_variable(name);
      i = j - 1;
    } else if (next == '(' && i + 2 < word.length && word[i + 2] == '(') {
      /* Arithmetic $((...)), scanned to the matching )). */
      String inner{heap_allocator()};
      usize j = i + 3;
      usize depth = 0;
      for (; j < word.length; j++) {
        if (word[j] == '(') {
          depth++;
        } else if (word[j] == ')' && depth > 0) {
          depth--;
        } else if (word[j] == ')' && j + 1 < word.length && word[j + 1] == ')')
        {
          j += 2;
          break;
        }
        inner += word[j];
      }
      out += utils::integer_to_string(evaluate_arithmetic(inner));
      i = j - 1;
    } else if (next == '(') {
      /* Command substitution $(...), scanned to the matching ). */
      String inner{heap_allocator()};
      usize j = i + 2;
      usize depth = 1;
      for (; j < word.length; j++) {
        if (word[j] == '(') {
          depth++;
        } else if (word[j] == ')') {
          depth--;
          if (depth == 0) break;
        }
        inner += word[j];
      }
      out += capture_command_substitution(inner);
      i = j;
    } else if (next == '?' || next == '@' || next == '*' || next == '#' ||
               next == '$' || next == '!' || next == '-' ||
               lexer::is_number(next))
    {
      out += expand_variable(StringView{&next, 1});
      i++;
    } else {
      out += '$';
    }
  }
  return out;
}

fn EvalContext::apply_parameter_expansion(StringView spec) -> String
{
  if (spec.empty()) return String{heap_allocator()};

  /* ${#name} is the length of the value, distinct from $# which is the count of
     positional parameters. */
  if (spec.length > 1 && spec[0] == '#') {
    let const name = spec.substring(1);
    if (name == "@" || name == "*")
      return String{heap_allocator(), utils::unsigned_integer_to_string(
                                          m_positional_params.size())};
    let const value = get_variable_value(name);
    if (m_error_unset && !value.has_value())
      throw Error{name + ": parameter not set"};
    return String{heap_allocator(), utils::unsigned_integer_to_string(
                                        value.value_or(String{}).length())};
  }

  /* Split the parameter name from an optional operator and its word. */
  ASSERT(!spec.empty());
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
  if (rest.empty()) {
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
  let const is_doubled =
      (op_index + 1 < rest.length && rest[op_index + 1] == op &&
       (op == '#' || op == '%'));
  let const word = rest.substring(op_index + (is_doubled ? 2 : 1));

  let const current = get_variable_value(name);
  let const is_set = current.has_value();
  let const is_empty = !is_set || current->empty();
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
      if (word.empty()) throw Error{name + ": parameter not set or empty"};
      throw Error{expand_modifier_word(word)};
    }
    ASSERT(current.has_value());
    return String{heap_allocator(), current->view()};

  case '#': {
    let const value = current.value_or(String{});
    return trim_matching_prefix(value.view(), expand_modifier_word(word),
                                is_doubled);
  }

  case '%': {
    let const value = current.value_or(String{});
    return trim_matching_suffix(value.view(), expand_modifier_word(word),
                                is_doubled);
  }

  default: return expand_variable(name);
  }
}

fn EvalContext::make_stats_string() const -> String
{
  let s = String{};

  s += "[Stats\n";

  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Expansions: " +
       utils::unsigned_integer_to_string(last_expansion_count());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Nodes evaluated: " +
       utils::unsigned_integer_to_string(last_expressions_executed());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total expansions: " +
       utils::unsigned_integer_to_string(total_expansion_count());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total nodes evaluated: " +
       utils::unsigned_integer_to_string(total_expressions_executed());
  s += '\n';

  s += "]";

  return s;
}

fn EvalContext::should_echo() const -> bool { return m_enable_echo; }

fn EvalContext::should_echo_expanded() const -> bool
{
  return m_enable_echo_expanded;
}

fn EvalContext::shell_is_interactive() const -> bool
{
  return m_shell_is_interactive;
}

fn EvalContext::last_expressions_executed() const -> usize
{
  return m_expressions_executed_last;
}

fn EvalContext::total_expressions_executed() const -> usize
{
  return m_expressions_executed_total + m_expressions_executed_last;
}

fn EvalContext::last_expansion_count() const -> usize { return m_expansions_last; }

fn EvalContext::total_expansion_count() const -> usize
{
  return m_expansions_total + m_expansions_last;
}

/* TODO: Test symlinks. */
/* TODO: What the fuck is happening. */
fn EvalContext::expand_path_once(const GlobField &field,
                                 bool should_expand_files)
    -> ArrayList<GlobField>
{
  let const scratch = scratch_allocator();
  let expanded = ArrayList<GlobField>{scratch};

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
  const usize stem_start = has_slashes ? *last_slash + 1 : 0;
  let const has_glob = stem_start < path.length;
  let glob = StringView{};
  if (has_glob) glob = path.substring(stem_start);

  let const entries = Path::read_directory(parent_dir);
  if (!entries.has_value())
    throw Error{"Could not descend into '" + parent_dir.text() +
                "': " + os::last_system_error_message()};

  if (!has_glob) {
    let copy = GlobField{scratch};
    copy.text.append(field.text.view());
    copy.glob_active = field.glob_active;
    expanded.push(std::move(copy));
    return expanded;
  }

  let const parent_is_dot = parent_dir.text() == StringView{"."};

  /* The no-glob field returned above, so the stem is a non-empty glob here and
     glob[0] reads a real byte. */
  ASSERT(has_glob);
  ASSERT(!glob.empty());

  for (const String &entry_name : *entries) {
    let const filename = entry_name.view();

    /* The full path joins the parent and the filename, the way the directory
       walk needs it for the is_directory test and the result text. */
    let full_path = parent_dir;
    full_path.push_component(filename);

    if (!should_expand_files && !full_path.is_directory()) continue;

    /* TODO: Figure the rules of hidden file expansion. */
    if (glob[0] != '.' && !filename.empty() && filename[0] == '.') continue;

    if (utils::glob_matches(glob, filename, field.glob_active, stem_start)) {
      add_expansion();

      /* A real filename is literal, so the resulting field never globs again.
         The empty mask is the all-literal convention, so it carries no
         per-result allocation. A parent of "." is dropped to keep the result
         relative. */
      let result_field = GlobField{scratch};
      if (parent_is_dot)
        result_field.text.append(filename);
      else
        result_field.text.append(full_path.text().view());
      expanded.push(std::move(result_field));
    }
  }

  return expanded;
}

namespace {

/* The index of the first active metacharacter that actually forms a glob. A '['
   without a later ']' is a literal bracket, not a glob, so a field such as the
   command word '[' needs no directory scan at all. Returns nullopt when the
   field is all literal. */
fn first_active_glob(StringView text, const ArrayList<bool> &mask)
    -> Maybe<usize>
{
  let open_bracket = Maybe<usize>{};
  for (usize i = 0; i < mask.size(); i++) {
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

fn EvalContext::expand_path_recurse(ArrayList<GlobField> fields)
    -> ArrayList<GlobField>
{
  let const scratch = scratch_allocator();
  let result = ArrayList<GlobField>{scratch};

  for (GlobField &field : fields) {
    let const text = field.text.view();

    /* An empty mask is the all-literal convention, so a field without one holds
       no live glob metacharacter. */
    let const expand_ch = first_active_glob(text, field.glob_active);

    if (!expand_ch) {
      /* No glob remains. This field is a literal suffix appended after an
         earlier glob, so keep it only when it actually exists. A path produced
         purely by globbing came from a directory read and always exists, so it
         never reaches here and pays no stat. */
      if (Path{field.text.view()}.exists()) result.push(std::move(field));
      continue;
    }

    /* An active glob index came from the mask, so it points inside the text and
       the field carries a mask parallel to the text. */
    ASSERT(*expand_ch < text.length);
    ASSERT(field.glob_active.size() == text.length);

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
      for (GlobField &f : once)
        result.push(std::move(f));
      continue;
    }

    /* Split off the first globbed directory component and the literal-or-glob
       suffix after it, building each from a substring rather than copying the
       whole field. */
    let const slash_offset = static_cast<std::ptrdiff_t>(*slash_after);
    let operating = GlobField{scratch};
    operating.text.append(StringView{text.data, *slash_after});
    for (std::ptrdiff_t k = 0; k < slash_offset; k++)
      operating.glob_active.push(field.glob_active[static_cast<usize>(k)]);
    let removed_suffix = GlobField{scratch};
    removed_suffix.text.append(
        StringView{text.data + *slash_after, text.length - *slash_after});
    for (usize k = static_cast<usize>(slash_offset);
         k < field.glob_active.size(); k++)
      removed_suffix.glob_active.push(field.glob_active[k]);

    let once = expand_path_once(operating, false);

    /* Bring back the removed suffix and recurse on the expanded entries. Each
       match came back all-literal with an empty mask, so restore its false
       entries before the suffix mask to keep the mask aligned with the text. */
    for (GlobField &f : once) {
      let const matched_length = f.text.size();
      f.text.append(removed_suffix.text.view());
      f.glob_active.clear();
      for (usize k = 0; k < matched_length; k++)
        f.glob_active.push(false);
      for (usize k = 0; k < removed_suffix.glob_active.size(); k++)
        f.glob_active.push(removed_suffix.glob_active[k]);
    }

    /* The recurse validates each level through the directory read or, for a
       literal suffix, the existence check above, so no extra stat is needed
       here. */
    let twice = expand_path_recurse(std::move(once));
    for (GlobField &f : twice)
      result.push(std::move(f));
  }

  return result;
}

fn EvalContext::expand_tilde(WordSegment &leading_segment) const -> void
{
  /* A tilde only expands when it is unquoted. An escaped or quoted tilde is a
     literal segment and stays as is. */
  if (!leading_segment.is_tilde_candidate()) return;

  let &text = leading_segment.text;
  if (text.empty() || text[0] != '~') return;

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
  text = std::move(expanded);
}

fn EvalContext::expand_path(GlobField field) -> ArrayList<String>
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
    single.push(std::move(field.text));
    return single;
  }

  /* The pattern is kept so a glob that matches None falls back to it, since
     the field moves into the recurse. */
  let pattern = String{scratch};
  pattern.append(field.text.view());

  let input = ArrayList<GlobField>{scratch};
  input.push(std::move(field));
  let fields = expand_path_recurse(std::move(input));

  let values = ArrayList<String>{scratch};
  for (GlobField &f : fields)
    values.push(std::move(f.text));

  /* Sort the matches in byte order, which is the POSIX collating order in the C
     locale and what dash produces. A plain compare also keeps a large expansion
     from spending most of its time in the sort comparator. */
  std::sort(values.begin(), values.end());

  /* A pattern that matches no file expands to itself, the POSIX behavior dash
     follows, rather than being dropped or raising an error. */
  if (values.size() == 0) values.push(std::move(pattern));

  return values;
}

namespace {

/* The count of leading bytes that are digits in the given radix, so a value
   with trailing non-digit bytes reads only its numeric prefix the way base-0
   strtoll did. A hexadecimal scan accepts both letter cases. */
fn count_leading_digits(StringView text, u32 radix) -> usize
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
fn parse_arithmetic_operand(StringView text) -> i64
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

/* A recursive-descent evaluator for $((...)), following C operator precedence,
   that resolves and assigns shell variables through the context. */
struct ArithmeticParser
{
  EvalContext &context;
  StringView source;
  usize pos;

  [[noreturn]] fn fail(StringView message) -> void
  {
    throw Error{"Arithmetic: " + message};
  }

  fn skip_spaces() -> void
  {
    while (pos < source.length && (source[pos] == ' ' || source[pos] == '\t' ||
                                   source[pos] == '\n' || source[pos] == '\r'))
      pos++;
  }

  fn starts_with(StringView op) -> bool
  {
    skip_spaces();
    return pos + op.length <= source.length &&
           source.substring_of_length(pos, op.length) == op;
  }

  fn consume(StringView op) -> bool
  {
    if (!starts_with(op)) return false;
    pos += op.length;
    return true;
  }

  fn read_variable_value(StringView name) -> i64
  {
    /* A plain shell variable, the common operand, reads its digits straight
       from the stored value with no copy. The operand parser stops at the first
       non-digit and reads a non-numeric value as zero, which matches the old
       strtoll path. */
    if (let const *stored = context.lookup_shell_variable(name)) {
      if (stored->size() == 0) return 0;
      return parse_arithmetic_operand(stored->view());
    }

    let const value = context.get_variable_value(name).value_or(String{});
    if (value.empty()) return 0;
    return parse_arithmetic_operand(value.view());
  }

  fn parse() -> i64
  {
    let const result = parse_assignment();
    skip_spaces();
    if (pos != source.length) fail("unexpected trailing characters");
    return result;
  }

  fn apply_compound(i64 lhs, i64 rhs, char kind) -> i64
  {
    switch (kind) {
    case '+': return lhs + rhs;
    case '-': return lhs - rhs;
    case '*': return lhs * rhs;
    case '/':
      if (rhs == 0) fail("division by zero");
      return lhs / rhs;
    case '%':
      if (rhs == 0) fail("division by zero");
      return lhs % rhs;
    case '&': return lhs & rhs;
    case '|': return lhs | rhs;
    case '^': return lhs ^ rhs;
    case 'L': return lhs << rhs;
    case 'R': return lhs >> rhs;
    default: return rhs;
    }
  }

  fn parse_assignment() -> i64
  {
    /* An assignment has a bare variable name on the left, so try it and rewind
       when the name is not followed by an assignment operator. */
    let const save = pos;
    skip_spaces();
    if (pos < source.length && lexer::is_variable_name_start(source[pos])) {
      let name = String{};
      while (pos < source.length && lexer::is_variable_name(source[pos]))
        name += source[pos++];

      struct CompoundOperator
      {
        StringView token;
        u8 kind;
      };
      static const CompoundOperator compound_operators[] = {
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
          context.set_shell_variable(name, utils::integer_to_string(result));
          return result;
        }
      }
      if (starts_with("=") && !starts_with("==")) {
        consume("=");
        let const rhs = parse_assignment();
        context.set_shell_variable(name, utils::integer_to_string(rhs));
        return rhs;
      }
      pos = save;
    }
    return parse_ternary();
  }

  fn parse_ternary() -> i64
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

  fn parse_logical_or() -> i64
  {
    let lhs = parse_logical_and();
    while (consume("||"))
      lhs = (lhs != 0 || parse_logical_and() != 0) ? 1 : 0;
    return lhs;
  }

  fn parse_logical_and() -> i64
  {
    let lhs = parse_bitwise_or();
    while (consume("&&"))
      lhs = (lhs != 0 && parse_bitwise_or() != 0) ? 1 : 0;
    return lhs;
  }

  fn parse_bitwise_or() -> i64
  {
    let lhs = parse_bitwise_xor();
    while (starts_with("|") && !starts_with("||")) {
      consume("|");
      lhs |= parse_bitwise_xor();
    }
    return lhs;
  }

  fn parse_bitwise_xor() -> i64
  {
    let lhs = parse_bitwise_and();
    while (consume("^"))
      lhs ^= parse_bitwise_and();
    return lhs;
  }

  fn parse_bitwise_and() -> i64
  {
    let lhs = parse_equality();
    while (starts_with("&") && !starts_with("&&")) {
      consume("&");
      lhs &= parse_equality();
    }
    return lhs;
  }

  fn parse_equality() -> i64
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

  fn parse_relational() -> i64
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

  fn parse_shift() -> i64
  {
    let lhs = parse_additive();
    for (;;) {
      if (consume("<<"))
        lhs <<= parse_additive();
      else if (consume(">>"))
        lhs >>= parse_additive();
      else
        break;
    }
    return lhs;
  }

  fn parse_additive() -> i64
  {
    let lhs = parse_multiplicative();
    for (;;) {
      if (consume("+"))
        lhs += parse_multiplicative();
      else if (consume("-"))
        lhs -= parse_multiplicative();
      else
        break;
    }
    return lhs;
  }

  fn parse_multiplicative() -> i64
  {
    let lhs = parse_unary();
    for (;;) {
      if (consume("*"))
        lhs *= parse_unary();
      else if (consume("/")) {
        let const divisor = parse_unary();
        if (divisor == 0) fail("division by zero");
        lhs /= divisor;
      } else if (consume("%")) {
        let const divisor = parse_unary();
        if (divisor == 0) fail("division by zero");
        lhs %= divisor;
      } else
        break;
    }
    return lhs;
  }

  fn parse_unary() -> i64
  {
    if (consume("!")) return parse_unary() == 0 ? 1 : 0;
    if (consume("~")) return ~parse_unary();
    if (consume("-")) return -parse_unary();
    if (consume("+")) return parse_unary();
    return parse_primary();
  }

  fn parse_primary() -> i64
  {
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
      let name = String{};
      while (pos < source.length && lexer::is_variable_name(source[pos]))
        name += source[pos++];
      return read_variable_value(name);
    }
    fail("unexpected character");
  }
};

} /* namespace */

fn EvalContext::evaluate_arithmetic(StringView expression) -> i64
{
  /* Parameter expansion runs first, so a $1, a $x, or a ${...} inside the
     arithmetic becomes its value before the expression is parsed. A bare name
     is still resolved during evaluation. When the source holds no parameter to
     expand, which the d=$((d+1)) hot loop hits every iteration, the expansion
     copy is skipped and the original is parsed directly. */
  if (!expression.find_character('$').has_value() &&
      !expression.find_character('`').has_value())
  {
    let parser = ArithmeticParser{*this, expression, 0};
    return parser.parse();
  }

  /* The expanded word owns the bytes the parser views, so it outlives the
     parser below. */
  let const expanded_word = expand_modifier_word(expression);
  let parser = ArithmeticParser{*this, expanded_word.view(), 0};
  return parser.parse();
}

fn EvalContext::expand_word(const Word &word) -> ArrayList<GlobField>
{
  let const scratch = scratch_allocator();

  /* Only copy the segments when a leading tilde must be rewritten. The common
     word has no tilde and reads its segments in place. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{heap_allocator()};
  if (!word.segments.empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.empty() &&
      word.segments.front().text.first_character() == '~')
  {
    tilde_expanded_segments = word.segments;
    expand_tilde(tilde_expanded_segments.front());
    segments = &tilde_expanded_segments;
  }

  let fields = ArrayList<GlobField>{scratch};
  let current = GlobField{scratch};
  let has_current = false;

  auto flush = [&]() {
    if (has_current) {
      fields.push(std::move(current));
      current = GlobField{scratch};
      has_current = false;
    }
  };

  auto append_run = [&](StringView text, bool glob_active) {
    current.text.append(text);
    for (usize k = 0; k < text.length; k++)
      current.glob_active.push(glob_active);
    has_current = true;
  };

  /* A split run breaks into fields on every IFS run. Leading and trailing IFS
     leave no empty field behind, since flush only emits a started field. */
  auto append_split_run = [&](StringView text, bool glob_active) {
    usize i = 0;
    while (i < text.length) {
      if (is_field_separator(text.data[i])) {
        flush();
        while (i < text.length && is_field_separator(text.data[i]))
          i++;
        continue;
      }
      usize start = i;
      while (i < text.length && !is_field_separator(text.data[i]))
        i++;
      append_run(StringView{text.data + start, i - start}, glob_active);
    }
  };

  for (const WordSegment &segment : *segments) {
    let const segment_text = StringView{segment.text.data(), segment.text.size()};
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
        for (usize i = 0; i < m_positional_params.size(); i++) {
          if (i > 0) flush();
          append_run(StringView{m_positional_params[i].data(),
                                m_positional_params[i].size()},
                     false);
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
      let const output = String{heap_allocator(),
                                capture_command_substitution(segment.text)};
      if (segment.is_in_double_quotes)
        append_run(output, false);
      else
        append_split_run(output, true);
    } break;

    case WordSegment::Kind::ArithmeticExpansion: {
      let const value = String{
          heap_allocator(),
          utils::integer_to_string(evaluate_arithmetic(segment.text.view()))};
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

fn EvalContext::expand_word_for_assignment(const Word &word) -> String
{
  /* Only copy the segments when a leading tilde must be rewritten, so the
     common assignment reads its segments in place with no per-command copy. */
  let const *segments = &word.segments;
  let tilde_expanded_segments = ArrayList<WordSegment>{heap_allocator()};
  if (!word.segments.empty() && word.segments.front().is_tilde_candidate() &&
      !word.segments.front().text.empty() &&
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
      result += capture_command_substitution(segment.text);
    else if (segment.kind == WordSegment::Kind::ArithmeticExpansion)
      result += utils::integer_to_string(evaluate_arithmetic(segment_text));
    else
      result += segment_text;
  }
  return result;
}

fn EvalContext::capture_command_substitution(const String &source) -> String
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

  /* A cd or an assignment inside the substitution must not leak. */
  let snapshot = snapshot_state();

  let const pipe = os::make_pipe();
  if (!pipe) throw Error{"Could not open a pipe for command substitution"};

  /* Drain the read end on a thread so output larger than the pipe buffer cannot
     deadlock the commands writing into it. */
  let captured = String{heap_allocator()};
  let reader = std::thread([&captured, read_fd = pipe->in]() {
    /* A failed allocation here must not escape the thread and call terminate.
     */
    try {
      char buffer[4096];
      for (;;) {
        let const n = os::read_fd(read_fd, buffer, sizeof(buffer));
        if (!n.has_value() || *n == 0) break;
        captured.append(StringView{buffer, static_cast<usize>(*n)});
      }
    } catch (...) {}
  });

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
    if (pending_control_flow().kind == ControlFlow::Kind::Exit)
      set_last_exit_status(static_cast<i32>(pending_control_flow().value));
    clear_control_flow();
  }
  leave_subshell();

  m_shell_is_interactive = was_interactive;

  shit::flush();
  os::restore_stdout(saved);
  os::close_fd(pipe->out);
  reader.join();
  os::close_fd(pipe->in);
  restore_state(std::move(snapshot));

  if (error) std::rethrow_exception(error);

  while (!captured.empty() && captured.back() == '\n')
    captured.pop_back();
  return captured;
}

fn EvalContext::run_source(StringView source, StringView origin,
                           bool consume_return, Maybe<SourceLocation> call_site,
                           Maybe<StringView> filename) -> i32
{
  /* Parse into the active arena, coexisting with the outer tree, the same way a
     command substitution does. The control-flow exceptions are not caught here,
     so a return or a break inside the evaluated source reaches the caller. */
  if (AST_ARENA == nullptr) throw Error{"Cannot run source outside of a parse"};

  /* The source the call site lives in, captured before set_current_source below
     changes it, so a backtrace caret renders the dot or eval against the parent
     text rather than the source about to run. It is NULL when no call site is
     known, which sends the backtrace to the plain origin message. */
  const String *const parent_source = call_site ? m_current_source : nullptr;

  /* The frame joins the backtrace stack for the length of this call, so an
     error deep in a nested source prints every call site. The pop runs at
     function scope, after the catch below has read the stack. A frame with no
     call site stores a zero location, unused because parent_source is NULL. */
  m_source_frames.push(SourceFrame{
      String{origin}, call_site ? *call_site : SourceLocation{0, 0},
      parent_source});
  defer { m_source_frames.pop_back(); };

  /* The whole chain from the innermost source out to the outermost is printed
     when an error is caught, so every nested call site is named, not only the
     one running now. A frame whose parent source is known renders a caret at
     its call site, otherwise it falls back to naming the origin. */
  let const print_backtrace = [this]() {
    for (usize i = m_source_frames.size(); i > 0; i--) {
      const SourceFrame &frame = m_source_frames[i - 1];
      if (frame.parent_source != nullptr) {
        /* A frame is context under the primary error, not an error of its own,
           so it prints with the Trace severity rather than Error. */
        let const sourced_here = TraceWithLocation{frame.call_site,
                                                   "sourced here"};
        show_message(sourced_here.to_string(*frame.parent_source));
      } else {
        show_message("This error was raised while running " + frame.origin +
                     ".");
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
    let const ast = parser.construct_ast().release();
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
    set_current_source(retained_source, String{origin});
    defer { set_current_source(previous_source, previous_origin); };

    ast->evaluate(*this);
    /* A return at the top of a sourced file or an eval returns from that source
       with its status, the way a return ends a function. Break, continue, and
       exit keep propagating, so an enclosing loop or the shell consumes them.
     */
    if (consume_return && has_pending_control_flow() &&
        pending_control_flow().kind == ControlFlow::Kind::Return)
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

fn EvalContext::clear_retained_sources() -> void
{
  for (Expression *ast : m_retained_source_asts)
    delete ast;
  m_retained_source_asts.clear();

  for (String *source : m_retained_sources)
    delete source;
  m_retained_sources.clear();

  /* The current source frame may point at a retained copy just freed, so reset
     it to None until the next run sets it. */
  m_current_source = nullptr;
  m_current_origin.clear();
}

fn EvalContext::retain_ast(Expression *ast) -> void
{
  m_retained_source_asts.push(ast);
}

fn EvalContext::expand_heredoc_body(StringView body) -> String
{
  /* A heredoc body keeps its quote characters literally. */
  return expand_modifier_word(body, false);
}

fn EvalContext::process_args(const ArrayList<const Token *> &args)
    -> ArrayList<String>
{
  /* The expansion fields live on the scratch arena only until the heap argument
     vector is built, so the arena is released back to here on return. The mark
     nests, so a command substitution inside one of these words reclaims only
     its own fields and leaves this word's in-progress fields alone. */
  let const scratch_mark = m_scratch_arena.mark();
  defer { m_scratch_arena.release(scratch_mark); };

  let expanded_args = ArrayList<String>{};
  expanded_args.reserve(args.size());

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
                                                std::move(key_literal), false});
        let const &value = a->value_word();
        for (const WordSegment &value_segment : value.segments)
          fallback_word.segments.push(value_segment);
        word = &fallback_word;
      } else {
        fallback_word.segments.push(WordSegment{WordSegment::Kind::UnquotedText,
                                                t->raw_string(), false});
        word = &fallback_word;
      }

      for (GlobField &field : expand_word(*word)) {
        for (String &g : expand_path(std::move(field)))
          expanded_args.push(String{
              heap_allocator(), StringView{g.c_str(), g.size()}
          });
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
                         const ArrayList<String> &args)
    : m_kind(std::move(kind)), m_location(location), m_args(args)
{}

fn ExecContext::source_location() const -> const SourceLocation &
{
  return m_location;
}

fn ExecContext::program() const -> const String &
{
  ASSERT(!m_args.empty());
  return m_args[0];
}

fn ExecContext::args() const -> const ArrayList<String> & { return m_args; }

fn ExecContext::is_builtin() const -> bool { return m_kind.is_builtin(); }

fn ExecContext::program_path() const -> const Path &
{
  ASSERT(!is_builtin());
  return m_kind.program_path;
}

fn ExecContext::close_fds() -> void
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

fn ExecContext::builtin_kind() const -> const Builtin::Kind &
{
  ASSERT(is_builtin());
  return m_kind.builtin_kind;
}

fn ExecContext::print_to_stdout(StringView s) const -> void
{
  if (!os::write_fd(out_fd.value_or(SHIT_STDOUT), s.data, s.length).has_value())
  {
    throw Error{"Unable to write to stdout: " +
                os::last_system_error_message()};
  }
}

fn ExecContext::make_from(SourceLocation location,
                          const ArrayList<String> &args) -> ExecContext
{
  /* Make sure we always include at least one argument, the program path. */
  ASSERT(args.size() > 0);

  let const &program = args[0];

  Maybe<Builtin::Kind> bk;
  Maybe<Path> p;

  if (!program.find_character('/').has_value()) {
    bk = search_builtin(std::string_view{program.c_str(), program.size()});

    if (!bk) {
      let ps = utils::search_program_path(program.view());
      if (ps.size() > 0) p = std::move(ps[0]);
    }
  } else {
    /* TODO: Sanitize extensions here too. */
    p = utils::canonicalize_path(program.view());
  }

  /* Builtins take precedence over programs. */
  ResolvedCommand kind;
  if (!bk) {
    if (p.has_value()) {
      kind = ResolvedCommand::from_program(std::move(*p));
    } else {
      throw ErrorWithLocation{location,
                              "Program '" + program + "' wasn't found"};
    }
  } else {
    kind = ResolvedCommand::from_builtin(*bk);
  }

  return {location, std::move(kind), args};
}

fn ExecContext::from_resolved(SourceLocation location, ResolvedCommand kind,
                              const ArrayList<String> &args) -> ExecContext
{
  ASSERT(args.size() > 0);
  return {location, std::move(kind), args};
}


} /* namespace shit */
