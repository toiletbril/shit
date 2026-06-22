#include "Expressions.hpp"

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "ExpressionsInternal.hpp"
#include "Lexer.hpp"
#include "Optimizer.hpp"
#include "Platform.hpp"
#include "Shitbox.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

fn indent_for_layer(usize layer) throws -> String
{
  let pad = String{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad;
}

Expression::Expression(SourceLocation location)
    : m_location(location),
      m_source_end_position(location.position + location.length)
{}

pure fn Expression::source_location() const wontthrow -> SourceLocation
{
  return m_location;
}

pure fn Expression::source_end_position() const wontthrow -> usize
{
  return m_source_end_position;
}

fn Expression::set_source_end_position(usize position) wontthrow -> void
{
  m_source_end_position = position;
}

cold fn Expression::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

hot flatten fn Expression::evaluate(EvalContext &cxt) const throws -> i64
{
  /* A Ctrl-C sets the interrupt flag, and the check here runs before every
     node, so a running command, including a loop body or condition, stops
     promptly and control returns to the prompt. */
  if (os::INTERRUPT_REQUESTED) {
    os::INTERRUPT_REQUESTED = 0;
    throw InterruptError{};
  }
  /* A trapped signal arrived since the last node, so its action runs here at
     the command boundary before the next node. The single flag keeps the common
     no-signal path to one read. */
  if (os::SIGNAL_PENDING) cxt.run_pending_traps();
  cxt.add_evaluated_expression();
  return evaluate_impl(cxt);
}

fn Expression::operator delete(opaque *pointer) wontthrow -> void
{
  if (is_arena_pointer(pointer)) return;
  ::operator delete(pointer);
}

cold fn AnalysisContext::warn(SourceLocation location, StringView message,
                              StringView suggestion) throws -> void
{
  WarningWithLocation located{location, message};
  if (!suggestion.is_empty()) located.set_note(suggestion);
  show_message(located.to_string(source));
}

cold fn AnalysisContext::trace_optimizer_line(StringView message) const throws
    -> void
{
  if (!should_trace_optimizer) return;
  print_error("[optimizer] ");
  print_error(message);
  print_error("\n");
}

cold fn AnalysisContext::trace_eliminated_node(SourceLocation location,
                                               StringView message) const throws
    -> void
{
  if (!should_print_optimizer_state) return;
  const WarningWithLocation located{location, message};
  print_error("[optimizer-state] ");
  print_error(located.to_string(source));
  print_error("\n");
}

cold fn AnalysisContext::fail(SourceLocation location, StringView message,
                              StringView suggestion) throws -> void
{
  /* Under -W the analysis still runs but its errors are reported as warnings
     and the run proceeds, so the same call reports without stopping. */
  if (should_treat_errors_as_warnings) {
    warn(location, message, suggestion);
    return;
  }
  ErrorWithLocation located{location, message};
  if (!suggestion.is_empty()) located.set_note(suggestion);
  show_message(located.to_string(source));
  has_fatal = true;
}

/* A command-not-found at runtime is non-fatal. It prints a located diagnostic
   to stderr against the source the evaluator is running, so a redirection such
   as 2>/dev/null still suppresses it, and leaves the caller to report status
   127. */
cold fn report_command_not_found(EvalContext &cxt,
                                 const CommandNotFound &e) throws -> void
{
  const String *source = cxt.current_source();
  show_message(e.to_string(source != nullptr ? source->view() : StringView{}));
  /* A command not found inside a sourced file prints the source backtrace under
     the error the way a fatal error does, so the chain of dot or source calls
     that led here is named. It prints nothing at the top level. */
  cxt.print_source_backtrace(e.location());
}

fn window_function_body_error(EvalContext &cxt, ErrorWithLocation &error)
    wontthrow -> Maybe<StringView>
{
  let const resolved = cxt.resolve_render_source(error.location());
  if (!resolved.is_windowed || resolved.text == nullptr) return None;

  let rebased = error.location();
  rebased.position =
      rebased.position - resolved.body_start_position + resolved.header_length;
  rebased.filename = resolved.filename.is_empty()
                         ? Maybe<StringView>{}
                         : Maybe<StringView>{resolved.filename};
  if (rebased.position > resolved.text->count()) return None;

  error.set_location(rebased);
  error.set_line_offset(resolved.line_offset);
  return resolved.text->view();
}

cold fn Expression::analyze(AnalysisContext &actx,
                            bool is_unconditional) const throws -> void
{
  unused(actx);
  unused(is_unconditional);
}

cold fn Expression::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  unused(actx);
}

fn Expression::is_simple_command() const wontthrow -> bool { return false; }

fn Expression::is_dummy() const wontthrow -> bool { return false; }

fn Expression::as_if_clause() const wontthrow -> const expressions::IfClause *
{
  return nullptr;
}

fn Expression::as_while_loop() const wontthrow -> const expressions::WhileLoop *
{
  return nullptr;
}

fn Expression::as_assign_command() const wontthrow
    -> const expressions::AssignCommand *
{
  return nullptr;
}

fn Expression::as_simple_command() const wontthrow
    -> const expressions::SimpleCommand *
{
  return nullptr;
}

fn Expression::as_for_loop() const wontthrow -> const expressions::ForLoop *
{
  return nullptr;
}

fn Expression::as_cstyle_for_loop() const wontthrow
    -> const expressions::CStyleForLoop *
{
  return nullptr;
}

fn Expression::try_static_condition_verdict(
    const AnalysisContext &actx) const wontthrow -> Maybe<bool>
{
  unused(actx);
  return shit::None;
}

namespace {

/* The literal name of a command when it is statically known. A word that holds
   a variable reference or a live glob metacharacter is dynamic, so its target
   cannot be checked before run time. */
fn static_command_name(const Token *token) throws -> Maybe<String>
{
  ASSERT(token != nullptr);

  if (token->kind() != Token::Kind::Word) return shit::None;

  let const &word = static_cast<const tokens::WordToken *>(token)->word();

  let name = String{};
  for (let const &segment : word.segments) {
    /* Any expansion segment makes the name a runtime value, the variable,
       command, arithmetic, process, and function substitutions alike, so
       none of their raw bytes may pass for the program text. */
    if (segment.kind != WordSegment::Kind::LiteralText &&
        segment.kind != WordSegment::Kind::DoubleQuotedText &&
        segment.kind != WordSegment::Kind::UnquotedText)
    {
      return shit::None;
    }
    if (segment.kind == WordSegment::Kind::UnquotedText) {
      for (usize i = 0; i < segment.text.count(); i++) {
        if (lexer::is_expandable_char(segment.text[i])) return shit::None;
      }
    }
    name.append(segment.text.view());
  }
  return name;
}

/* A command resolves when it is a builtin, a program on PATH, or an existing
   path. The result is memoized per name in the analysis context, so a command
   run many times across the file scans PATH at most once. A name holding a
   slash is a path, so it is not cached, since the filesystem may differ per
   run. */
/* Expand a leading tilde in a command path the way the runtime does before it
   resolves the command, so the analysis checks ~/bin/foo at the home directory
   rather than as a literal ~ path. None when the path has no leading tilde or
   names a user with no home. */
static fn expand_leading_tilde(StringView name) throws -> Maybe<String>
{
  if (name.is_empty() || name[0] != '~') return None;
  let const slash = name.find_character('/');
  let const user = slash.has_value() ? name.substring_of_length(1, *slash - 1)
                                     : name.substring(1);
  Maybe<Path> home =
      user.is_empty() ? os::get_home_directory() : os::get_home_for_user(user);
  if (!home.has_value()) return None;
  let expanded = *home;
  if (slash.has_value()) expanded.push_component(name.substring(*slash + 1));
  return String{expanded.text().view()};
}

fn command_resolves(AnalysisContext &actx, const String &name) throws -> bool
{
  if (name.is_empty()) return false;
  if (search_builtin(name.view()).has_value()) return true;
  /* The analysis prepass runs only in the default mood, which is exactly where
     a coreutil falls back to its shitbox implementation, so a shitbox utility
     name resolves here even when PATH has no binary of that name. */
  if (shitbox::find_util(name.view()).has_value()) return true;
  if (name.find_character('/').has_value()) {
    /* A leading tilde is expanded first, since the runtime expands it before
       resolving the command. */
    if (let const expanded = expand_leading_tilde(name.view()))
      return Path::canonicalize(expanded->view()).has_value();
    return Path::canonicalize(name.view()).has_value();
  }

  if (const bool *cached = actx.command_resolution_cache.find(name.view())) {
    LOG(Debug, "reusing the cached resolution of '%s'", name.c_str());
    return *cached;
  }

  const bool was_resolved =
      utils::search_program_path(name.view()).count() != 0;
  LOG(Debug, "scanning PATH for '%s', the command was %s", name.c_str(),
      was_resolved ? "found" : "not found");
  actx.command_resolution_cache.set(name.view(), was_resolved);
  return was_resolved;
}

/* A flattened view of a word's bytes paired with whether each byte is an active
   glob metacharacter. Only an unquoted '[' or ']' is active, so a quoted "[" or
   an escaped \[ stays literal and never opens a bracket expression. */
struct glob_scan_byte
{
  char ch;
  bool is_glob_active;
};

fn collect_glob_scan_bytes(const Word &word) throws -> ArrayList<glob_scan_byte>
{
  ArrayList<glob_scan_byte> bytes{heap_allocator()};
  for (let const &segment : word.segments) {
    const bool is_active = segment.has_live_glob_chars();
    for (usize i = 0; i < segment.text.count(); i++) {
      bytes.push(glob_scan_byte{segment.text[i], is_active});
    }
  }
  return bytes;
}

/* A word's bracket expressions are well-formed when every active '[' that opens
   a character class is closed by a later ']'. The scan mirrors the matcher in
   utils::glob_matches, an active '[' that has no closing ']' is a literal
   there, so only a '[' that does open a class and never closes raises an error.
   A lone trailing '[', such as the test command word, opens nothing and stays
   literal. Returns true when malformed. */
fn word_has_malformed_glob_bracket(const Word &word) throws -> bool
{
  const ArrayList<glob_scan_byte> bytes = collect_glob_scan_bytes(word);

  usize position = 0;
  while (position < bytes.count()) {
    if (!(bytes[position].is_glob_active && bytes[position].ch == '[')) {
      position++;
      continue;
    }

    /* A '[' with nothing after it is the last byte of the word, so it cannot
       open a character class and stays literal, like the test command word. */
    usize scan = position + 1;
    if (scan >= bytes.count()) {
      position++;
      continue;
    }

    /* A leading '!' or '^' negates the class, mirroring the matcher which skips
       either one before it scans for the closing ']'. The prepass skipped only
       '^' before, so it rejected a form the matcher accepts. */
    if (scan < bytes.count() &&
        (bytes[scan].ch == '!' || bytes[scan].ch == '^'))
    {
      scan++;
    }

    /* The matcher treats a ']' right after '[' or '[^' as a member, then, when
       no further ']' closes the class, falls back to a literal '[' rather than
       throwing. The prepass keeps that leading ']' in view rather than skipping
       past it, so it both opens and closes the degenerate class and [^] and [!]
       are accepted the way the matcher accepts them. A '[' with no reachable
       ']' at all, such as [abc, stays the unterminated class the matcher's
       caller rejects. */
    bool has_closing_bracket = false;
    for (; scan < bytes.count(); scan++) {
      if (bytes[scan].ch == ']') {
        has_closing_bracket = true;
        break;
      }
    }

    if (!has_closing_bracket) return true;

    position = scan + 1;
  }

  return false;
}

} // namespace

fn analyze_ast(const Expression *root, StringView source,
               const HashSet &known_functions, const HashSet &known_aliases,
               const EvalContext *eval_context, bool errors_are_warnings,
               bool silence_unresolved_commands,
               bool show_optimizer_state) throws -> bool
{
  ASSERT(root != nullptr);

  AnalysisContext actx{source};
  actx.should_treat_errors_as_warnings = errors_are_warnings;
  actx.should_silence_unresolved_commands = silence_unresolved_commands;
  actx.eval_context = eval_context;
  /* One flag drives both the per-decision trace and the located eliminated-node
     dump, so a folded node reports once through the trace path rather than
     needing a second emitter. */
  actx.should_print_optimizer_state = show_optimizer_state;
  actx.should_trace_optimizer = show_optimizer_state;

  /* A leading shebang that names a POSIX shell gates the bashism lints. The
     first line is scanned for a contained 'dash', or for an 'sh' interpreter
     name without 'bash', so '#!/bin/sh', '#!/usr/bin/env dash', and
     '#!/bin/dash' all arm the gate while a bash or shit shebang leaves it
     off. */
  if (source.length >= 2 && source[0] == '#' && source[1] == '!') {
    usize line_end = 0;
    while (line_end < source.length && source[line_end] != '\n')
      line_end++;
    let const first_line = source.substring_of_length(0, line_end);
    let contains_dash = false;
    let contains_bash = false;
    let interpreter_is_sh = false;
    for (usize i = 0; i + 4 <= first_line.length; i++) {
      if (first_line.substring(i).starts_with(StringView{"dash"}))
        contains_dash = true;
      if (first_line.substring(i).starts_with(StringView{"bash"}))
        contains_bash = true;
    }
    /* The interpreter ends the line, so a trailing 'sh' after a slash is the
       sh program name. */
    if (first_line.length >= 2 &&
        first_line.substring(first_line.length - 2) == StringView{"sh"})
    {
      interpreter_is_sh = true;
    }
    if (contains_dash || (interpreter_is_sh && !contains_bash)) {
      actx.shebang_is_posix_sh = true;
    }
  }

  LOG(Debug, "analyzing the ast, the posix sh shebang gate is %s",
      actx.shebang_is_posix_sh ? "armed" : "off");

  /* A function or alias defined by an earlier command resolves, so seed the
     prepass with the names already registered. */
  known_functions.for_each(
      [&actx](StringView name) { actx.defined_functions.add(name); });
  known_aliases.for_each(
      [&actx](StringView name) { actx.known_aliases.add(name); });

  root->analyze(actx, true);

  if (actx.should_trace_optimizer) {
    let summary = String{"summary: "};
    summary.append(utils::uint_to_text(actx.optimizer_folded_arithmetic));
    summary.append(" arithmetic folded, ");
    summary.append(utils::uint_to_text(actx.optimizer_recorded_constants));
    summary.append(" constants recorded, ");
    summary.append(utils::uint_to_text(actx.optimizer_folded_branches));
    summary.append(" branches folded, ");
    summary.append(utils::uint_to_text(actx.optimizer_folded_loops));
    summary.append(" loops folded, ");
    summary.append(utils::uint_to_text(actx.optimizer_eliminated_compounds));
    summary.append(" compounds eliminated");
    actx.trace_optimizer_line(summary.view());
  }

  return !actx.has_fatal;
}

namespace expressions {

IfStatement::IfStatement(SourceLocation location, const Expression *condition,
                         const Expression *then, const Expression *otherwise)
    : Expression(location), m_condition(condition), m_then(then),
      m_otherwise(otherwise)
{
  ASSERT(condition != nullptr);
  ASSERT(then != nullptr);
}

IfStatement::~IfStatement() = default;

hot fn IfStatement::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_then != nullptr);

  const i64 condition = m_condition->evaluate(cxt);
  if (cxt.has_pending_control_flow()) return condition;

  LOG(Debug, "the if condition yielded %lld, running the %s branch",
      static_cast<long long>(condition),
      condition ? "then" : (m_otherwise != nullptr ? "else" : "no"));

  if (condition)
    return m_then->evaluate(cxt);
  else if (m_otherwise != nullptr)
    return m_otherwise->evaluate(cxt);

  return 0;
}

cold fn IfStatement::to_string() const throws -> String { return "If"; }

cold fn IfStatement::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_then != nullptr);

  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[If]\n";
  s += pad + EXPRESSION_AST_INDENT + m_condition->to_ast_string(layer + 1) +
       "\n";
  s += pad + EXPRESSION_AST_INDENT + m_then->to_ast_string(layer + 1);

  if (m_otherwise != nullptr) {
    s += '\n';
    s += pad + pad + "[Else]\n";
    s += pad + EXPRESSION_AST_INDENT + m_otherwise->to_ast_string(layer + 1);
  }

  return s;
}

Command::Command(SourceLocation location) : Expression(location) {}

fn Command::make_async() wontthrow -> void { m_is_async = true; }

pure fn Command::is_async() const wontthrow -> bool { return m_is_async; }

fn Command::set_negated() wontthrow -> void { m_is_negated = true; }

pure fn Command::is_negated() const wontthrow -> bool { return m_is_negated; }

fn Command::set_timed(bool posix_format) wontthrow -> void
{
  m_is_timed = true;
  m_is_time_posix_format = posix_format;
}

pure fn Command::is_timed() const wontthrow -> bool { return m_is_timed; }

pure fn Command::time_uses_posix_format() const wontthrow -> bool
{
  return m_is_time_posix_format;
}

fn Command::set_local_vars(ArrayList<prefix_assignment> &&vars) throws -> void
{
  m_local_vars = steal(vars);
}

pure fn Command::local_vars() const wontthrow
    -> const ArrayList<prefix_assignment> &
{
  return m_local_vars;
}

fn Command::is_assignment() const wontthrow -> bool { return false; }

/* The parser wraps a redirected command in a RedirectedCommand, so a plain
   command node carries no target of its own and the default reports that. A
   node that does take a target overrides this. */
fn Command::redirect_to(usize d, String &f, bool duplicate) throws -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

fn Command::append_to(usize d, String &f, bool duplicate) throws -> void
{
  redirect_to(d, f, duplicate);
}

DummyExpression::DummyExpression(SourceLocation location) : Expression(location)
{}

fn DummyExpression::is_dummy() const wontthrow -> bool { return true; }

fn DummyExpression::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  unused(cxt);
  return 0;
}

cold fn DummyExpression::to_string() const throws -> String { return "Dummy"; }

cold fn DummyExpression::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

cold fn SimpleCommand::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  if (m_args.is_empty() || m_args[0]->raw_string() != "alias") return;

  /* An alias defined anywhere in the input resolves a later use of its name,
     the same way a function does, so the prepass records each alias name before
     the resolution check runs and an in-chunk alias is not flagged as missing.
     The NAME=value operand lexes as an assignment token rather than a word, so
     the name is taken from the raw token text up to the '='. */
  for (usize i = 1; i < m_args.count(); i++) {
    let const text = m_args[i]->raw_string();
    let const equals_position = text.find_character('=');
    if (equals_position.has_value() && *equals_position > 0)
      actx.known_aliases.add(StringView{text.data(), *equals_position});
  }
}

/* The direct test operator a leading ! collapses into, so the SC2335 lint can
   name the shorter form. A negated -eq is -ne, a negated -lt is -ge, and so on
   down the comparison pairs, with the same for = and !=. None for an operator
   with no negated shortcut, where the ! stays. */
cold fn negated_test_operator(StringView op) wontthrow -> Maybe<StringView>
{
  if (op == "-eq") return StringView{"-ne"};
  if (op == "-ne") return StringView{"-eq"};
  if (op == "-lt") return StringView{"-ge"};
  if (op == "-ge") return StringView{"-lt"};
  if (op == "-gt") return StringView{"-le"};
  if (op == "-le") return StringView{"-gt"};
  if (op == "=") return StringView{"!="};
  if (op == "!=") return StringView{"="};
  return shit::None;
}

/* The binary operators of test, used to tell a == sitting in the operator slot
   from a literal == that is the operand of another operator, so the SC3014 lint
   does not flag [ x = == ]. */
cold fn is_test_binary_operator_word(StringView op) wontthrow -> bool
{
  return op == "=" || op == "==" || op == "!=" || op == "<" || op == ">" ||
         op == "-eq" || op == "-ne" || op == "-lt" || op == "-le" ||
         op == "-gt" || op == "-ge" || op == "-ef" || op == "-nt" ||
         op == "-ot";
}

/* The numeric comparison operators of test, where a non-numeric literal
   operand always errors at run time, the SC2170 lint. */
cold fn is_test_numeric_operator_word(StringView op) wontthrow -> bool
{
  return op == "-eq" || op == "-ne" || op == "-lt" || op == "-le" ||
         op == "-gt" || op == "-ge";
}

cold fn word_is_fully_literal(const Word &word) wontthrow -> bool
{
  for (let const &segment : word.segments)
    if (segment.kind != WordSegment::Kind::LiteralText &&
        segment.kind != WordSegment::Kind::UnquotedText &&
        segment.kind != WordSegment::Kind::DoubleQuotedText)
    {
      return false;
    }
  return true;
}

cold pure fn view_is_integer_literal(StringView view) wontthrow -> bool
{
  usize start = view.length >= 1 && view[0] == '-' ? 1 : 0;
  return start < view.length && view.substring(start).is_all_decimal_digits();
}

cold pure fn view_contains(StringView view, StringView needle) wontthrow -> bool
{
  if (needle.length == 0 || needle.length > view.length) return false;
  for (usize i = 0; i + needle.length <= view.length; i++)
    if (view.substring(i).starts_with(needle)) return true;
  return false;
}

cold fn args_have_stdin_operand(const ArrayList<const Token *> &args) throws
    -> bool
{
  for (usize i = 1; i < args.count(); i++) {
    let const raw = args[i]->raw_string();
    if (raw.view() == "-" || raw.view() == "/dev/stdin") return true;
  }
  return false;
}

cold fn args_have_short_flag(const ArrayList<const Token *> &args,
                             char letter) throws -> bool
{
  for (usize i = 1; i < args.count(); i++) {
    if (args[i]->kind() != Token::Kind::Word) continue;
    let const literal = static_cast<const tokens::WordToken *>(args[i])
                            ->word()
                            .to_literal_string();
    let const view = literal.view();
    if (view.length >= 2 && view[0] == '-' && view[1] != '-' &&
        view.find_character(letter).has_value())
    {
      return true;
    }
  }
  return false;
}

/* The commands that never read stdin, so a pipe or an input redirect into one
   of them silently discards the upstream data, shellcheck SC2216 for the pipe
   and SC2217 for the redirect. The value is unused, the membership is the
   answer. */
constexpr StaticStringMap<bool>::entry NON_STDIN_READER_ENTRIES[] = {
    {SSK("rm"),       true},
    {SSK("echo"),     true},
    {SSK("printf"),   true},
    {SSK("true"),     true},
    {SSK("false"),    true},
    {SSK("mkdir"),    true},
    {SSK("rmdir"),    true},
    {SSK("touch"),    true},
    {SSK("chmod"),    true},
    {SSK("chown"),    true},
    {SSK("cp"),       true},
    {SSK("mv"),       true},
    {SSK("ln"),       true},
    {SSK("kill"),     true},
    {SSK("basename"), true},
    {SSK("dirname"),  true},
    {SSK("sleep"),    true},
    {SSK("unlink"),   true},
};
constexpr StaticStringMap<bool> NON_STDIN_READERS{
    NON_STDIN_READER_ENTRIES,
    sizeof(NON_STDIN_READER_ENTRIES) / sizeof(NON_STDIN_READER_ENTRIES[0])};

/* The top-level system directories rm -r must never aim at, the SC2114
   table. */
constexpr StaticStringMap<bool>::entry SYSTEM_DIRECTORY_ENTRIES[] = {
    {SSK("/"),     true},
    {SSK("/bin"),  true},
    {SSK("/boot"), true},
    {SSK("/dev"),  true},
    {SSK("/etc"),  true},
    {SSK("/home"), true},
    {SSK("/lib"),  true},
    {SSK("/proc"), true},
    {SSK("/root"), true},
    {SSK("/sbin"), true},
    {SSK("/sys"),  true},
    {SSK("/usr"),  true},
    {SSK("/var"),  true},
};
constexpr StaticStringMap<bool> SYSTEM_DIRECTORIES{
    SYSTEM_DIRECTORY_ENTRIES,
    sizeof(SYSTEM_DIRECTORY_ENTRIES) / sizeof(SYSTEM_DIRECTORY_ENTRIES[0])};

cold fn SimpleCommand::analyze(AnalysisContext &actx,
                               bool is_unconditional) const throws -> void
{
  unused(is_unconditional);

  /* A constant $((...)) in any argument or assignment prefix is folded once by
     the constant-arithmetic rule, so the loop body that re-runs this command
     does not re-parse it. The rule reads the constant table, so the fold runs
     while the recorded values still hold for this command. */
  optimizer::optimize_node(this, actx);

  if (m_args.is_empty()) return;

  ASSERT(m_args[0] != nullptr);
  let const name = static_command_name(m_args[0]);

  /* local, declare, and typeset name the variables that stay inside the
     function, so the names they take are recorded and the leak warning stays
     quiet for a later assignment to one of them. A leading flag such as -A is
     skipped, and the name ends at an = or a [ subscript. */
  if (actx.function_scope_depth > 0 &&
      (name == "local" || name == "declare" || name == "typeset"))
  {
    for (usize i = 1; i < m_args.count(); i++) {
      let const word = m_args[i]->kind() == Token::Kind::Word
                           ? static_cast<const tokens::WordToken *>(m_args[i])
                                 ->word()
                                 .to_literal_string()
                           : m_args[i]->raw_string();
      const StringView text = word.view();
      if (text.is_empty() || text[0] == '-') continue;
      usize end = 0;
      while (end < text.length && lexer::is_variable_name(text[end]))
        end++;
      if (end > 0)
        actx.function_local_names.add(text.substring_of_length(0, end));
    }
  }

  /* The literal command text, used for the test recognition. A name like [
     holds a glob metacharacter, so static_command_name rejects it, but the test
     check still needs to see it. */
  let const command_literal =
      m_args[0]->kind() == Token::Kind::Word
          ? static_cast<const tokens::WordToken *>(m_args[0])
                ->word()
                .to_literal_string()
          : m_args[0]->raw_string();

  /* A user-defined function or alias of a builtin name runs that user code, not
     the builtin, so a lint that keys on the builtin name must stay quiet here.
     This is the predicate the missing-command check below also reads. */
  let const command_is_shadowed =
      actx.defined_functions.contains(command_literal.view()) ||
      actx.known_aliases.contains(command_literal.view());

  /* A dot, source, eval, or alias runs or defines code the prepass cannot see,
     so any later unresolved command must not be a hard failure. */
  if (command_literal == "." || command_literal == "source" ||
      command_literal == "eval" || command_literal == "alias")
  {
    LOG(Debug,
        "'%s' may define commands at run time, later resolution failures "
        "degrade to warnings",
        command_literal.c_str());
    actx.has_seen_runtime_definer = true;
  }

  /* A funsub argument, ${ ...; }, runs its body in the current shell, so a
     function it defines persists where the prepass cannot see it, the same
     reason eval degrades a later unresolved command to a warning. */
  for (let const t : m_args) {
    if (t->kind() != Token::Kind::Word) continue;
    let const &word = static_cast<const tokens::WordToken *>(t)->word();
    for (let const &segment : word.segments) {
      if (segment.kind == WordSegment::Kind::FunctionSubstitution) {
        actx.has_seen_runtime_definer = true;
        break;
      }
    }
  }

  /* A glob pattern with an unterminated bracket expression can never compile
     into a matcher, so the expansion would throw at run time. The malformed
     pattern is visible from the word bytes alone, so the prepass rejects it
     here at the located word. */
  for (let const t : m_args) {
    if (t->kind() != Token::Kind::Word) continue;
    let const &word = static_cast<const tokens::WordToken *>(t)->word();
    if (word_has_malformed_glob_bracket(word)) {
      actx.fail(t->source_location(),
                "Malformed glob pattern, unterminated '['");
    }
  }

  /* An unquoted variable inside a test silently breaks when it is empty or
     splits into several words. This stays a warning even at the strict default,
     since the split may be intended, and POSIX mode skips the analysis entirely
     so a POSIX script that relies on it runs quietly. */
  if (command_literal == "[" || command_literal == "test" ||
      command_literal == "[[")
  {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      for (let const &segment : word.segments) {
        if (segment.kind == WordSegment::Kind::VariableReference &&
            segment.is_split_eligible())
        {
          actx.warn(m_args[i]->source_location(),
                    "A test reads an unquoted variable",
                    "Quote it to avoid an empty or split argument");
          break;
        }
      }
    }
  }

  /* read without -r lets a backslash in the input escape the next byte, so a
     path or a line with a backslash is mangled. This is shellcheck SC2162. A
     combined short flag such as -rs carries the r, a long option or a value
     does not. A function or alias named read is the user's own, so the lint is
     off for it. */
  if (command_literal == "read" && !command_is_shadowed &&
      !args_have_short_flag(m_args, 'r'))
  {
    actx.warn(source_location(),
              "A read without -r mangles a backslash in the input",
              "Add -r to read the line literally");
  }

  /* The bashism lints, each fired only when the shebang names a POSIX shell so
     a deliberately bash-shaped script stays quiet. echo with a -e, -n, or -E
     flag relies on a bash builtin where the POSIX echo prints the flag as
     text, shellcheck SC3037, use printf. declare and its typeset alias are not
     in POSIX, shellcheck SC3044, assign plainly or use a function. source is
     the bash spelling of the dot command, shellcheck SC3046, use '.'. */
  if (actx.shebang_is_posix_sh && !command_is_shadowed) {
    if (command_literal == "echo" && m_args.count() >= 2 &&
        m_args[1]->kind() == Token::Kind::Word)
    {
      let const flag = static_cast<const tokens::WordToken *>(m_args[1])
                           ->word()
                           .to_literal_string();
      let const view = flag.view();
      if (view == "-e" || view == "-n" || view == "-E" || view == "-ne" ||
          view == "-en")
      {
        actx.warn(m_args[1]->source_location(),
                  "An echo " + view +
                      " relies on a bash builtin, the POSIX echo prints the "
                      "flag as text",
                  "Use printf instead under a sh shebang");
      }
    }
    if (command_literal == "declare" || command_literal == "typeset")
      actx.warn(m_args[0]->source_location(),
                StringView{"The "} + command_literal.view() +
                    " builtin is not in POSIX",
                "Assign the variable plainly under a sh shebang, or switch the "
                "shebang to bash");
    if (command_literal == "source")
      actx.warn(m_args[0]->source_location(),
                "The name source is the bash spelling, the POSIX dot command "
                "is '.'",
                "Use '.' under a sh shebang");
    if (command_literal == "local")
      actx.warn(m_args[0]->source_location(),
                "The local builtin is not in POSIX sh, the value stays global",
                "rework the function or switch the shebang to bash");
    if (command_literal == "printf" && m_args.count() >= 2 &&
        m_args[1]->kind() == Token::Kind::Word &&
        static_cast<const tokens::WordToken *>(m_args[1])
                ->word()
                .to_literal_string()
                .view() == "-v")
    {
      actx.warn(m_args[1]->source_location(),
                "The printf -v form is a bash extension, the POSIX printf "
                "has no -v",
                "capture the output with a command substitution under a sh "
                "shebang");
    }
    /* mapfile and its readarray alias are bash array builtins with no POSIX
       counterpart. This is shellcheck SC3030, read the input with a while
       read loop under a sh shebang. */
    if (command_literal == "mapfile" || command_literal == "readarray")
      actx.warn(m_args[0]->source_location(),
                command_literal.view() +
                    " is a bash array builtin absent from POSIX sh",
                "read the input with a while read loop or switch the shebang "
                "to bash");
  }

  /* The deprecated-tool and native-form lints. egrep and fgrep are deprecated
     by GNU grep, shellcheck SC2196 and SC2197, use grep -E and grep -F. expr
     forks for arithmetic the shell does natively, shellcheck SC2003, use
     $((...)). let runs arithmetic as a command, shellcheck SC2219, use the
     ((...)) compound. local outside a function has no scope to bind,
     shellcheck SC2168. echo of a single command substitution is redundant,
     shellcheck SC2005, run the command on its own. */
  if (!command_is_shadowed) {
    if (command_literal == "egrep")
      actx.warn(m_args[0]->source_location(), "The egrep command is deprecated",
                "Use grep -E for the extended regular expression match");
    else if (command_literal == "fgrep")
      actx.warn(m_args[0]->source_location(), "The fgrep command is deprecated",
                "Use grep -F for the fixed string match");
    else if (command_literal == "expr")
      actx.warn(m_args[0]->source_location(),
                "An expr forks for arithmetic the shell does natively",
                "Use $((...)) for the calculation");
    else if (command_literal == "let")
      actx.warn(m_args[0]->source_location(),
                "A let runs arithmetic as a command",
                "Use the ((...)) compound so the operands need no quoting");
    else if (command_literal == "local" && actx.function_scope_depth == 0)
      actx.warn(m_args[0]->source_location(),
                "A local outside a function has no scope to bind",
                "Declare the variable plainly or move it into a function");
  }

  if (command_literal == "echo" && !command_is_shadowed &&
      m_args.count() == 2 && m_args[1]->kind() == Token::Kind::Word)
  {
    let const &word = static_cast<const tokens::WordToken *>(m_args[1])->word();
    if (word.segments.count() == 1 &&
        word.segments[0].kind == WordSegment::Kind::CommandSubstitution)
    {
      actx.warn(m_args[0]->source_location(),
                "An echo of a command substitution prints what the command "
                "already prints",
                "Run the command on its own instead");
    }
  }

  /* A trap action in double quotes expands its variables and command
     substitutions when the trap is set, not when it fires, so the action
     captures the values at set time. This is shellcheck SC2064, single-quote
     the action so it expands when the signal arrives. The action is the first
     operand. */
  if (command_literal == "trap" && !command_is_shadowed &&
      m_args.count() >= 2 && m_args[1]->kind() == Token::Kind::Word)
  {
    let const &action =
        static_cast<const tokens::WordToken *>(m_args[1])->word();
    let action_expands_now = false;
    for (let const &segment : action.segments)
      if (segment.is_in_double_quotes &&
          (segment.kind == WordSegment::Kind::VariableReference ||
           segment.kind == WordSegment::Kind::CommandSubstitution))
      {
        action_expands_now = true;
        break;
      }
    if (action_expands_now)
      actx.warn(m_args[1]->source_location(),
                "The double-quoted trap action expands now, when the trap is "
                "set, not when it fires",
                "Single-quote it so it expands as the signal arrives");
  }

  /* printf reads its format argument as the template, so a variable or a
     command substitution there lets the data control the format directives.
     This is shellcheck SC2059, write printf '%s' \"$var\" instead. The format
     is the first non-option word, so a leading -v or other dash run is skipped
     and a -- ends the options and forces the next word as the format, which
     keeps printf -- "$fmt" inspected. A function or alias named printf is the
     user's own, so the lint is off for it. */
  if (command_literal == "printf" && !command_is_shadowed) {
    usize format_index = 0;
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) {
        /* A non-literal word can be neither -- nor an option to skip, so it is
           the format. */
        format_index = i;
        break;
      }
      let const literal = static_cast<const tokens::WordToken *>(m_args[i])
                              ->word()
                              .to_literal_string();
      let const view = literal.view();
      if (view == "--") {
        /* The options end here, so the word after -- is the format. */
        if (i + 1 < m_args.count()) format_index = i + 1;
        break;
      }
      if (!(view.length >= 1 && view[0] == '-')) {
        format_index = i;
        break;
      }
    }

    if (format_index != 0 && m_args[format_index]->kind() == Token::Kind::Word)
    {
      let const &format =
          static_cast<const tokens::WordToken *>(m_args[format_index])->word();
      bool format_has_expansion = false;
      for (let const &segment : format.segments) {
        if (segment.kind == WordSegment::Kind::VariableReference ||
            segment.kind == WordSegment::Kind::CommandSubstitution)
        {
          format_has_expansion = true;
          break;
        }
      }
      if (format_has_expansion)
        actx.warn(m_args[format_index]->source_location(),
                  "The printf format comes from a variable, the data can "
                  "inject format directives",
                  "Use printf '%s' to print it");
    }
  }

  /* which is not in POSIX and its output and exit status vary across systems,
     so command -v is the portable lookup. This is shellcheck SC2230. A function
     or alias named which is the user's own, so the lint is off for it. */
  if (command_literal == "which" && !command_is_shadowed) {
    actx.warn(m_args[0]->source_location(), "The which command is non-standard",
              "Use command -v for a portable lookup");
  }

  /* An unquoted command substitution splits its captured output on IFS and
     globs each field, so a path with a space or a glob character breaks into
     several arguments. This is shellcheck SC2046, quote the substitution to
     keep it one argument. An assignment-builtin operand such as export
     FOO=$(cmd) does not split in assignment context, so it is left alone. */
  let const command_is_assignment_builtin =
      command_literal == "export" || command_literal == "readonly" ||
      command_literal == "local" || command_literal == "declare" ||
      command_literal == "typeset";

  /* A declaration builtin that assigns from a command substitution, such as
     local x=$(cmd), reports the builtin's own success rather than the
     command's exit status, so a failing cmd looks like it succeeded. This is
     shellcheck SC2155, declare on one line and assign on the next so the
     status is seen. The value rides an Assignment token. */
  if (command_is_assignment_builtin && !command_is_shadowed)
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Assignment) continue;
      let const &value =
          static_cast<const tokens::Assignment *>(m_args[i])->value_word();
      let value_has_substitution = false;
      for (let const &segment : value.segments)
        if (segment.kind == WordSegment::Kind::CommandSubstitution) {
          value_has_substitution = true;
          break;
        }
      if (!value_has_substitution) continue;
      actx.warn(m_args[i]->source_location(),
                "Declaring and assigning from a command substitution in one "
                "command masks the command's exit status",
                "Split the declaration and the assignment so a failure is "
                "seen");
      break;
    }

  for (usize i = 1; i < m_args.count(); i++) {
    if (m_args[i]->kind() != Token::Kind::Word) continue;
    let const &word = static_cast<const tokens::WordToken *>(m_args[i])->word();
    bool word_has_unquoted_command_substitution = false;
    for (let const &segment : word.segments) {
      if (segment.kind == WordSegment::Kind::CommandSubstitution &&
          !segment.is_in_double_quotes)
      {
        word_has_unquoted_command_substitution = true;
        break;
      }
    }
    if (!word_has_unquoted_command_substitution) continue;
    /* An assignment-builtin operand such as export FOO=$(cmd) does not split in
       assignment context, so the substitution there is left alone. This split
       check allocates, so it runs only for a word that actually carries an
       unquoted substitution. */
    if (command_is_assignment_builtin &&
        word.get_assignment_split().has_value())
    {
      continue;
    }
    actx.warn(m_args[i]->source_location(),
              "An unquoted command substitution splits its output",
              "Quote it to keep one argument");
  }

  /* rm -r with a "$var/" operand deletes / outright when the variable is
     empty, since only the slash remains. This is shellcheck SC2115, the
     ${var:?} form aborts on the empty value instead. A literal operand naming
     a top-level system directory is shellcheck SC2114. */
  if (command_literal == "rm" && !command_is_shadowed &&
      args_have_short_flag(m_args, 'r'))
  {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      if (word.segments.count() >= 2 &&
          word.segments[0].kind == WordSegment::Kind::VariableReference &&
          !word.segments[0].text.view().find_character(':').has_value() &&
          !word.segments[1].text.is_empty() && word.segments[1].text[0] == '/')
      {
        actx.warn(m_args[i]->source_location(),
                  "A rm -r on \"$" + word.segments[0].text.view() +
                      "/\" deletes '/' when the variable is empty",
                  StringView{"write ${"} + word.segments[0].text.view() +
                      ":?} so an empty value aborts the command instead");
      }
      if (word_is_fully_literal(word)) {
        let const literal = word.to_literal_string();
        if (SYSTEM_DIRECTORIES.find(literal.view()).has_value())
          actx.warn(m_args[i]->source_location(),
                    "A rm -r targets the system directory '" + literal.view() +
                        "'",
                    "double-check the path before running this");
      }
    }
  }

  /* The grep pattern lints. An unquoted pattern with a glob metacharacter can
     expand against the local files before grep ever sees it, shellcheck
     SC2062. A quoted pattern that starts with * looks like a glob, but grep
     reads a regular expression where a leading * has nothing to repeat,
     shellcheck SC2063. The pattern is the first word past the options. */
  if ((command_literal == "grep" || command_literal == "egrep" ||
       command_literal == "fgrep") &&
      !command_is_shadowed)
  {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      let const literal = word.to_literal_string();
      let const view = literal.view();
      if (view.length >= 1 && view[0] == '-') continue;
      if (word.segments.count() == 1 &&
          word.segments[0].kind == WordSegment::Kind::UnquotedText &&
          word.segments[0].has_glob_metacharacter())
      {
        actx.warn(m_args[i]->source_location(),
                  "The unquoted grep pattern can glob against the local files "
                  "before grep sees it",
                  "Quote the pattern");
      } else if (!view.is_empty() && view[0] == '*') {
        actx.warn(m_args[i]->source_location(),
                  "A grep reads a regular expression, where a leading * has "
                  "nothing to repeat, this pattern looks like a glob");
      }
      break;
    }
  }

  /* mkdir -p with -m applies the mode only to the deepest directory, the
     parents take the umask default. This is shellcheck SC2174. */
  if (command_literal == "mkdir" && !command_is_shadowed &&
      args_have_short_flag(m_args, 'p') && args_have_short_flag(m_args, 'm'))
  {
    actx.warn(m_args[0]->source_location(),
              "A mkdir -pm applies the mode only to the deepest directory, "
              "the "
              "created parents keep the umask default");
  }

  /* An exit or return code outside the literal 0-255 integer shape either
     errors at run time or wraps modulo 256. This is shellcheck SC2242. */
  if ((command_literal == "exit" || command_literal == "return") &&
      !command_is_shadowed && m_args.count() >= 2 &&
      m_args[1]->kind() == Token::Kind::Word)
  {
    let const &operand =
        static_cast<const tokens::WordToken *>(m_args[1])->word();
    if (word_is_fully_literal(operand)) {
      let const literal = operand.to_literal_string();
      let const view = literal.view();
      let is_in_range = view_is_integer_literal(view) && view[0] != '-';
      if (is_in_range) {
        let const parsed_code = utils::parse_decimal_integer(view);
        is_in_range = !parsed_code.is_error() && parsed_code.value() <= 255;
      }
      if (!is_in_range)
        actx.warn(m_args[1]->source_location(),
                  "The code '" + view + "' is not a number from 0 to 255, " +
                      command_literal.view() +
                      " either rejects it or wraps it modulo 256");
    }
  }

  /* The $@ word lints that need no array tracking. A bare $@ word-splits and
     globs every argument, shellcheck SC2068, quote it as "$@". A $@ mixed
     into a longer word concatenates the arguments around the neighboring text
     unpredictably, shellcheck SC2145. The [[ form gets SC2199 below. */
  for (usize i = command_literal == "[[" ? m_args.count() : 1;
       i < m_args.count(); i++)
  {
    if (m_args[i]->kind() != Token::Kind::Word) continue;
    let const &word = static_cast<const tokens::WordToken *>(m_args[i])->word();
    for (let const &segment : word.segments) {
      if (segment.kind != WordSegment::Kind::VariableReference ||
          segment.text.view() != "@")
      {
        continue;
      }
      if (word.segments.count() == 1 && !segment.is_in_double_quotes) {
        actx.warn(m_args[i]->source_location(),
                  "An unquoted $@ word-splits and globs each argument",
                  "Quote it as \"$@\" to pass the arguments through unchanged");
      } else if (word.segments.count() > 1) {
        actx.warn(m_args[i]->source_location(),
                  "$@ inside a longer word concatenates the surrounding text "
                  "onto the first and last argument",
                  "Use $* for one joined string or a separate \"$@\" word");
      }
      break;
    }
  }

  /* A command substitution that only echoes runs a subshell to produce text
     the caller already has. This is shellcheck SC2116, drop the $(echo ...)
     wrapper. A body carrying an operator runs more than the echo, so it is
     left alone. */
  for (usize i = 0; i < m_args.count(); i++) {
    if (m_args[i]->kind() != Token::Kind::Word) continue;
    let const &word = static_cast<const tokens::WordToken *>(m_args[i])->word();
    for (let const &segment : word.segments) {
      if (segment.kind != WordSegment::Kind::CommandSubstitution) continue;
      let const body = segment.text.view();
      usize start = 0;
      while (start < body.length && (body[start] == ' ' || body[start] == '\t'))
        start++;
      let const trimmed = body.substring(start);
      if (!trimmed.starts_with(StringView{"echo "}) && trimmed != "echo")
        continue;
      let body_runs_more_than_echo = false;
      for (usize b = 0; b < trimmed.length; b++)
        if (trimmed[b] == '|' || trimmed[b] == ';' || trimmed[b] == '&' ||
            trimmed[b] == '<' || trimmed[b] == '>' || trimmed[b] == '`')
        {
          body_runs_more_than_echo = true;
          break;
        }
      if (!body_runs_more_than_echo)
        actx.warn(m_args[i]->source_location(),
                  "A command substitution wraps a useless echo",
                  "The text can be used directly without the subshell");
    }
  }

  /* The redirection lints. 2>&1 written before the stdout file redirect
     duplicates the still-unredirected stdout, so stderr keeps going to the
     terminal, shellcheck SC2069, write the file redirect first. Reading and
     truncating the same file in one command destroys the input before it is
     read, shellcheck SC2094. An input redirect into a command that never
     reads stdin discards the data, shellcheck SC2217. */
  {
    let saw_stderr_to_stdout = false;
    /* The read target is held as an owned String, not a view, since the view
       of a to_literal_string() temporary would dangle past the statement. */
    String read_target{};
    const Token *read_token = nullptr;
    for (let const &redirection : m_redirections) {
      if (redirection.kind == Redirection::Kind::DuplicateOutput &&
          redirection.fd == 2 && redirection.dup_fd == 1)
      {
        saw_stderr_to_stdout = true;
        continue;
      }
      let const is_file_output =
          redirection.kind == Redirection::Kind::TruncateOutput ||
          redirection.kind == Redirection::Kind::TruncateOutputOverride;
      if (is_file_output && redirection.fd == 1 && saw_stderr_to_stdout &&
          redirection.target != nullptr)
      {
        actx.warn(redirection.target->source_location(),
                  "2>&1 before the file redirect duplicates the terminal, so "
                  "stderr stays on the terminal",
                  "Put the file redirect first as in '>file 2>&1'");
      }
      if (redirection.kind == Redirection::Kind::ReadInput &&
          redirection.target != nullptr &&
          redirection.target->kind() == Token::Kind::Word)
      {
        read_target = static_cast<const tokens::WordToken *>(redirection.target)
                          ->word()
                          .to_literal_string();
        read_token = redirection.target;
      }
      if (is_file_output && redirection.target != nullptr &&
          redirection.target->kind() == Token::Kind::Word &&
          read_token != nullptr)
      {
        let const write_target =
            static_cast<const tokens::WordToken *>(redirection.target)
                ->word()
                .to_literal_string();
        if (!read_target.is_empty() &&
            write_target.view() == read_target.view())
        {
          actx.warn(redirection.target->source_location(),
                    "The command reads and truncates '" + read_target.view() +
                        "' at once, the truncation empties the input before "
                        "it is read",
                    "Write to a temporary and move it over");
        }
      }
    }

    if (!m_redirections.is_empty() && !command_is_shadowed &&
        NON_STDIN_READERS.find(command_literal.view()).has_value())
    {
      if (!args_have_stdin_operand(m_args))
        for (let const &redirection : m_redirections)
          if (redirection.kind == Redirection::Kind::ReadInput ||
              redirection.kind == Redirection::Kind::Heredoc ||
              redirection.kind == Redirection::Kind::HereString)
          {
            actx.warn(m_args[0]->source_location(),
                      "The input redirect feeds '" + command_literal.view() +
                          "', which never reads stdin, so the data is "
                          "discarded");
            break;
          }
    }
  }

  /* Obsolescent or redundant test forms, each a shellcheck check. -a and -o
     joining two conditions is obsolescent and misparses with some operands, so
     prefer a separate test joined with && or || (SC2166). The operator is
     binary only past the first operand, so a unary -a file test does not trip
     it, and a -a right after a ! is the negated unary file test rather than the
     binary AND. A negated -z or -n has a direct operator, -n or -z (SC2236,
     SC2237). A function or alias named test or [ is the user's own, so the lint
     is off for it. */
  if ((command_literal == "[" || command_literal == "test" ||
       command_literal == "[[") &&
      !command_is_shadowed)
  {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const literal = static_cast<const tokens::WordToken *>(m_args[i])
                              ->word()
                              .to_literal_string();
      let const view = literal.view();
      /* The literal of the word before this one, used to tell == in the
         operator slot from a literal == operand, and a negated unary -a file
         test from the binary AND operator. Empty for a non-word predecessor. */
      let const previous_literal =
          m_args[i - 1]->kind() == Token::Kind::Word
              ? static_cast<const tokens::WordToken *>(m_args[i - 1])
                    ->word()
                    .to_literal_string()
              : String{};
      /* == is a bashism in test, POSIX test compares strings with =. This is
         shellcheck SC3014, warned only when == sits in the operator slot, after
         a plain operand rather than as the first operand or the right side of
         another operator, so [ x = == ] comparing the literal == is left alone.
         The test builtin rejects an operator == at run time with the same
         suggestion. */
      if (view == "==" && i >= 2 &&
          !is_test_binary_operator_word(previous_literal.view()))
      {
        actx.warn(m_args[i]->source_location(), "== is undefined in POSIX test",
                  "Use = for string equality");
      }
      let const previous_is_bang = previous_literal.view() == "!";
      if (i >= 2 && !previous_is_bang && (view == "-a" || view == "-o")) {
        actx.warn(m_args[i]->source_location(),
                  "A test with -a or -o is obsolescent",
                  "Join two tests with && or || instead");
      } else if (view == "!" && i + 1 < m_args.count() &&
                 m_args[i + 1]->kind() == Token::Kind::Word)
      {
        let const next = static_cast<const tokens::WordToken *>(m_args[i + 1])
                             ->word()
                             .to_literal_string();
        if (next.view() == "-z") {
          actx.warn(m_args[i]->source_location(), "A negated -z is just -n",
                    "Test with -n instead");
        } else if (next.view() == "-n") {
          actx.warn(m_args[i]->source_location(), "A negated -n is just -z",
                    "Test with -z instead");
        } else if (i + 2 < m_args.count() &&
                   m_args[i + 2]->kind() == Token::Kind::Word)
        {
          /* The ! X OP Y shape, where OP is a comparison with a direct negated
             form. This is shellcheck SC2335, drop the ! and use the inverse
             operator so the test reads without the negation. */
          let const op = static_cast<const tokens::WordToken *>(m_args[i + 2])
                             ->word()
                             .to_literal_string();
          let const inverse = negated_test_operator(op.view());
          if (inverse.has_value()) {
            actx.warn(m_args[i]->source_location(),
                      StringView{"A negated "} + op + " is just " +
                          inverse.value(),
                      StringView{"Drop the ! and use "} + inverse.value());
          }
        }
      }
    }
  }

  /* A test with a single operand and no operator is the nonempty-string test,
     which reads clearer written with -n. This is shellcheck SC2244. The [ form
     ends at the closing ], the test form runs to the end, and an operand that
     looks like a flag is left alone so [ -n ] is not told to use -n. A function
     or alias named test or [ is the user's own, so the lint is off for it. */
  if ((command_literal == "[" || command_literal == "test" ||
       command_literal == "[[") &&
      !command_is_shadowed)
  {
    usize operand_end = m_args.count();
    bool bracket_form_is_closed = true;
    if (command_literal == "[" || command_literal == "[[") {
      bracket_form_is_closed =
          m_args.count() >= 2 &&
          m_args[m_args.count() - 1]->kind() == Token::Kind::Word &&
          static_cast<const tokens::WordToken *>(m_args[m_args.count() - 1])
                  ->word()
                  .to_literal_string()
                  .view() == (command_literal == "[" ? "]" : "]]");
      if (bracket_form_is_closed) operand_end = m_args.count() - 1;
    }
    if (bracket_form_is_closed && operand_end == 2 &&
        m_args[1]->kind() == Token::Kind::Word)
    {
      let const operand = static_cast<const tokens::WordToken *>(m_args[1])
                              ->word()
                              .to_literal_string();
      if (!(operand.view().length >= 1 && operand.view()[0] == '-')) {
        actx.warn(m_args[1]->source_location(),
                  "A one-operand test is the nonempty-string test",
                  "Write it with -n to read clearer");
      }
    }

    /* The operand-shape lints over the closed operand range. A -z or -n on a
       fully literal operand is constant, shellcheck SC2157. A numeric
       comparison against a non-numeric literal always errors at run time,
       shellcheck SC2170. A = or == against a literal with a glob character
       reads like a pattern match the [ and test commands never do, shellcheck
       SC2081. A grep inside a test substitution buffers the whole output
       where grep -q answers on the first match, shellcheck SC2143. */
    for (usize i = 1; i < operand_end; i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      let const literal = word.to_literal_string();
      let const view = literal.view();

      if ((view == "-z" || view == "-n") && i + 1 < operand_end &&
          m_args[i + 1]->kind() == Token::Kind::Word)
      {
        let const &next =
            static_cast<const tokens::WordToken *>(m_args[i + 1])->word();
        if (word_is_fully_literal(next))
          actx.warn(m_args[i + 1]->source_location(),
                    "The operand is a literal, so this " + view +
                        " test is constant",
                    "Test a variable or drop the check");
      }

      if (is_test_numeric_operator_word(view)) {
        for (usize side = i - 1; side <= i + 1; side += 2) {
          if (side >= operand_end || m_args[side]->kind() != Token::Kind::Word)
            continue;
          let const &operand =
              static_cast<const tokens::WordToken *>(m_args[side])->word();
          if (!word_is_fully_literal(operand)) continue;
          let const operand_literal = operand.to_literal_string();
          if (!view_is_integer_literal(operand_literal.view()))
            actx.warn(m_args[side]->source_location(),
                      "The numeric comparison " + view + " reads '" +
                          operand_literal.view() +
                          "', which is not a number, so the test errors at "
                          "run time");
        }
      }

      if (command_literal != "[[" && (view == "=" || view == "==") &&
          i + 1 < operand_end && m_args[i + 1]->kind() == Token::Kind::Word)
      {
        let const &right =
            static_cast<const tokens::WordToken *>(m_args[i + 1])->word();
        if (word_is_fully_literal(right)) {
          let const right_literal = right.to_literal_string();
          if (right_literal.view().find_character('*').has_value() ||
              right_literal.view().find_character('?').has_value())
          {
            actx.warn(m_args[i + 1]->source_location(),
                      "[ and test compare strings byte for byte and never "
                      "glob-match",
                      "Use a case or the [[ ]] form for the pattern");
          }
        }
      }

      for (let const &segment : word.segments) {
        if (segment.kind != WordSegment::Kind::CommandSubstitution) continue;
        if (view_contains(segment.text.view(), StringView{"grep"}) &&
            !view_contains(segment.text.view(), StringView{"grep -c"}))
        {
          actx.warn(m_args[i]->source_location(),
                    "The test buffers the whole grep output only to check it "
                    "is nonempty",
                    "Run grep -q directly and test its exit status");
          break;
        }
      }

      /* A test against $? checks the exit status indirectly, where the command
         can be tested directly with if or &&. This is shellcheck SC2181, which
         also avoids a $? clobbered by an intervening command. */
      if (word.segments.count() == 1 &&
          word.segments[0].kind == WordSegment::Kind::VariableReference &&
          word.segments[0].text.view() == "?")
      {
        actx.warn(m_args[i]->source_location(),
                  "Testing $? checks the exit status indirectly",
                  "Test the command directly with if or && so an intervening "
                  "command cannot clobber the status");
      }
    }
  }

  /* A prefix assignment does not affect the expansion on the same command, so a
     reference to one of its names reads the old value. */
  if (m_local_vars.count() > 0) {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      for (let const &segment : word.segments) {
        if (segment.kind != WordSegment::Kind::VariableReference) continue;
        const StringView referenced{segment.text.data(), segment.text.count()};
        bool does_name_a_prefix = false;
        for (let const &var : m_local_vars) {
          if (var.name.view() == referenced) {
            does_name_a_prefix = true;
            break;
          }
        }
        if (does_name_a_prefix) {
          let const message =
              StringView{"The assignment prefix does not affect this "
                         "command, '"} +
              segment.text + StringView{"' is read before it is set"};
          actx.warn(m_args[i]->source_location(), message);
          break;
        }
      }
    }
  }

  if (name && !actx.should_silence_unresolved_commands &&
      !command_resolves(actx, *name) &&
      !actx.defined_functions.contains(
          StringView{name->data(), name->count()}) &&
      !actx.known_aliases.contains(StringView{name->data(), name->count()}))
  {
    let const message = StringView{"Command '"} + StringView{*name} +
                        StringView{"' was not found"};
    /* A close function, alias, builtin, or PATH program is offered as a
       did-you-mean hint on a trailing note, so a typo points at the command it
       resembles without crowding the problem line. */
    let local_names = ArrayList<String>{};
    actx.defined_functions.for_each(
        [&](StringView n) throws { local_names.push(String{n}); });
    actx.known_aliases.for_each([&](StringView n)
                                    throws { local_names.push(String{n}); });
    let suggestion_note = String{};
    if (Maybe<String> suggestion =
            utils::suggest_command(StringView{*name}, local_names))
    {
      suggestion_note = "Did you mean '" + *suggestion + "'?";
    }
    /* Point at the command word, not at the whole command. With an assignment
       prefix the command location is the assignment, not the program name. A
       missing command is a fatal analysis error, so the file does not run with
       a command that cannot resolve. After a dot, source, or eval the command
       may be defined by code the prepass cannot see, so it is only a warning
       there.
       POSIX mode skips the analysis, so the file runs and the runtime
       resolution sets 127 per command the way dash does. */
    if (actx.has_seen_runtime_definer)
      actx.warn(m_args[0]->source_location(), message, suggestion_note.view());
    else
      actx.fail(m_args[0]->source_location(), message, suggestion_note.view());
  }

  /* A command may change a variable out of the prepass's static view, so a
     constant recorded for a later straight-line reference is no longer proven.
     A set of read-only builtins and read-only coreutils in a static packed
     table never writes a shell variable and never runs code the prepass cannot
     see, so a constant survives across them. Every other command, including a
     function call, an unset, an export, or a command substitution argument,
     forgets the whole table. */
  let const is_variable_neutral_builtin =
      optimizer::command_is_environment_neutral(command_literal.view());

  bool clears_constants = !is_variable_neutral_builtin;
  if (!clears_constants) {
    /* A command substitution runs arbitrary code, so even a neutral builtin
       carrying one forgets the table to stay conservative. */
    for (let const t : m_args) {
      if (t->kind() != Token::Kind::Word) continue;
      let const &word = static_cast<const tokens::WordToken *>(t)->word();
      for (let const &segment : word.segments) {
        if (segment.kind == WordSegment::Kind::CommandSubstitution) {
          clears_constants = true;
          break;
        }
      }
      if (clears_constants) break;
    }
  }

  /* A neutral builtin run under a name shadowed by a function or an alias is
     really a call into user code, so it forgets the table too. */
  if (!clears_constants &&
      (actx.defined_functions.contains(command_literal.view()) ||
       actx.known_aliases.contains(command_literal.view())))
  {
    clears_constants = true;
  }

  if (clears_constants) {
    LOG(Debug,
        "the command '%s' may write variables, forgetting the recorded "
        "constants",
        command_literal.c_str());
    actx.constant_variables.clear();
  }
}

cold fn SimpleCommand::try_static_condition_verdict(
    const AnalysisContext &actx) const wontthrow -> Maybe<bool>
{
  /* A redirection has an effect even when the command succeeds, an async or a
     negated command changes the status, and a prefix assignment writes a
     variable. None of those is constant, so the fold declines them. The guards
     read this node's private members, so they stay here and the decision over
     the command words runs in the optimizer. */
  if (!m_redirections.is_empty()) return shit::None;
  if (is_async() || is_negated()) return shit::None;
  if (m_local_vars.count() > 0) return shit::None;

  return optimizer::simple_command_static_verdict(m_args, actx);
}

cold fn Pipeline::analyze(AnalysisContext &actx,
                          bool is_unconditional) const throws -> void
{
  /* A multi-stage pipeline runs each stage in a forked child, so an assignment
     in a stage never changes a parent variable and must not be recorded as a
     straight-line constant. A single command is not a real pipeline and keeps
     the caller's unconditional context. */
  let const stage_is_unconditional =
      is_unconditional && m_commands.count() == 1;
  for (let const command : m_commands) {
    ASSERT(command != nullptr);
    command->analyze(actx, stage_is_unconditional);
  }

  /* cat reading a single named file only to feed it into the next stage runs an
     extra process for nothing, the next command can open the file itself. This
     is shellcheck SC2002. The first stage must be cat with one plain file
     operand and a later stage must follow. A function or alias named cat is the
     user's own, so the lint is off for it. */
  if (m_commands.count() > 1) {
    ASSERT(m_commands[0] != nullptr);
    const SimpleCommand *first_stage = m_commands[0]->as_simple_command();
    if (first_stage != nullptr) {
      let const &cat_args = first_stage->args();
      if (cat_args.count() == 2) {
        let const name = static_command_name(cat_args[0]);
        let const raw_operand = cat_args[1]->raw_string();
        let const file_is_plain_operand =
            cat_args[1]->kind() == Token::Kind::Word &&
            !raw_operand.is_empty() && raw_operand[0] != '-';
        if (name.has_value() && name->view() == "cat" &&
            !actx.defined_functions.contains(name->view()) &&
            !actx.known_aliases.contains(name->view()) && file_is_plain_operand)
        {
          actx.warn(cat_args[0]->source_location(), "A useless cat",
                    "Give the file to the next command directly instead of "
                    "piping cat");
        }
      }
    }
  }

  /* The stage-pair lints. find piped into xargs splits the names on whitespace
     and quotes, so a name with a space breaks apart, shellcheck SC2038, pair
     find -print0 with xargs -0 or use find -exec. A pipe into a command that
     never reads stdin discards the upstream output, shellcheck SC2216. */
  for (usize i = 0; i + 1 < m_commands.count(); i++) {
    const SimpleCommand *stage = m_commands[i]->as_simple_command();
    const SimpleCommand *next = m_commands[i + 1]->as_simple_command();
    if (stage == nullptr || next == nullptr) continue;
    if (stage->args().is_empty() || next->args().is_empty()) continue;
    let const stage_name = static_command_name(stage->args()[0]);
    let const next_name = static_command_name(next->args()[0]);
    if (!stage_name.has_value() || !next_name.has_value()) continue;
    let const next_is_user =
        actx.defined_functions.contains(next_name->view()) ||
        actx.known_aliases.contains(next_name->view());

    if (stage_name->view() == "find" && next_name->view() == "xargs" &&
        !next_is_user && !actx.defined_functions.contains(stage_name->view()) &&
        !actx.known_aliases.contains(stage_name->view()))
    {
      let has_null_flag = false;
      for (usize a = 1; a < stage->args().count() && !has_null_flag; a++)
        if (stage->args()[a]->raw_string().view() == "-print0")
          has_null_flag = true;
      for (usize a = 1; a < next->args().count() && !has_null_flag; a++) {
        let const raw = next->args()[a]->raw_string();
        if (raw.view() == "-0" || raw.view() == "--null") has_null_flag = true;
      }
      if (!has_null_flag)
        actx.warn(next->args()[0]->source_location(),
                  "An xargs splits the find output on whitespace and quotes",
                  "Pair find -print0 with xargs -0 or use find -exec");
    }

    if (!next_is_user && NON_STDIN_READERS.find(next_name->view()).has_value())
    {
      if (!args_have_stdin_operand(next->args()))
        actx.warn(next->args()[0]->source_location(),
                  "The pipe feeds '" + next_name->view() +
                      "', which never reads stdin, so the upstream output is "
                      "discarded");
    }

    let const stage_is_user =
        actx.defined_functions.contains(stage_name->view()) ||
        actx.known_aliases.contains(stage_name->view());
    let const next_is_grep = next_name->view() == "grep" ||
                             next_name->view() == "egrep" ||
                             next_name->view() == "fgrep";

    /* ps piped into grep races the live process table and matches the grep
       itself, shellcheck SC2009, use pgrep. */
    if (stage_name->view() == "ps" && !stage_is_user && next_is_grep)
      actx.warn(next->args()[0]->source_location(),
                "Grepping the ps output races the process table and matches "
                "the grep itself",
                "Use pgrep to match a process by name");

    /* ls piped into grep parses the formatted listing, which mangles a name
       with a space or a newline, shellcheck SC2010, use a glob or find. */
    if (stage_name->view() == "ls" && !stage_is_user && next_is_grep)
      actx.warn(next->args()[0]->source_location(),
                "Grepping the ls listing mangles a name with a space or a "
                "newline",
                "Match the names with a glob or with find instead");

    /* grep whose output only feeds wc -l counts matches with a second
       process, shellcheck SC2126, use grep -c. */
    if (stage_name->view() == "grep" && !stage_is_user &&
        next_name->view() == "wc" && !next_is_user &&
        next->args().count() == 2 &&
        next->args()[1]->raw_string().view() == "-l")
    {
      actx.warn(stage->args()[0]->source_location(),
                "Counting grep output with wc -l runs an extra process",
                "Use grep -c to count the matching lines directly");
    }
  }

  /* A multi-stage pipeline reads variables in its children and the table cannot
     prove a value across the fork, so it forgets any recorded constant. */
  if (m_commands.count() > 1) actx.constant_variables.clear();
}

cold fn CompoundListCondition::analyze(AnalysisContext &actx,
                                       bool is_unconditional) const throws
    -> void
{
  ASSERT(m_cmd != nullptr);

  m_cmd->analyze(actx, is_unconditional);
}

cold fn CompoundListCondition::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_cmd != nullptr);

  m_cmd->register_defined_functions(actx);
}

cold fn CompoundListCondition::try_static_condition_verdict(
    const AnalysisContext &actx) const wontthrow -> Maybe<bool>
{
  ASSERT(m_cmd != nullptr);

  /* An && or || node depends on the command before it, so only a plain
     sequence node carries the verdict of its own command. */
  if (m_kind != Kind::None) return shit::None;
  return m_cmd->try_static_condition_verdict(actx);
}

cold fn CompoundList::analyze(AnalysisContext &actx,
                              bool is_unconditional) const throws -> void
{
  /* A function defined by a later sibling resolves a call earlier in the same
     list, the way the runtime would once the whole file is read. Register every
     top-level function name before the ordered walk so a forward or
     cross-branch call does not scan PATH or warn. */
  for (let const node : m_nodes) {
    ASSERT(node != nullptr);
    node->register_defined_functions(actx);
  }

  for (let const node : m_nodes) {
    ASSERT(node != nullptr);

    /* A semicolon or newline node runs whenever the list runs. An && or || node
       runs only depending on the previous command, so it is conditional. */
    let const node_unconditional =
        is_unconditional && node->kind() == CompoundListCondition::Kind::None;
    node->analyze(actx, node_unconditional);
  }
}

cold fn CompoundList::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  for (let const node : m_nodes) {
    ASSERT(node != nullptr);
    node->register_defined_functions(actx);
  }
}

cold fn CompoundList::try_static_condition_verdict(
    const AnalysisContext &actx) const wontthrow -> Maybe<bool>
{
  /* A condition list of more than one command runs each in turn, so only a list
     of exactly one command has a verdict the whole condition takes. */
  if (m_nodes.count() != 1) return shit::None;
  ASSERT(m_nodes[0] != nullptr);
  return m_nodes[0]->try_static_condition_verdict(actx);
}

cold fn IfStatement::analyze(AnalysisContext &actx,
                             bool is_unconditional) const throws -> void
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_then != nullptr);

  /* The condition always runs to decide the branch. The branches do not. */
  m_condition->analyze(actx, is_unconditional);
  m_then->analyze(actx, false);
  if (m_otherwise != nullptr) m_otherwise->analyze(actx, false);

  /* A branch ran conditionally and may have reassigned a name, so a value
     recorded before this if is no longer proven to hold in the straight-line
     block after it. Clearing matches IfClause::analyze and keeps the constant
     propagation to a single straight-line run. */
  actx.constant_variables.clear();
}

cold fn IfStatement::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_then != nullptr);

  m_condition->register_defined_functions(actx);
  m_then->register_defined_functions(actx);
  if (m_otherwise != nullptr) m_otherwise->register_defined_functions(actx);
}

} // namespace expressions

} // namespace shit
