#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* The completion bridge of the evaluator, the spec registry the complete
   builtin fills and the COMP_WORDS function runner the interactive engine
   calls on an explicit tab. Split out of Eval.cpp so the evaluator core
   stays the hot-path file. */

namespace shit {

fn EvalContext::register_completion_spec(StringView command,
                                         completion_spec spec) throws -> void
{
  LOG(Debug,
      "registering a completion spec for '%.*s' with function '%s' and %zu "
      "word-list bytes",
      static_cast<int>(command.length), command.data,
      spec.function_name.c_str(), spec.word_list.length());
  m_completion_specs.set(command, steal(spec));
}

fn EvalContext::register_default_completion_spec(completion_spec spec) throws
    -> void
{
  LOG(Debug, "registering the default completion spec");
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

  LOG(Info,
      "running the completion function '%.*s' with %zu words, cursor word %zu",
      static_cast<int>(function_name.length), function_name.data, words.count(),
      cword);

  /* A completion function is bash code that reads and writes arrays, COMP_WORDS
     and COMPREPLY above all, so the call evaluates in bash mode whatever the
     interactive session's mode, then the mode is put back. */
  let const saved_mood = m_runtime.mood;
  m_runtime.mood = mimic_mood::Bash;
  defer { m_runtime.mood = saved_mood; };

  /* bash-completion is written for bash's lax defaults and reads unset names
     such as SHELLOPTS freely, so the mood-seeded strictness relaxes for the
     function run the way the mood does. An explicit set -u or set -o failglob
     is the user's own ask and stays fatal, the same rule -W follows. */
  let const saved_error_unset = m_runtime.error_unset;
  let const saved_failglob = m_runtime.failglob;
  if (!m_runtime.error_unset_explicit) m_runtime.error_unset = false;
  if (!m_runtime.failglob_explicit) m_runtime.failglob = false;
  defer
  {
    m_runtime.error_unset = saved_error_unset;
    m_runtime.failglob = saved_failglob;
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
    LOG(Debug, "completion function '%.*s' threw: %s",
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
  LOG(Info, "completion function '%.*s' returned %zu candidates with status %d",
      static_cast<int>(function_name.length), function_name.data,
      result.count(),
      out_exit_status != nullptr ? *out_exit_status : last_exit_status());
  return result;
}

} /* namespace shit */
