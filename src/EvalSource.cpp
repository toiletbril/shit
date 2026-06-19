#include "Arena.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "ResolvedCommand.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

/* A cap on nested mimicked scripts, so a script that mimics another that mimics
   it cannot recurse without limit. */
static constexpr usize MAX_MIMICRY_DEPTH = 400;

/* A Ctrl-C inside a mimicked script arrives as Error{"Interrupted"} thrown from
   the next AST node. The mimicked script runs in-process, so without this the
   catch-all below would absorb the interrupt into a status and let the script
   that invoked the mimic keep running. The interrupt is recognized here so the
   caller can re-throw it past the mimic boundary the way a normal run stops. */
static fn mimicked_error_is_interrupt(const std::exception_ptr &error) throws
    -> bool
{
  if (error == nullptr) return false;

  try {
    std::rethrow_exception(error);
  } catch (const Error &caught) {
    return caught.message() == "Interrupted";
  } catch (...) {
    return false;
  }

  return false;
}

fn EvalContext::run_mimicked_script(ExecContext &ec, mimic_mood mode,
                                    bool isolated) throws -> i32
{
  if (m_mimicry_depth >= MAX_MIMICRY_DEPTH)
    throw Error{"Unable to mimic '" + ec.program() +
                "' because the script nesting is too deep"};
  if (AST_ARENA == nullptr)
    throw Error{"Unable to mimic '" + ec.program() + "' outside of a parse"};

  let contents = ec.program_path().read_entire_file();
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
    LOG(Debug, "a NUL byte in the leading bytes marks '%s' as a binary file",
        ec.program().c_str());
    shit::print_error("shit: " + ec.program_path().text() +
                      ": cannot execute binary file\n");
    return 126;
  }

  /* The mimic mode decides the lexing and the evaluation, so it is set before
     the parse. The parent's mode is put back when the run is isolated, while
     the terminal run leaves it since the shell exits next. */
  let const previous_mood = m_runtime.mood;
  m_runtime.mood = mode;
  LOG(Debug, "mimicking the script '%s'%s", ec.program().c_str(),
      isolated ? " in an isolated subshell" : "");
  /* A mimicked script is a script-file run, so its FUNCNAME bottoms out at
     "main" the way the direct file invocation marks it. */
  let const previous_script_run = m_is_script_run;
  m_is_script_run = true;

  /* A mimicked script runs with the strictness of the mood it mimics, so a bash
     or sh script clears nounset, pipefail, and failglob the way the named shell
     runs a file, while a shit script keeps the strict default. The isolated
     case puts the parent's options back. This is why a mimicked declare -A
     array literal does not abort on the unmatched [k]=v glob, and an unset
     parameter expands empty rather than tripping nounset, the way bash runs the
     script. */
  let const previous_error_unset = error_unset();
  let const previous_pipefail = pipefail();
  let const previous_failglob = failglob();
  let const is_mimic_strict = mode == mimic_mood::Default;
  set_error_unset(is_mimic_strict);
  set_pipefail(is_mimic_strict);
  set_failglob(is_mimic_strict);
  LOG(Debug, "seeded the strict options for the %s mimicked run",
      is_mimic_strict ? "shit" : "lax");

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
  let const previous_location = m_current_location;

  /* The redirections the spawn would have applied are applied to the standard
     descriptors for the in-process run, then put back. A file redirect to
     stdout or stderr is already staged on the real shell fd by the simple
     command, so only the descriptors carried on the context are applied here.
     A standard descriptor with no staged redirect is backed up too, since the
     script itself may move it with an exec redirection, the way configure
     points stdin away, and a fork would have contained that. */
  let saved_fds = ArrayList<os::saved_descriptor>{};
  saved_fds.push(ec.in_fd.has_value()
                     ? os::save_and_replace_descriptor(0, *ec.in_fd)
                     : os::save_descriptor(0));
  saved_fds.push(ec.out_fd.has_value()
                     ? os::save_and_replace_descriptor(1, *ec.out_fd)
                     : os::save_descriptor(1));
  saved_fds.push(ec.err_fd.has_value()
                     ? os::save_and_replace_descriptor(2, *ec.err_fd)
                     : os::save_descriptor(2));
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
    } catch (const ErrorWithLocation &located_error) {
      show_message(located_error.to_string(contents->view()));
      print_source_backtrace(located_error.location());
    } catch (const Error &caught_error) {
      show_message(caught_error.to_string());
      print_source_backtrace();
    }
  };

  /* The kernel hands a shebang interpreter the resolved script path, so $0
     and BASH_SOURCE read the path the exec would have received, not the
     word as typed. realpath "$0" then finds the script's true directory for
     a PATH-found command the way it does under bash. */
  m_shell_name = String{heap_allocator(), ec.program_path().text().view()};
  set_current_source(&*contents, String{ec.program().view()});
  m_current_location = SourceLocation{};
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
      if (mimicked_error_is_interrupt(error)) throw Error{"Interrupted"};

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
  m_current_location = previous_location;
  m_runtime.mood = previous_mood;
  m_is_script_run = previous_script_run;
  set_error_unset(previous_error_unset);
  set_pipefail(previous_pipefail);
  set_failglob(previous_failglob);
  m_shell_name = steal(previous_shell_name);

  if (error) {
    if (mimicked_error_is_interrupt(error)) throw Error{"Interrupted"};

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
      "progcomp",
      "promptvars",
      "sourcepath",
      "extquote",
      "complete_fullquote",
      "hostcomplete",
      "cmdhist",
      "checkwinsize",
      "force_fignore",
      "globasciiranges",
      "globskipdots",
      "expand_aliases",
      "interactive_comments",
  };
  for (const StringView default_name : DEFAULT_ON_SHOPT_NAMES)
    if (name == default_name) return true;
  return false;
}

fn EvalContext::run_source(StringView source, StringView origin,
                           bool consume_return, Maybe<SourceLocation> call_site,
                           Maybe<StringView> filename) throws -> i32
{
  /* Parse into the active arena, coexisting with the outer tree, the same way a
     command substitution does. The control-flow exceptions are not caught here,
     so a return or a break inside the evaluated source reaches the caller. */
  if (AST_ARENA == nullptr) throw Error{"Cannot run source outside of a parse"};

  LOG(Debug, "running source '%.*s' of %zu bytes at depth %zu",
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
    let const previous_location = m_current_location;
    set_current_source(retained_source, String{origin});
    /* The sourced text has its own line numbering, so $LINENO inside it counts
       from its first line. The parent location is restored on return so the
       caller's $LINENO resumes against the caller's source. */
    m_current_location = SourceLocation{};
    defer
    {
      set_current_source(previous_source, previous_origin);
      m_current_location = previous_location;
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
  } catch (const ErrorWithLocationAndDetails &detailed_error) {
    show_message(detailed_error.to_string(source));
    show_message(detailed_error.details_to_string(source));
    print_source_backtrace(detailed_error.location());
    return 1;
  } catch (const ErrorWithLocation &located_error) {
    show_message(located_error.to_string(source));
    print_source_backtrace(located_error.location());
    return 1;
  } catch (const Error &caught_error) {
    show_message(caught_error.to_string());
    print_source_backtrace();
    return 1;
  }
}

fn EvalContext::clear_retained_sources() wontthrow -> void
{
  LOG(All, "dropping %zu retained sources and %zu retained asts",
      m_retained_sources.count(), m_retained_source_asts.count());
  /* The retained AST nodes live in the arena, which runs every node's
     destructor on the reset that follows, so this only drops the references. */
  m_retained_source_asts.clear();

  /* A pending process substitution stashed its command's source view and
     location for a later reap warning, and both may index a buffer freed
     just below, so the stash drops to the unlocated rendering first. */
  for (process_substitution &sub : m_pending_process_substitutions) {
    sub.source = StringView{};
    sub.location = SourceLocation{};
  }

  /* A break or continue that escaped to the top level, such as one that a stray
     rc line ran, holds a pointer to its source and a location whose filename
     view both index a buffer freed just below. Both drop to the unlocated
     rendering, since the escaped jump is rendered only after the next chunk. */
  if (has_pending_control_flow()) {
    pending_control_flow().source = nullptr;
    pending_control_flow().location = SourceLocation{};
  }

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
  LOG(Debug, "expanding a heredoc body of %zu bytes", body.length);
  /* A heredoc body keeps its quote characters literally. */
  return expand_modifier_word(body, false);
}

} /* namespace shit */
