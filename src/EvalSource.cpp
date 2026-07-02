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

static constexpr usize MAX_MIMICRY_DEPTH = 400;

/* An InterruptError is re-thrown past the mimic boundary, so the catch-all
   below does not absorb it into a status. */
static fn mimicked_error_is_interrupt(const std::exception_ptr &error) throws
    -> bool
{
  if (error == nullptr) return false;

  try {
    std::rethrow_exception(error);
  } catch (const InterruptError &) {
    return true;
  } catch (...) {
    return false;
  }
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

  /* A NUL byte in the leading bytes marks a binary file, reported with status
     126. Only the head is sampled, so a script carrying a NUL on a later line
     still runs. */
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

  let const previous_runtime = m_runtime;

  m_runtime.mood = mode;
  LOG(Debug, "mimicking the script '%s'%s", ec.program().c_str(),
      isolated ? " in an isolated subshell" : "");
  let const previous_script_run = m_is_script_run;
  m_is_script_run = true;

  /* A mimicked script runs with the strictness of the mood it mimics, so a bash
     or sh script clears nounset, pipefail, and failglob while a shit script
     keeps the strict default. */
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

  let previous_shell_name = String{m_shell_name};
  let params = ArrayList<String>{heap_allocator()};
  params.reserve(ec.args().count() - 1);
  for (usize i = 1; i < ec.args().count(); i++)
    params.push_managed(ec.args()[i].view());

  let const previous_source = m_current_source;
  let const previous_origin = m_current_origin;
  let const previous_location = m_current_location;

  /* A standard descriptor with no staged redirect is backed up too, since the
     script may move it with an exec redirection that a fork would contain. */
  let saved_fds = ArrayList<os::saved_descriptor>{heap_allocator()};
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
  /* The context descriptors were dup'd onto the standard fds above, so the
     originals are closed when this run ends, the way the replaced fork-and-exec
     would have. */
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

  /* The kernel hands a shebang interpreter the resolved script path, so $0 and
     BASH_SOURCE read that path rather than the word as typed. */
  m_shell_name = String{heap_allocator(), ec.program_path().text().view()};
  set_current_source(&*contents, String{ec.program().view()});
  m_current_location = SourceLocation{};
  m_mimicry_depth++;

  /* The terminal command the shell exits with needs no isolation, so the script
     runs against the current state with no snapshot. */
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
      if (mimicked_error_is_interrupt(error)) throw InterruptError{};

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
  previous_runtime.restore(*this);
  m_is_script_run = previous_script_run;
  m_shell_name = steal(previous_shell_name);

  if (error) {
    if (mimicked_error_is_interrupt(error)) throw InterruptError{};

    render_error(error);
    return 1;
  }
  return status;
}

pure fn EvalContext::shopt_default_is_on(StringView name) wontthrow -> bool
{
  static constexpr PackedStringKey KEYS[] = {
      SSK("progcomp"),
      SSK("promptvars"),
      SSK("sourcepath"),
      SSK("extquote"),
      SSK("complete_fullquote"),
      SSK("hostcomplete"),
      SSK("cmdhist"),
      SSK("checkwinsize"),
      SSK("force_fignore"),
      SSK("globasciiranges"),
      SSK("globskipdots"),
      SSK("expand_aliases"),
      SSK("interactive_comments"),
  };
  static constexpr StaticStringSet DEFAULT_ON_SHOPT_NAMES{KEYS};
  return DEFAULT_ON_SHOPT_NAMES.contains(name);
}

fn EvalContext::run_source(StringView source, StringView origin,
                           bool consume_return, Maybe<SourceLocation> call_site,
                           Maybe<StringView> filename) throws -> i32
{
  if (AST_ARENA == nullptr) throw Error{"Cannot run source outside of a parse"};

  LOG(Debug, "running source '%.*s' of %zu bytes at depth %zu",
      static_cast<int>(origin.length), origin.data, source.length,
      m_source_depth);

  /* Bound the source and eval nesting so a file that sources itself errors here
     rather than exhausting memory. */
  enter_source(call_site ? *call_site : SourceLocation{0, 0});
  defer { leave_source(); };

  let const parent_source = call_site ? m_current_source : nullptr;

  m_source_frames.push(source_frame{
      String{origin},
      call_site ? *call_site : SourceLocation{0, 0},
      parent_source,
      filename.has_value() ? String{*filename}
      : String{heap_allocator()}
  });
  let const frame_is_sourced_file =
      filename.has_value() && !filename->is_empty();
  if (frame_is_sourced_file) m_sourced_file_frames++;
  defer
  {
    if (frame_is_sourced_file) m_sourced_file_frames--;
    m_source_frames.pop_back();
  };

  /* Retain an owned copy of the filename, so the views the lexer stamps onto
     every location stay valid after a control-flow jump carries a stamped
     location out to the top level. */
  Maybe<StringView> stable_filename = None;
  if (filename.has_value()) {
    let const retained_filename = new String{*filename};
    m_retained_sources.push(retained_filename);
    stable_filename = retained_filename->view();
  }

  try {
    let parser = Parser{
        Lexer{String{source}, *AST_ARENA, false, stable_filename, mood()}
    };

    /* Retain the AST before evaluating, so a function it defines outlives this
       call and a control-flow exception thrown inside still leaves it owned. */
    let const ast = parser.construct_ast();
    ASSERT(ast != nullptr);
    m_retained_source_asts.push(ast);

    /* Keep a copy of the source alive for as long as the AST, so a control-flow
       jump made inside it can point a caret at the right text after this call
       returns. */
    let const retained_source = new String{source};
    m_retained_sources.push(retained_source);

    let const previous_source = m_current_source;
    let const previous_origin = m_current_origin;
    let const previous_location = m_current_location;
    set_current_source(retained_source, String{origin});
    m_current_location = SourceLocation{};
    defer
    {
      set_current_source(previous_source, previous_origin);
      m_current_location = previous_location;
    };

    ast->evaluate(*this);
    /* A return at the top of a sourced file or an eval returns from that source
       with its status. Break, continue, and exit keep propagating. */
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
  m_retained_source_asts.clear();

  /* A stashed source view or location may index a buffer freed just below, so
     both drop to the unlocated rendering. */
  for (process_substitution &sub : m_pending_process_substitutions) {
    sub.source = StringView{};
    sub.location = SourceLocation{};
  }

  if (has_pending_control_flow()) {
    pending_control_flow().source = nullptr;
    pending_control_flow().location = SourceLocation{};
  }

  for (String *source : m_retained_sources)
    delete source;
  m_retained_sources.clear();

  /* A just-freed buffer can be reissued at the same address and length, so the
     caches keyed on that are dropped to keep them from serving a stale index.
   */
  invalidate_source_line_index();
  utils::invalidate_line_number_cache();

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
  return expand_modifier_word(body, false);
}

} // namespace shit
