#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Lexer.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Toiletline.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* calc reuses the shell arithmetic evaluator, so the same operators and the
   same located error messages apply. In the default mood the value is computed
   in 128 bits, so a result wider than a signed 64-bit integer prints in full.
   It is a shitbox utility, so a calc binary on PATH is preferred and the
   bundled evaluator runs only when PATH carries none. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-i] [expression ...]");

HELP_DESCRIPTION_DECL(
    "The calc utility evaluates each argument as an arithmetic expression and "
    "prints the value of the last one. In the default mood it computes in 128 "
    "bits. With no expression on a terminal, or with -i, it reads expressions "
    "interactively.");

FLAG(CALC_INTERACTIVE, Bool, 'i', "interactive",
     "Read and evaluate expressions interactively.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Calc);

namespace shit {

namespace shitbox {

Calc::Calc() = default;

pure fn Calc::kind() const wontthrow -> Utility::Kind { return Kind::Calc; }

namespace {

fn evaluate_one(const ExecContext &ec, EvalContext &cxt,
                StringView expression) throws -> i32
{
  bool nonzero = false;
  try {
    String result = cxt.evaluate_arithmetic_wide(expression, nonzero);
    result += '\n';
    ec.print_to_stdout(result);
    return 0;
  } catch (const ErrorWithLocation &error) {
    /* The located caret indexes the typed expression, so the diagnostic points
       under the offending token the way every shit diagnostic reads. */
    show_message(error.to_string(expression));
    return 1;
  } catch (const Error &error) {
    /* An unlocated error, a nested binding read among them, still renders as a
       capitalized shit line rather than the old terse wrapper. */
    show_message(error.to_string());
    return 1;
  }
}

/* A standalone REPL line of the form name=value binds the variable to its
   right-side expression text without evaluating it. A formula may name a
   variable that is not set yet and recompute on each later read. The line
   prints nothing. The return is true when the line was an assignment, and a ==
   comparison is left for the evaluator. */
fn try_define(EvalContext &cxt, StringView line) throws -> bool
{
  usize i = 0;
  while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= line.length || !lexer::is_variable_name_start(line[i])) return false;

  let const name_start = i;
  while (i < line.length && lexer::is_variable_name(line[i]))
    i++;
  let const name = line.substring_of_length(name_start, i - name_start);

  while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
    i++;

  /* A single = assigns, while == is a comparison the evaluator handles. */
  if (i >= line.length || line[i] != '=') return false;
  if (i + 1 < line.length && line[i + 1] == '=') return false;

  usize value_start = i + 1;
  while (value_start < line.length &&
         (line[value_start] == ' ' || line[value_start] == '\t'))
    value_start++;
  usize value_end = line.length;
  while (value_end > value_start &&
         (line[value_end - 1] == ' ' || line[value_end - 1] == '\t'))
    value_end--;

  /* An empty right side is a typo rather than a formula, so it reports rather
     than binding a name that would later read as zero and defeat the unset
     error. The caret spans the name the way the evaluator locates its errors.
   */
  if (value_end == value_start)
    throw ErrorWithLocation{
        SourceLocation{name_start, name.length},
        "Assignment to '" + String{name}
        + "' needs a value"
    };

  /* The right side is stored unevaluated, so the binding is a formula the next
     read evaluates against the current context. */
  cxt.set_shell_variable(
      name, line.substring_of_length(value_start, value_end - value_start));
  return true;
}

/* An interactive session reads through the toiletline editor for line editing
   and history, while a piped run, or a shell that never started the editor,
   falls back to a plain line read with a stderr prompt so the input stays
   deterministic. Ctrl-C and Ctrl-Z echo their indicator and keep the session,
   and a bad line reports and continues rather than ending the session. */
fn run_repl(const ExecContext &ec, EvalContext &cxt) throws -> i32
{
  let const input_fd = ec.in_fd.value_or(SHIT_STDIN);
  let const is_terminal = os::is_fd_a_tty(input_fd);
  let const should_use_editor = is_terminal && toiletline::is_active();

  /* The shell leaves the terminal in cooked mode before running a builtin, so
     the editor REPL takes raw mode for itself the way the main loop does and
     restores it on the way out. Without this the kernel and the editor both
     echo the line, the control keys leak, and the editor state goes invalid. */
  /* The shell completion and the syntax highlighter read shell tokens, which a
     calc expression is not, so they are turned off for the REPL while the
     native toiletline line editing and history navigation stay. The prior state
     is captured so a -T shell is left completion-free on the way out. The
     history swaps to ~/.shit_calc_history so a recalled calc line never mixes
     with the shell command history. */
  let const was_completion_enabled =
      should_use_editor && toiletline::completion_is_enabled();
  if (should_use_editor) {
    toiletline::enter_raw_mode();
    toiletline::disable_completion();
    toiletline::enter_calc_history();
  }

  defer
  {
    if (should_use_editor) {
      toiletline::leave_calc_history();
      if (was_completion_enabled) toiletline::enable_completion(cxt);

      /* This defer runs in a noexcept scope-exit destructor, and exit_raw_mode
         throws when the terminal cannot leave raw mode, so the throw is
         reported here rather than propagated, the way the main loop guards its
         own call.
       */
      try {
        toiletline::exit_raw_mode();
      } catch (const Error &error) {
        show_message(error.to_string());
      }
    }
  };

  loop
  {
    Maybe<String> line;
    if (should_use_editor) {
      toiletline::input_result result{};
      try {
        result = toiletline::get_input(String{"calc> "});
      } catch (const Error &error) {
        show_message(error.to_string());
        break;
      }

      /* The control indicators echo the way the main loop prints them, so
         Ctrl-C, Ctrl-D, and Ctrl-Z read the same inside calc as at the prompt.
         The newline emission then moves output to a fresh row below the line.
       */
      switch (result.code) {
      case TL_PRESSED_TAB: toiletline::set_input(result.text); continue;
      case TL_PRESSED_EOF:
        if (result.text.view().is_empty()) {
          shit::print("^D");
          shit::flush();
          toiletline::emit_newlines(result.text);
          return 0;
        }
        toiletline::set_input(result.text);
        continue;
      case TL_PRESSED_INTERRUPT:
        shit::print("^C");
        shit::flush();
        break;
      case TL_PRESSED_SUSPEND:
        shit::print("^Z");
        shit::flush();
        break;
      default:;
      }

      toiletline::emit_newlines(result.text);

      if (result.code != TL_PRESSED_ENTER || result.text.view().is_empty()) {
        continue;
      }

      line = steal(result.text);
    } else {
      if (is_terminal) shit::print_error("calc> ");
      bool was_delimiter_terminated = false;
      line = utils::read_line_from_fd(input_fd, was_delimiter_terminated);
      if (!line.has_value() || os::INTERRUPT_REQUESTED) {
        break;
      }
    }

    if (line->view().is_empty()) continue;

    /* A standalone binding reports a readonly target or an empty value the way
       a bad expression does, so a write that throws ends the line rather than
       the whole session. */
    try {
      if (try_define(cxt, line->view())) continue;
    } catch (const ErrorWithLocation &error) {
      show_message(error.to_string(line->view()));
      continue;
    } catch (const Error &error) {
      show_message(error.to_string());
      continue;
    }

    evaluate_one(ec, cxt, line->view());
  }

  if (!should_use_editor && is_terminal) {
    shit::print_error("\n");
  }
  return 0;
}

} // namespace

fn Calc::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  /* calc prints only errors, never the shell's advisory warnings such as the
     unset-variable notice, so the whole run suppresses diagnostics and restores
     the prior state on the way out. An unset variable is a calc error instead.
   */
  let const were_diagnostics_disabled = cxt.diagnostics_disabled();
  cxt.set_diagnostics_disabled(true);
  defer { cxt.set_diagnostics_disabled(were_diagnostics_disabled); };

  /* With -i, or with no expression on a terminal, calc reads expressions
     interactively. A piped run with no expression keeps the usage error so it
     does not hang waiting on input that is not coming. */
  let const has_expression = !operands.is_empty();
  let const is_interactive =
      FLAG_CALC_INTERACTIVE.is_enabled() ||
      (!has_expression && os::is_stdin_a_tty() && os::is_stdout_a_tty());
  if (is_interactive) return run_repl(ec, cxt);

  if (!has_expression) return report_usage_error(ec, cxt, args[0].view());

  LOG(Debug, "calc evaluating %zu arithmetic expressions", operands.count());

  /* The arguments join into one expression so `calc 1 + 2` reads as a single
     arithmetic expression rather than three separate ones, the way a desk
     calculator and a bare $(( )) do. */
  String expression{};
  for (usize i = 0; i < operands.count(); i++) {
    if (i > 0) expression += ' ';
    expression += operands[i].view();
  }

  return evaluate_one(ec, cxt, expression.view());
}

} // namespace shitbox

} // namespace shit
