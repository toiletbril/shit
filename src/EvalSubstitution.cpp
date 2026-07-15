#include "Arena.hpp"
#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

fn EvalContext::render_contained_substitution_error(std::exception_ptr error,
                                                    StringView source) throws
    -> void
{
  try {
    std::rethrow_exception(error);
  } catch (const ErrorWithLocationAndDetails &e) {
    show_message(e.to_string(source));
    show_message(e.details_to_string(source));
    print_source_backtrace(e.location());
  } catch (const ErrorWithLocation &e) {
    show_message(e.to_string(source));
    print_source_backtrace(e.location());
  } catch (const Error &e) {
    show_message(e.to_string());
    print_source_backtrace();
  }
}

static constexpr usize DRAIN_CHUNK_LENGTH = 4096;

/* The drain thread reads the pipe into its own libc buffer while the inner
   command writes the other end, so output larger than the pipe buffer cannot
   deadlock. The buffer is grown with libc realloc because the shared heap
   pool's free list is single threaded. */
struct command_substitution_drain_context
{
  char *data;
  usize length;
  usize capacity;
  os::descriptor read_fd;
};

fn drain_command_substitution_pipe(opaque *raw_context) wontthrow -> void
{
  let drain = static_cast<command_substitution_drain_context *>(raw_context);
  loop
  {
    if (drain->length + DRAIN_CHUNK_LENGTH > drain->capacity) {
      usize grown_capacity =
          drain->capacity == 0 ? DRAIN_CHUNK_LENGTH * 2 : drain->capacity * 2;
      while (grown_capacity < drain->length + DRAIN_CHUNK_LENGTH)
        grown_capacity *= 2;

      let grown =
          static_cast<char *>(std::realloc(drain->data, grown_capacity));
      if (grown == nullptr) break;

      drain->data = grown;
      drain->capacity = grown_capacity;
    }

    let const bytes_read = os::read_fd(
        drain->read_fd, drain->data + drain->length, DRAIN_CHUNK_LENGTH);
    if (!bytes_read.has_value() || *bytes_read == 0) break;
    drain->length += static_cast<usize>(*bytes_read);
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
  let const ast_mark = AST_ARENA->mark();
  defer { AST_ARENA->release(ast_mark); };
  let lexer = Lexer{String{source.substring_of_length(i, source.length - i)},
                    *AST_ARENA, false, None, mood()};
  Token *name = lexer.next_shell_token();
  if (name == nullptr || name->kind() != Token::Kind::Word) return None;
  /* Anything after the single filename means this is not the bare read form. */
  Token *after = lexer.next_shell_token();
  if (after != nullptr && after->kind() != Token::Kind::EndOfFile &&
      after->kind() != Token::Kind::Newline)
  {
    return None;
  }

  let const filename = expand_word_for_assignment(
      static_cast<const tokens::WordToken *>(name)->word());
  LOG(Debug, "the substitution is a bare file read of '%s'", filename.c_str());
  let content = Path{filename.view()}.read_entire_file();
  if (!content.has_value()) {
    LOG(Debug, "the file read substitution of '%s' failed, expanding to empty",
        filename.c_str());
    return String{heap_allocator()};
  }
  let result = steal(*content);
  while (!result.is_empty() && result.back() == '\n')
    result.pop_back();
  return result;
}

fn EvalContext::capture_command_substitution(const String &source,
                                             Maybe<StringView> filename) throws
    -> String
{
  LOG(Debug, "capturing a command substitution of %zu bytes", source.count());
  if (Maybe<String> file = read_redirect_substitution(source.view());
      file.has_value())
    return steal(*file);

  /* A caller such as the make $(shell) names a filename, so an error inside the
     command carets that source rather than a bare unnamed line. */
  if (AST_ARENA == nullptr)
    throw Error{"Command substitution outside of a parse"};
  let const ast_mark = AST_ARENA->mark();
  defer { AST_ARENA->release(ast_mark); };

  enter_substitution();
  defer { leave_substitution(); };

  let parser = Parser{
      Lexer{String{source.view()}, *AST_ARENA, false, filename, mood()}
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

  /* The first byte is the direction marker the lexer wrote. */
  const char direction = text[0];
  let const command_writes_the_pipe = direction == '<';
  LOG(Debug, "setting up a process substitution where the command %s the pipe",
      command_writes_the_pipe ? "writes" : "reads");

#if SHIT_PLATFORM_IS WIN32
  /* Windows has no fork, so the substitution runs in a fresh shell that writes
     its output to a temp file the consuming command reads by path. The >(cmd)
     form's ordering the synchronous spawn here cannot provide. */
  if (!command_writes_the_pipe)
    throw Error{"Unable to run a >(cmd) process substitution because it is not "
                "supported on this platform"};
  if (Maybe<String> substitution_path =
          os::run_substitution_to_temp(text.substring(1), is_bash_compatible());
      substitution_path.has_value())
  {
    m_substitution_temp_files.track(Path{substitution_path->view()});
    return steal(*substitution_path);
  }
  throw Error{"Unable to run the process substitution because the inner shell "
              "could not be spawned: " +
              os::last_system_error_message()};
#else
  let const ast_mark = AST_ARENA->mark();
  defer { AST_ARENA->release(ast_mark); };
  let parser = Parser{
      Lexer{String{text.substring(1)}, *AST_ARENA, false, None, mood()}
  };
  let const ast = parser.construct_ast();
  ASSERT(ast != nullptr);

  let const pipe = os::make_pipe();
  if (!pipe.has_value())
    throw Error{"Could not open a pipe for the process substitution: " +
                os::last_system_error_message()};

  bool was_pipe_handed_off = false;
  defer
  {
    if (!was_pipe_handed_off) {
      os::close_fd(pipe->in);
      os::close_fd(pipe->out);
    }
  };

  const os::process child = command_writes_the_pipe
                                ? os::fork_compound_stage(None, pipe->out, None)
                                : os::fork_compound_stage(pipe->in, None, None);
  was_pipe_handed_off = true;

  if (child == 0) {
    os::close_fd(command_writes_the_pipe ? pipe->in : pipe->out);
    i32 status = 0;
    try {
      ast->evaluate(*this);
      status = last_exit_status();
    } catch (...) {
      LOG(Debug,
          "the process substitution child swallowed an error, exiting with "
          "status 1");
      status = 1;
    }
    os::exit_process_immediately(status);
  }

  /* The kept end must survive an exec so the consuming command inherits it and
     a read of /dev/fd/N reaches this pipe. */
  let const shell_fd = command_writes_the_pipe ? pipe->in : pipe->out;
  os::close_fd(command_writes_the_pipe ? pipe->out : pipe->in);
  os::make_fd_inheritable(shell_fd);
  let const location = m_current_location;
  let const source =
      m_current_source != nullptr ? m_current_source->view() : StringView{};
  m_pending_process_substitutions.push(
      process_substitution{shell_fd, child, location, source});

  let path = String{"/dev/fd/"};
  path += String::from(static_cast<i64>(shell_fd), heap_allocator());
  LOG(Debug, "the process substitution is reachable at '%s'", path.c_str());
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
  LOG(Debug, "cleaning up %zu pending process substitutions",
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
      LOG(Debug, "a process substitution reap failed and was swallowed: %s",
          e.message().c_str());
      /* bash stays silent here, so the warning is suppressed in bash mode. */
      if (!is_bash_compatible()) {
        try {
          let const text =
              "A process substitution child could not be reaped. " +
              e.message();
          show_message(sub.source.is_empty()
                           ? Warning{text}.to_string()
                           : WarningWithLocation{sub.location, text}.to_string(
                                 sub.source));
        } catch (...) {
          LOG(Debug, "showing the reap warning failed, the error is swallowed");
        }
      }
    } catch (...) {
      LOG(Debug, "a process substitution reap failed with an unknown error, "
                 "swallowed");
      if (!is_bash_compatible()) {
        try {
          let const text =
              StringView{"A process substitution child could not be reaped."};
          show_message(sub.source.is_empty()
                           ? Warning{text}.to_string()
                           : WarningWithLocation{sub.location, text}.to_string(
                                 sub.source));
        } catch (...) {
          LOG(Debug, "showing the fallback reap warning failed, the error is "
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

  enter_substitution();
  defer { leave_substitution(); };

  /* A cached tree from an earlier arena generation points into reclaimed
     storage, so it is reparsed when the generation no longer matches. */
  let cache_arena = segment.is_substitution_cache_in_function_arena
                        ? FUNCTION_ARENA
                        : AST_ARENA;
  ASSERT(cache_arena != nullptr);
  const usize generation = cache_arena->reset_generation();
  if (segment.cached_substitution_ast == nullptr ||
      segment.cached_substitution_generation != generation)
  {
    LOG(Debug,
        "command substitution ast cache miss for generation %zu, reparsing",
        generation);
    let parser = Parser{
        Lexer{String{segment.text.view()}, *cache_arena, false, None, mood()}
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
  LOG(Debug, "running a captured substitution body of %zu bytes",
      source.count());

  /* The inner scratch is reclaimed at the substitution boundary, so a $(...)
     inside a loop does not grow the arena across iterations. The captured
     output is heap and escapes. */
  let const substitution_mark = m_scratch_arena.mark();
  defer { m_scratch_arena.release(substitution_mark); };

  let const previous_source = m_current_source;
  let const previous_origin = m_current_origin;
  let const previous_location = m_current_location;
  set_current_source(&source, String{"command substitution"});
  defer
  {
    set_current_source(previous_source, previous_origin);
    m_current_location = previous_location;
  };

#if SHIT_PLATFORM_IS POSIX
  let const pipe = os::make_pipe();
  if (!pipe) throw Error{"Could not open a pipe for command substitution"};
  bool was_pipe_handed_off = false;
  defer
  {
    if (!was_pipe_handed_off) {
      os::close_fd(pipe->in);
      os::close_fd(pipe->out);
    }
  };

  shit::flush();
  let const child = os::fork_compound_stage(None, pipe->out, None);
  was_pipe_handed_off = true;
  if (child == 0) {
    os::close_fd(pipe->in);
    m_shell_is_interactive = false;
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
    if (error) {
      render_contained_substitution_error(error, source.view());
      set_last_exit_status(1);
    }
    shit::flush();
    os::exit_process_immediately(last_exit_status());
  }

  os::close_fd(pipe->out);
  let captured = os::read_fd_to_string(pipe->in, heap_allocator());
  os::close_fd(pipe->in);
  let was_stopped = false;
  let const status = os::wait_and_monitor_process(child, &was_stopped);
  unused(was_stopped);
  set_last_exit_status(status);
  if (!captured.has_value())
    throw Error{"Could not read command substitution output"};
  while (!captured->is_empty() && captured->back() == '\n')
    captured->pop_back();
  return steal(*captured);
#else
  let snapshot = snapshot_state();

  let const pipe = os::make_pipe();
  if (!pipe) throw Error{"Could not open a pipe for command substitution"};

  let captured = String{heap_allocator()};
  let drain_context =
      command_substitution_drain_context{nullptr, 0, 0, pipe->in};
  let const reader =
      os::start_thread(drain_command_substitution_pipe, &drain_context);
  if (!reader) {
    os::close_fd(pipe->in);
    os::close_fd(pipe->out);
    throw Error{"Could not start a thread for command substitution"};
  }

  shit::flush();
  let const saved = os::redirect_stdout(pipe->out);

  let const was_interactive = m_shell_is_interactive;
  m_shell_is_interactive = false;

  /* A break, continue, return, or exit inside a substitution acts only within
     it and must not escape into the enclosing loop, function, or shell. */
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
  /* The substitution's own EXIT action runs while stdout still points at the
     pipe, so its output joins the captured value. */
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

  if (drain_context.data != nullptr) {
    captured.append(StringView{drain_context.data, drain_context.length});
    std::free(drain_context.data);
  }

  restore_state(steal(snapshot));

  if (error) {
    /* A throw inside the substitution is contained to its subshell the way bash
       holds a fatal expansion error to the command substitution. */
    LOG(Debug,
        "the command substitution failed, containing the error with status 1");
    render_contained_substitution_error(error, source.view());
    set_last_exit_status(1);
  }

  while (!captured.is_empty() && captured.back() == '\n')
    captured.pop_back();
  return captured;
#endif
}

fn EvalContext::capture_function_substitution(const WordSegment &segment) throws
    -> String
{
  if (AST_ARENA == nullptr)
    throw Error{"Function substitution outside of a parse"};

  let cache_arena = segment.is_substitution_cache_in_function_arena
                        ? FUNCTION_ARENA
                        : AST_ARENA;
  ASSERT(cache_arena != nullptr);
  const usize generation = cache_arena->reset_generation();
  if (segment.cached_substitution_ast == nullptr ||
      segment.cached_substitution_generation != generation)
  {
    LOG(Debug,
        "function substitution ast cache miss for generation %zu, reparsing",
        generation);
    let parser = Parser{
        Lexer{String{segment.text.view()}, *cache_arena, false, None, mood()}
    };
    segment.cached_substitution_ast = parser.construct_ast();
    segment.cached_substitution_generation = generation;
  }
  ASSERT(segment.cached_substitution_ast != nullptr);

  let const ast = segment.cached_substitution_ast;
  const String &source = segment.text;
  LOG(Debug, "running a function substitution body of %zu bytes",
      source.count());

  /* The body runs against the live state, no snapshot and no subshell, so its
     assignments, cd, and definitions persist the way the bash 5.3 funsub
     leaves them. */
  let const previous_source = m_current_source;
  let const previous_origin = m_current_origin;
  let const previous_location = m_current_location;
  set_current_source(&source, String{"function substitution"});
  defer
  {
    set_current_source(previous_source, previous_origin);
    m_current_location = previous_location;
  };

  let const pipe = os::make_pipe();
  if (!pipe) throw Error{"Could not open a pipe for function substitution"};

  let captured = String{heap_allocator()};
  let drain_context =
      command_substitution_drain_context{nullptr, 0, 0, pipe->in};
  let const reader =
      os::start_thread(drain_command_substitution_pipe, &drain_context);
  if (!reader) {
    os::close_fd(pipe->in);
    os::close_fd(pipe->out);
    throw Error{"Could not start a thread for function substitution"};
  }

  shit::flush();
  let const saved = os::redirect_stdout(pipe->out);

  let const was_interactive = m_shell_is_interactive;
  m_shell_is_interactive = false;

  std::exception_ptr error;
  try {
    ast->evaluate(*this);
  } catch (...) {
    error = std::current_exception();
  }
  /* A break, continue, or return acts only within the body and is consumed
     here. An exit stays pending, so the shell ends after the surrounding
     command finishes, the way bash exits from a funsub. */
  if (has_pending_control_flow() &&
      pending_control_flow().kind != control_flow::Kind::Exit)
  {
    clear_control_flow();
  }

  m_shell_is_interactive = was_interactive;

  shit::flush();
  os::restore_stdout(saved);
  os::close_fd(pipe->out);
  os::join_thread(*reader);
  os::close_fd(pipe->in);

  if (drain_context.data != nullptr) {
    captured.append(StringView{drain_context.data, drain_context.length});
    std::free(drain_context.data);
  }

  if (error) {
    LOG(Debug,
        "the function substitution failed, containing the error with status 1");
    render_contained_substitution_error(error, source.view());
    set_last_exit_status(1);
  }

  while (!captured.is_empty() && captured.back() == '\n')
    captured.pop_back();
  return captured;
}

} // namespace shit
