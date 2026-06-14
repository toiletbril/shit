#include "Common.hpp"
#include "Debug.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

fn EvalContext::register_function(StringView name, const Expression *body,
                                  StringView definition_text,
                                  usize body_start_position,
                                  SourceLocation definition_location) throws
    -> void
{
  LOG(Info, "registering function '%.*s' with a %zu byte definition",
      static_cast<int>(name.length), name.data, definition_text.length);
  m_functions.set(name, body);
  m_function_sources.set(name, definition_text);

  /* The copy is the "name () " header line then the body span verbatim, so
     the header length and the body's line in the defining file map an
     absolute body position onto the copy. The body sits on line two of the
     copy, and the printed line adds the offset so it matches the file. */
  let info = function_definition_info{};
  info.body_start_position = body_start_position;
  info.header_length = name.length + StringView{" () \n"}.length;
  if (m_current_source != nullptr && !definition_text.is_empty()) {
    const usize body_line =
        utils::line_number_at(m_current_source->view(), body_start_position);
    info.line_offset = body_line > 2 ? body_line - 2 : 0;
  }
  if (definition_location.filename.has_value())
    info.filename = String{*definition_location.filename};
  info.defining_instance = m_current_source;
  /* The mood and the diagnostics state are captured so the call restores them,
     letting a function defined in bash mood run bash after a later set --mood,
     and a function defined while diagnostics were off skip its checks. */
  info.defining_mood = static_cast<u8>(m_mood);
  info.defining_warnings = m_warnings_enabled;
  info.defining_diagnostics_disabled = m_diagnostics_disabled;
  m_function_definition_infos.set(name, steal(info));
}

fn EvalContext::function_definition_info_of(StringView name) const wontthrow
    -> const function_definition_info *
{
  return m_function_definition_infos.find(name);
}

pure fn EvalContext::resolve_render_source(
    SourceLocation location) const wontthrow -> resolved_render_source
{
  let resolved = resolved_render_source{};
  resolved.text = m_current_source;

  /* Inside a function call the body's positions index the file that defined
     it, which may not be the current source and may already be freed. When
     the innermost function was defined against another source instance and
     the position falls inside its recorded body span, the stored definition
     copy renders it with the defining file's name and numbering. */
  if (m_function_call_names.is_empty()) return resolved;
  let const innermost = funcname_frame_at(0);
  let const *info = m_function_definition_infos.find(innermost);
  /* The body span check below decides whether to window, not the defining
     source pointer. A freed defining source can have its address reused by the
     current source, so a pointer compare here would falsely read the body as
     belonging to the current source and drop the function's filename. */
  if (info == nullptr) return resolved;
  let const *copy = m_function_sources.find(innermost);
  if (copy == nullptr || copy->count() <= info->header_length) return resolved;
  const usize body_length = copy->count() - info->header_length;
  if (location.position < info->body_start_position ||
      location.position >= info->body_start_position + body_length)
    return resolved;

  resolved.text = copy;
  resolved.windowed = true;
  resolved.body_start_position = info->body_start_position;
  resolved.header_length = info->header_length;
  resolved.line_offset = info->line_offset;
  resolved.filename =
      info->filename.is_empty() ? StringView{} : info->filename.view();
  return resolved;
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
  LOG(Info, "unsetting function '%.*s'",
      static_cast<int>(name.length), name.data);
  m_functions.erase(name);
  m_function_sources.erase(name);
  m_function_definition_infos.erase(name);
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
  LOG(Info, "setting a trap for '%.*s' with a %zu byte action",
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
  LOG(Info, "removing the trap for '%.*s'",
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
  LOG(Info, "reinstalling the dispositions of %zu traps",
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
        LOG(Info, "running the trap action for signal '%s'",
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
      LOG(Info, "running the EXIT trap action at shell exit");
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
      LOG(Info,
          "running the EXIT trap action the subshell set at its end");
      run_source(action->view(), "the EXIT trap");
    }
}

fn EvalContext::mark_readonly(StringView name) throws -> void
{
  m_readonly_names.add(name);
}

fn EvalContext::unmark_readonly(StringView name) throws -> void
{
  m_readonly_names.remove(name);
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

} /* namespace shit */
