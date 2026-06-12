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

#include <exception>

/* The substitution captures of the evaluator, the $(...) and backtick pipe
   capture with its drain thread, the bare $(< file) read, and the <(cmd)
   process substitutions with their cleanup. Split out of Eval.cpp so the
   evaluator core stays the hot-path file. */

namespace shit {

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
  let const location = m_current_location;
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
  let const previous_location = m_current_location;
  set_current_source(&source, String{"command substitution"});
  defer
  {
    set_current_source(previous_source, previous_origin);
    m_current_location = previous_location;
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

fn EvalContext::capture_function_substitution(const WordSegment &segment) throws
    -> String
{
  if (AST_ARENA == nullptr)
    throw Error{"Function substitution outside of a parse"};

  /* The same per-segment tree cache the $(...) capture keeps, so a funsub in
     a loop body parses once per arena generation. */
  const usize generation = AST_ARENA->reset_generation();
  if (segment.cached_substitution_ast == nullptr ||
      segment.cached_substitution_generation != generation)
  {
    LOG(verbosity::Debug,
        "function substitution ast cache miss for generation %zu, reparsing",
        generation);
    let parser = Parser{
        Lexer{String{segment.text.view()}, *AST_ARENA, false, None, mood()}
    };
    segment.cached_substitution_ast = parser.construct_ast();
    segment.cached_substitution_generation = generation;
  }
  ASSERT(segment.cached_substitution_ast != nullptr);

  let const ast = segment.cached_substitution_ast;
  const String &source = segment.text;
  LOG(verbosity::Debug, "running a function substitution body of %zu bytes",
      source.count());

  /* The body runs against the live state on purpose, no snapshot and no
     subshell, so its assignments, cd, and definitions persist the way the
     bash 5.3 funsub leaves them. Only the capture plumbing matches the
     isolated path, the pipe, the drain thread, and the newline trim. */
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
  let drain_context = command_substitution_drain_context{&captured, pipe->in};
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
    clear_control_flow();

  m_shell_is_interactive = was_interactive;

  shit::flush();
  os::restore_stdout(saved);
  os::close_fd(pipe->out);
  os::join_thread(*reader);
  os::close_fd(pipe->in);

  if (error) {
    /* The same containment the $(...) capture applies, the error renders
       against the body source with the backtrace and the parent continues
       with the partial output and a failing status. */
    LOG(verbosity::Debug,
        "the function substitution failed, containing the error with status 1");
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

} /* namespace shit */
