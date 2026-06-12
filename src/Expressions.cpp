#include "Expressions.hpp"

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Lexer.hpp"
#include "Optimizer.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

static fn indent_for_layer(usize layer) throws -> String
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
    throw Error{"Interrupted"};
  }
  /* A trapped signal arrived since the last node, so its action runs here at
     the command boundary before the next node. The single flag keeps the common
     no-signal path to one read. */
  if (os::SIGNAL_PENDING) cxt.run_pending_traps();
  cxt.add_evaluated_expression();
  return evaluate_impl(cxt);
}

fn Expression::operator delete(void *pointer) wontthrow -> void
{
  if (is_arena_pointer(pointer)) return;
  ::operator delete(pointer);
}

cold fn AnalysisContext::warn(SourceLocation location,
                              StringView message) throws -> void
{
  const WarningWithLocation located{location, message};
  show_message(located.to_string(source));
}

cold fn AnalysisContext::fail(SourceLocation location,
                              StringView message) throws -> void
{
  /* Under -W the analysis still runs but its errors are reported as warnings
     and the run proceeds, so the same call reports without stopping. */
  if (errors_are_warnings) {
    warn(location, message);
    return;
  }
  const ErrorWithLocation located{location, message};
  show_message(located.to_string(source));
  has_fatal = true;
}

/* A command-not-found at runtime is non-fatal. It prints a located diagnostic
   to stderr against the source the evaluator is running, so a redirection such
   as 2>/dev/null still suppresses it, and leaves the caller to report status
   127. */
cold static fn report_command_not_found(EvalContext &cxt,
                                        const CommandNotFound &e) throws -> void
{
  const String *source = cxt.current_source();
  show_message(e.to_string(source != nullptr ? source->view() : StringView{}));
  /* A command not found inside a sourced file prints the source backtrace under
     the error the way a fatal error does, so the chain of dot or source calls
     that led here is named. It prints nothing at the top level. */
  cxt.print_source_backtrace();
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
  for (const WordSegment &segment : word.segments) {
    if (segment.kind == WordSegment::Kind::VariableReference) return shit::None;
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
  if (name.find_character('/').has_value()) {
    /* A leading tilde is expanded first, since the runtime expands it before
       resolving the command. */
    if (let const expanded = expand_leading_tilde(name.view()))
      return utils::canonicalize_path(expanded->view()).has_value();
    return utils::canonicalize_path(name.view()).has_value();
  }

  if (const bool *cached = actx.command_resolution_cache.find(name.view())) {
    LOG(verbosity::Debug, "reusing the cached resolution of '%s'",
        name.c_str());
    return *cached;
  }

  const bool was_resolved =
      utils::search_program_path(name.view()).count() != 0;
  LOG(verbosity::Debug, "scanning PATH for '%s', the command was %s",
      name.c_str(), was_resolved ? "found" : "not found");
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
  for (const WordSegment &segment : word.segments) {
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
      scan++;

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

/* Fold every constant arithmetic expansion in a word to its decimal result
   once, so the evaluator reads the cached value instead of re-parsing the
   arithmetic on every expansion. A segment that holds a parameter or a
   substitution is left alone, since its value is only known at run time. The
   fold now runs as the constant-arithmetic rule in the optimizer, reached
   through optimize_node from each command's analyze. */

} /* namespace */

fn analyze_ast(const Expression *root, StringView source,
               const HashSet &known_functions, const HashSet &known_aliases,
               bool errors_are_warnings,
               bool silence_unresolved_commands) throws -> bool
{
  ASSERT(root != nullptr);

  AnalysisContext actx{source};
  actx.errors_are_warnings = errors_are_warnings;
  actx.silence_unresolved_commands = silence_unresolved_commands;

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
      interpreter_is_sh = true;
    if (contains_dash || (interpreter_is_sh && !contains_bash))
      actx.shebang_is_posix_sh = true;
  }

  LOG(verbosity::Debug, "analyzing the ast, the posix sh shebang gate is %s",
      actx.shebang_is_posix_sh ? "armed" : "off");

  /* A function or alias defined by an earlier command resolves, so seed the
     prepass with the names already registered. */
  known_functions.for_each(
      [&actx](StringView name) { actx.defined_functions.add(name); });
  known_aliases.for_each(
      [&actx](StringView name) { actx.known_aliases.add(name); });

  root->analyze(actx, true);

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
  /* And *otherwise may be nullptr. */
}

/* The condition, the then branch, and the else branch live in the arena, which
   runs each node's destructor once on reset. */
IfStatement::~IfStatement() = default;

hot fn IfStatement::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_then != nullptr);

  const i64 condition = m_condition->evaluate(cxt);
  /* A jump set while evaluating the condition stops the if and stays pending.
   */
  if (cxt.has_pending_control_flow()) return condition;

  LOG(verbosity::Debug, "the if condition yielded %lld, running the %s branch",
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
  m_time_uses_posix_format = posix_format;
}

pure fn Command::is_timed() const wontthrow -> bool { return m_is_timed; }

pure fn Command::time_uses_posix_format() const wontthrow -> bool
{
  return m_time_uses_posix_format;
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

AssignCommand::AssignCommand(SourceLocation location, const Assignment *a)
    : Command(location), m_assignment(a)
{}

/* The assignment lives in the arena, torn down once on reset. */
AssignCommand::~AssignCommand() = default;

pure fn AssignCommand::assignment() const wontthrow -> const Assignment *
{
  return m_assignment;
}

fn AssignCommand::is_assignment() const wontthrow -> bool { return true; }

fn AssignCommand::as_assign_command() const wontthrow -> const AssignCommand *
{
  return this;
}

cold fn AssignCommand::analyze(AnalysisContext &actx,
                               bool is_unconditional) const throws -> void
{
  ASSERT(m_assignment != nullptr);

  /* A constant $((...)) on the right of x=$((2+2)) is folded once by the
     constant-arithmetic rule, so the loop body that re-runs this assignment
     reads the cached result. The rule reads the constant table, so the fold
     runs before the table records this assignment. */
  optimizer::optimize_node(this, actx);

  /* The constant-propagation rule records a name assigned a plain literal value
     in a straight-line block, so a later $name reference in a static condition
     or constant arithmetic reads the recorded value. The record is conservative
     in every uncertain case, where the table simply forgets the name. */
  let const &name = m_assignment->key();

  /* An element assignment a[i]=v or m[k]=v changes what $a or $((a)) reads
     without recording a scalar literal, so the base name before the bracket is
     forgotten rather than recorded under the bracketed key. */
  if (let const bracket = name.view().find_character('['); bracket.has_value())
  {
    LOG(verbosity::All,
        "forgetting the constant for the array base '%.*s' after an element "
        "assignment",
        static_cast<int>(*bracket), name.view().data);
    actx.constant_variables.erase(name.view().substring_of_length(0, *bracket));
    return;
  }

  /* A plain scalar assignment inside a function body with no prior local for
     the name leaks the value to the global scope, the footgun shit's own
     default mood guards against at run time. The append form is left alone
     since it extends a value the name already holds. This is the shellcheck
     SC2030-style warning for a leaked variable. */
  if (actx.function_scope_depth > 0 && !m_assignment->is_append() &&
      !actx.function_local_names.contains(name.view()))
    actx.warn(source_location(),
              StringView{"This assignment to '"} + name +
                  "' in a function has no local, so the value leaks to the "
                  "global scope, declare it with local to keep it inside the "
                  "function");

  /* A conditional or nested assignment may not run, and a runtime definer such
     as eval or dot may already have changed the name out of view, so neither
     proves the value. The append form NAME+=VALUE depends on the prior value,
     which the prepass does not track. Each of these forgets the name rather
     than record it. */
  if (!is_unconditional || actx.saw_runtime_definer ||
      m_assignment->is_append())
  {
    LOG(verbosity::All,
        "forgetting the constant for '%s', the assignment is conditional, "
        "appends, or follows a runtime definer",
        name.c_str());
    actx.constant_variables.erase(name.view());
    return;
  }

  let const literal = optimizer::literal_word_value(m_assignment->value_word());
  if (literal.has_value()) {
    LOG(verbosity::All, "recording the constant '%s' = '%s'", name.c_str(),
        literal->c_str());
    actx.constant_variables.set(name.view(), literal->view());
  } else {
    /* The value is only known at run time, so a constant recorded for this name
       under an earlier assignment no longer holds and is forgotten. */
    LOG(verbosity::All,
        "forgetting the constant for '%s', its value is only known at run "
        "time",
        name.c_str());
    actx.constant_variables.erase(name.view());
  }
}

hot fn AssignCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_assignment != nullptr);

  LOG(verbosity::All, "assigning the variable '%s'",
      m_assignment->key().c_str());

  /* Record where this assignment sits so a $LINENO in its value reports its
     line, since a script may read it as x=$LINENO. */
  cxt.set_current_location(source_location());

  /* The status defaults to 0, but a command substitution in the value sets it
     to the status of that substitution, which the assignment then reports. */
  cxt.set_last_exit_status(0);

  /* The value expansion and the store throw a plain Error, an unset variable
     under set -u or a readonly name. The assignment has a source location, so
     the error is relocated to a caret at it the way process_args does for a
     command argument. An already located error from a deeper command
     substitution rides through, since it is a separate branch under ErrorBase.
   */
  try {
    let value = cxt.expand_word_for_assignment(m_assignment->value_word());

    /* a[i]=v and m[k]=v assign one array element. The evaluator routes the
       subscript to the indexed or the associative store. */
    const StringView key_view = m_assignment->key().view();
    if (let const bracket = key_view.find_character('[');
        bracket.has_value() && key_view[key_view.length - 1] == ']')
    {
      const StringView array_name = key_view.substring_of_length(0, *bracket);
      const StringView subscript = key_view.substring_of_length(
          *bracket + 1, key_view.length - *bracket - 2);
      cxt.assign_array_element(array_name, subscript, value.view(),
                               m_assignment->is_append());
      cxt.set_last_exit_status(0);
      return 0;
    }

    /* The append form NAME+=VALUE prepends the current value of NAME, treating
       an unset name as empty, so the store receives the concatenation. An
       integer name adds rather than concatenates, so the join wraps the
       appended expression for the arithmetic in the store. */
    if (m_assignment->is_append()) {
      let appended =
          String{cxt.get_variable_value(m_assignment->key()).value_or("")};
      if (cxt.is_integer_variable(m_assignment->key()))
        cxt.append_integer_expression(appended, value.view());
      else
        appended += value;
      value = steal(appended);
    }

    /* The assignment goes through set_shell_variable first, so it still rejects
       a readonly name and refreshes the cached IFS. Under allexport it is then
       marked for the environment so a child inherits it, while a later lookup
       still finds the shell copy. */
    cxt.set_shell_variable(m_assignment->key(), value);
    if (cxt.export_all()) {
      let const &key = m_assignment->key();
      cxt.record_environment_change(key);
      os::set_environment_variable(key, value);
      cxt.mark_exported(key);
    }
    return cxt.last_exit_status();
  } catch (const Error &e) {
    throw relocate_error(e, source_location());
  }
}

cold fn AssignCommand::to_string() const throws -> String
{
  return "Assign " + m_assignment->to_ast_string();
}

cold fn AssignCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

fn AssignCommand::redirect_to(usize d, String &f, bool duplicate) throws -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

fn AssignCommand::append_to(usize d, String &f, bool duplicate) throws -> void
{
  redirect_to(d, f, duplicate);
}

SimpleCommand::SimpleCommand(SourceLocation location,
                             ArrayList<const Token *> &&args)
    : Command(location), m_args(steal(args))
{

  /* The command's location spans from its first word to the end of its last,
     so a caret under the whole command, such as a sourced '. file' in a
     backtrace, covers the argument and not only the command word. */
  if (!m_args.is_empty()) {
    const SourceLocation first = m_args[0]->source_location();
    const SourceLocation last = m_args.back()->source_location();
    m_location.position = first.position;
    m_location.length = last.position + last.length - first.position;
  }
}

/* The argument tokens and the redirection target tokens live in the arena,
   torn down once on reset. */
SimpleCommand::~SimpleCommand() = default;

fn SimpleCommand::set_redirections(ArrayList<Redirection> &&redirections) throws
    -> void
{
  m_redirections = steal(redirections);
}

fn SimpleCommand::set_array_args(
    ArrayList<array_builtin_assignment> &&array_args) throws -> void
{
  m_array_args = steal(array_args);
}

namespace {

using expressions::Redirection;

/* Route an opened descriptor into one of the three standard slots a stage
   carries. A target fd of 0 is the standard input, 2 the standard error, and
   any other number the standard output, since a pipeline stage and an exec
   context only express those three. The last redirection of a descriptor wins,
   so a descriptor already in the slot closes before the new one takes its
   place. */
fn assign_standard_fd(Maybe<os::descriptor> &in_fd,
                      Maybe<os::descriptor> &out_fd,
                      Maybe<os::descriptor> &err_fd, i32 fd,
                      os::descriptor file_fd) throws -> void
{
  if (fd == 0) {
    if (in_fd) os::close_fd(*in_fd);
    in_fd = file_fd;
  } else if (fd == 2) {
    if (err_fd) os::close_fd(*err_fd);
    err_fd = file_fd;
  } else {
    if (out_fd) os::close_fd(*out_fd);
    out_fd = file_fd;
  }
}

/* Resolve the descriptor a duplication copies from. A literal descriptor and
   the close form were settled at parse time and pass straight through. A
   dynamic word such as $4 is expanded to one field here, where a dash means
   close and a numeric field names the descriptor. The result is a non-negative
   descriptor or Redirection::DUP_FD_CLOSE for the close form. A field that is
   neither throws a located error at the word. */
fn resolve_duplication_fd(const Redirection &redir, EvalContext &cxt) throws
    -> i32
{
  if (redir.target == nullptr) return redir.dup_fd;

  ArrayList<const Token *> target_tokens{cxt.scratch_allocator()};
  target_tokens.push(redir.target);
  const ArrayList<String> fields = cxt.process_args(target_tokens);
  if (fields.count() != 1) {
    throw ErrorWithLocation{redir.target->source_location(),
                            "Duplication target is not a single descriptor"};
  }

  const String &field = fields[0];
  if (field == "-") return Redirection::DUP_FD_CLOSE;

  let const parsed = utils::parse_decimal_integer(field.view());
  if (parsed.is_error() || parsed.value() < 0) {
    throw ErrorWithLocation{redir.target->source_location(),
                            "'" + field + "' is not a valid descriptor"};
  }
  return static_cast<i32>(parsed.value());
}

} /* namespace */

/* The pipeline-stage redirect path fills the stage's descriptor slots and the
   cross-route flags the spawn applies after the files, so a `cmd 2>&1 >file`
   stage still snapshots the final file target. The standalone simple-command
   path in evaluate_impl is source-ordered, this one is not yet. */
/* The file open mode a file redirection opens its target with. A plain > honors
   noclobber, >| overrides it, >> appends, <> opens read-write, and < and every
   other kind reads. */
static fn redirection_open_mode(Redirection::Kind kind,
                                bool no_clobber) wontthrow -> os::file_open_mode
{
  switch (kind) {
  case Redirection::Kind::TruncateOutput:
    return no_clobber ? os::file_open_mode::TruncateNoClobber
                      : os::file_open_mode::Truncate;
  case Redirection::Kind::TruncateOutputOverride:
    return os::file_open_mode::Truncate;
  case Redirection::Kind::AppendOutput: return os::file_open_mode::Append;
  case Redirection::Kind::ReadWrite: return os::file_open_mode::ReadWrite;
  default: return os::file_open_mode::Read;
  }
}

fn SimpleCommand::redirect_exec_context(ExecContext &ec,
                                        EvalContext &cxt) const throws -> void
{
  LOG(verbosity::Debug, "applying %zu redirections to the pipeline stage",
      m_redirections.count());
  for (const Redirection &redir : m_redirections) {
    if (redir.kind == Redirection::Kind::Heredoc) {
      ASSERT(redir.heredoc_body != nullptr);

      let body = redir.heredoc_body->clone();
      if (redir.heredoc_expand) {
        body = cxt.expand_heredoc_body(body);
      }

      let opened = os::write_to_temp_file(body);
      if (!opened)
        throw Error{"Could not stage the heredoc body: " +
                    os::last_system_error_message()};

      if (ec.in_fd) os::close_fd(*ec.in_fd);
      ec.in_fd = opened.take();
      continue;
    }

    if (redir.kind == Redirection::Kind::HereString) {
      ASSERT(redir.target != nullptr);
      String body = cxt.expand_word_for_assignment(
          static_cast<const tokens::WordToken *>(redir.target)->word());
      body += "\n";

      let opened = os::write_to_temp_file(body);
      if (!opened)
        throw Error{"Could not stage the here-string: " +
                    os::last_system_error_message()};

      if (ec.in_fd) os::close_fd(*ec.in_fd);
      ec.in_fd = opened.take();
      continue;
    }

    if (redir.kind == Redirection::Kind::DuplicateOutput ||
        redir.kind == Redirection::Kind::DuplicateInput)
    {
      const i32 from_fd = resolve_duplication_fd(redir, cxt);
      /* A self copy changes nothing. The standard cross-routing keeps the flag
         fast path. An arbitrary descriptor or the close form is not represented
         by the stage's three descriptor slots and is left to the compound path.
       */
      if (from_fd == redir.fd) {
        /* Self copy is a no-op. */
      } else if (redir.fd == 2 && from_fd == 1) {
        ec.dup_err_to_out = true;
        ec.dup_out_to_err_came_last = false;
      } else if (redir.fd == 1 && from_fd == 2) {
        ec.dup_out_to_err = true;
        ec.dup_out_to_err_came_last = true;
      }
      continue;
    }

    ASSERT(redir.target != nullptr);

    ArrayList<const Token *> target_tokens{cxt.scratch_allocator()};
    target_tokens.push(redir.target);
    const ArrayList<String> target = cxt.process_args(target_tokens);
    if (target.count() != 1) {
      throw ErrorWithLocation{redir.target->source_location(),
                              "Redirection target is not a single file"};
    }

    let mode = redirection_open_mode(redir.kind, cxt.no_clobber());

    const String &target_path = target[0];
    let opened = os::open_file_descriptor(target_path, mode);
    if (!opened) {
      throw ErrorWithLocation{redir.target->source_location(),
                              "Could not open '" + target_path +
                                  "': " + os::last_system_error_message()};
    }
    const os::descriptor file_fd = opened.take();

    assign_standard_fd(ec.in_fd, ec.out_fd, ec.err_fd, redir.fd, file_fd);
  }
}

fn SimpleCommand::is_simple_command() const wontthrow -> bool { return true; }

pure fn SimpleCommand::args() const wontthrow
    -> const ArrayList<const Token *> &
{
  return m_args;
}

fn SimpleCommand::as_simple_command() const wontthrow -> const SimpleCommand *
{
  return this;
}

namespace {

/* Replace a command word that names an alias with the alias body. The body is
   split on whitespace, which covers the common case of an alias to a command
   and its options, and a name already expanded is not expanded again so a
   self-referential alias terminates. A quoted space inside the body is not
   preserved, since this expansion does not re-run the full tokenizer. */
fn expand_command_aliases(EvalContext &cxt, ArrayList<String> &args) throws
    -> void
{
  HashSet already_expanded{heap_allocator()};

  while (!args.is_empty()) {
    let const &word = args[0];

    if (already_expanded.contains(word.view())) break;

    let const body = cxt.get_alias(word);
    if (!body.has_value()) break;
    already_expanded.add(word.view());
    LOG(verbosity::Debug, "expanding the alias '%s'", word.c_str());

    /* The alias body replaces the first word, so the split words go in front of
       the remaining arguments. ArrayList has no in-place erase, so the new list
       is built and swapped in. */
    let rebuilt = ArrayList<String>{};
    let current = String{};
    let const &body_value = *body;
    for (usize i = 0; i < body_value.count(); i++) {
      const char c = body_value[i];
      if (c == ' ' || c == '\t') {
        if (!current.is_empty()) {
          rebuilt.push(String{
              heap_allocator(), StringView{current.data(), current.count()}
          });
          current.clear();
        }
      } else {
        current += c;
      }
    }
    if (!current.is_empty())
      rebuilt.push(String{
          heap_allocator(), StringView{current.data(), current.count()}
      });

    for (usize i = 1; i < args.count(); i++)
      rebuilt.push(steal(args[i]));

    args = steal(rebuilt);
  }
}

} /* namespace */

hot fn SimpleCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  /* A command may have no words when it is only a redirection, such as > file,
     or only assignments, such as a=1 b=2 or a bare array assignment a=(1 2), so
     the redirections and the assignments still run below. */
  ASSERT(m_args.count() > 0 || !m_redirections.is_empty() ||
         m_local_vars.count() > 0 || !m_array_args.is_empty());

  /* Record where this command sits so a $LINENO in its words reports its line.
   */
  cxt.set_current_location(source_location());

  if (cxt.should_echo()) {
    shit::print(utils::merge_tokens_to_string(m_args) + "\n");
    shit::flush();
  }

  /* A simple command consumes its argument words and discards them when it
     returns, so they are built on the scratch arena and reclaimed here rather
     than heap-allocated per argument. The mark is taken before the expansion
     and released on every exit from this command, so a loop body does not
     accumulate a vector per iteration. A builtin that keeps a word past the
     command copies it into the heap-backed store, so nothing the release frees
     is still read. */
  let const args_mark = cxt.scratch_mark();
  defer { cxt.scratch_release(args_mark); };
  /* A <(...) or >(...) in the words below opens a pipe and forks a child or
     leaves a temp file. The mark is taken before the expansion so this command
     reaps only what it opens, leaving a substitution from an enclosing command,
     such as a while loop's producer, for that command to reap. */
  let const substitution_mark = cxt.mark_process_substitutions();
  let program_args = cxt.process_args(m_args, /*args_are_transient=*/true);
  /* The descriptors stay open while the command runs and are closed and the
     children reaped when this command returns, on every path. */
  defer { cxt.cleanup_process_substitutions(substitution_mark); };
  expand_command_aliases(cxt, program_args);

  LOG(verbosity::Info, "dispatching the command '%s' with %zu words",
      program_args.is_empty() ? "" : program_args[0].c_str(),
      program_args.count());

  /* A bare exec, the word exec with no further argument, applies its
     redirections to the shell's own descriptors permanently rather than around
     a single command. A function named exec shadows the builtin the same way a
     function shadows any command, so a shadowing function takes the ordinary
     path. The redirection loop below routes each entry to the permanent path
     instead of the temporary save and restore path when this is set. */
  const bool is_bare_exec =
      program_args.count() == 1 && program_args[0] == "exec" &&
      !(cxt.has_functions() && cxt.find_function(program_args[0]) != nullptr);

  /* Whether the command word resolves to a POSIX special builtin not shadowed
     by a function. It decides both that a redirection error exits the shell
     rather than failing the command, and that a prefix assignment persists, so
     it is computed once here and read on both paths. An empty command word, a
     bare redirection or assignment line, is not a special builtin. */
  const bool command_is_special_builtin =
      !program_args.is_empty() &&
      !(cxt.has_functions() && cxt.find_function(program_args[0]) != nullptr) &&
      is_special_builtin_name(program_args[0].view());

  /* Open the redirection targets. A redirection takes effect even when the
     command expands to None, so > file with no command still creates the
     file. A heredoc on the standard input passes its staged descriptor to the
     exec context through this slot, which closes it, and the guard closes it on
     any path that does not hand it off. A file or a cross-route on the standard
     output and error is staged in source order onto the real shell fd below
     rather than into a slot. */
  Maybe<os::descriptor> redirect_in_fd;
  bool redirect_in_fd_handed_off = false;
  /* A redirect that points a real shell descriptor at its target around this
     command, a file on fd 1 or fd 2, a cross-route like 2>&1, a duplication
     onto an arbitrary descriptor like >&5, the close form >&-, and a numbered
     heredoc among them. The backups put the descriptors back once the command
     finishes, restored in reverse on every exit path. The standard fds are
     routed here so a later 2>&1 copies the descriptor its source points at now,
     in source order, rather than the one a deferred slot would place last. */
  ArrayList<os::saved_descriptor> dup_saved_descriptors{
      cxt.scratch_allocator()};
  defer
  {
    for (usize i = dup_saved_descriptors.count(); i > 0; i--)
      os::restore_descriptor(dup_saved_descriptors[i - 1]);
  };
  defer
  {
    if (!redirect_in_fd_handed_off && redirect_in_fd)
      os::close_fd(*redirect_in_fd);
  };

  /* Set just before a redirection resource failure throws, an open that failed
     or a descriptor that is not open, so the catch tells those apart from an
     expansion error in a target word, which must stay fatal. */
  bool redirection_open_failed = false;
  try {
    for (const Redirection &redir : m_redirections) {
      /* A heredoc body becomes the standard input through an anonymous temp
         file, expanded when the delimiter was unquoted. */
      if (redir.kind == Redirection::Kind::Heredoc ||
          redir.kind == Redirection::Kind::HereString)
      {
        let body = String{};
        if (redir.kind == Redirection::Kind::Heredoc) {
          ASSERT(redir.heredoc_body != nullptr);
          body = redir.heredoc_body->clone();
          if (redir.heredoc_expand) body = cxt.expand_heredoc_body(body);
        } else {
          ASSERT(redir.target != nullptr);
          body = cxt.expand_word_for_assignment(
              static_cast<const tokens::WordToken *>(redir.target)->word());
          body += "\n";
        }

        let opened = os::write_to_temp_file(body);
        if (!opened) {
          redirection_open_failed = true;
          throw ErrorWithLocation{source_location(),
                                  "Could not stage the heredoc body: " +
                                      os::last_system_error_message()};
        }

        /* A bare exec heredoc points the shell's standard input at the staged
           body for good and drops the temporary descriptor. Inside an
           in-process subshell the move is backed up first, so it stays
           contained the way a fork would contain it. */
        if (is_bare_exec) {
          cxt.snapshot_subshell_descriptor(redir.fd);
          shit::flush();
          const os::descriptor body_fd = opened.take();
          os::replace_descriptor(redir.fd, body_fd);
#if SHIT_PLATFORM_IS WIN32
          /* A Windows descriptor is a HANDLE, so the staged body is compared
             against the handle that already occupies the shell slot rather than
             against the bare fd number. */
          if (body_fd != os::descriptor_for_shell_fd(redir.fd))
            os::close_fd(body_fd);
#else
          if (body_fd != redir.fd) os::close_fd(body_fd);
#endif
          continue;
        }

        /* A heredoc on the standard input takes the in_fd slot. A numbered
           heredoc such as 3<<EOF targets descriptor N instead, which the three
           standard slots cannot express, so the body descriptor is staged onto
           the real shell fd N around the command and restored afterward, the
           same way a duplication onto an arbitrary descriptor is. */
        if (redir.fd == 0) {
          if (redirect_in_fd) os::close_fd(*redirect_in_fd);
          redirect_in_fd = opened.take();
          continue;
        }

        const os::descriptor body_fd = opened.take();
        /* The temp file already lands on fd N when mkstemp handed back that
           very number, since the standard descriptors took the lower slots. The
           generic save then dup2 would back up the body itself and leave it
           open on N after the command, so the collision is handled directly.
           The restore closes fd N, which fd N was free before mkstemp claimed
           it makes correct. */
#if SHIT_PLATFORM_IS WIN32
        /* A Windows descriptor is a HANDLE, so the staged body never shares the
           identity of a bare fd number the way a POSIX mkstemp descriptor can,
           and the save then replace path always runs. */
        const bool body_is_target_fd =
            body_fd == os::descriptor_for_shell_fd(redir.fd);
#else
        const bool body_is_target_fd = body_fd == redir.fd;
#endif
        if (body_is_target_fd) {
          dup_saved_descriptors.push(
              os::saved_descriptor{.shell_fd = redir.fd, .was_open = false});
          continue;
        }
        dup_saved_descriptors.push(
            os::save_and_replace_descriptor(redir.fd, body_fd));
        os::close_fd(body_fd);
        continue;
      }

      /* A duplication like 2>&1 routes one descriptor to another without a
         file. The descriptor may come from a dynamic word such as >&$5,
         resolved here.
       */
      if (redir.kind == Redirection::Kind::DuplicateOutput ||
          redir.kind == Redirection::Kind::DuplicateInput)
      {
        const i32 from_fd = resolve_duplication_fd(redir, cxt);

        /* A bare exec applies a duplication to the shell's own descriptor for
           good, so the copy or the close stays in effect for every later
           command, except inside an in-process subshell where the move is
           backed up and contained at the subshell's end. The flush keeps
           buffered output on the original descriptor before it moves. */
        if (is_bare_exec) {
          cxt.snapshot_subshell_descriptor(redir.fd);
          shit::flush();

          if (from_fd == Redirection::DUP_FD_CLOSE) {
            os::close_shell_fd(redir.fd);
            continue;
          }

          /* A duplication onto a closed or invalid descriptor, as in exec 6>&9
             with fd 9 closed, fails the dup2. The exec fails with a located
             error and the shell keeps the descriptor unchanged, matching dash.
           */
          if (!os::replace_descriptor(redir.fd,
                                      os::descriptor_for_shell_fd(from_fd)))
          {
            const SourceLocation location =
                redir.target != nullptr ? redir.target->source_location()
                                        : source_location();
            redirection_open_failed = true;
            throw ErrorWithLocation{location, utils::int_to_text(from_fd) +
                                                  ": Bad file descriptor"};
          }
          continue;
        }

        /* A descriptor copied onto itself, as in 1>&1, changes nothing. */
        if (from_fd == redir.fd) {
          /* Self copy is a no-op. */
          continue;
        }

        /* A cross-route between the standard output and error, as in 2>&1,
           points the real shell descriptor at the target in source order so a
           later file redirect on the source does not change what the copy
           already captured. The shell's buffered output is flushed first, so it
           lands on the original descriptor rather than the duplication target.
           The arbitrary descriptor and the close form take the same in-order
           path. */
        shit::flush();

        if (from_fd == Redirection::DUP_FD_CLOSE) {
          dup_saved_descriptors.push(os::save_and_replace_descriptor(
              redir.fd, os::descriptor_for_shell_fd(redir.fd)));
          os::close_fd(os::descriptor_for_shell_fd(redir.fd));
          continue;
        }

        const os::saved_descriptor saved = os::save_and_replace_descriptor(
            redir.fd, os::descriptor_for_shell_fd(from_fd));
        dup_saved_descriptors.push(saved);
        /* A duplication onto a closed or invalid descriptor, as in >&5 with fd
           5 closed, fails the dup2. The command fails with a located error
           rather than writing to the original descriptor, matching dash. */
        if (!saved.dup2_ok) {
          const SourceLocation location = redir.target != nullptr
                                              ? redir.target->source_location()
                                              : source_location();
          redirection_open_failed = true;
          throw ErrorWithLocation{location, utils::int_to_text(from_fd) +
                                                ": Bad file descriptor"};
        }
        continue;
      }

      ASSERT(redir.target != nullptr);

      ArrayList<const Token *> target_tokens{cxt.scratch_allocator()};
      target_tokens.push(redir.target);
      const ArrayList<String> target = cxt.process_args(target_tokens);
      if (target.count() != 1) {
        /* An ambiguous redirect, the target expanding to more or fewer than one
           word, fails the command rather than the shell, the way dash does. */
        redirection_open_failed = true;
        throw ErrorWithLocation{redir.target->source_location(),
                                "Redirection target is not a single file"};
      }

      let mode = redirection_open_mode(redir.kind, cxt.no_clobber());

      const String &target_path = target[0];
      let opened = os::open_file_descriptor(target_path, mode);
      if (!opened) {
        redirection_open_failed = true;
        throw ErrorWithLocation{redir.target->source_location(),
                                "Could not open '" + target_path +
                                    "': " + os::last_system_error_message()};
      }
      const os::descriptor file_fd = opened.take();

      /* A bare exec points the shell's own descriptor at the opened file for
         good, then drops the temporary descriptor the open returned. The dup2
         onto fd N replaces whatever fd N held before, so a second exec onto the
         same number closes the earlier file rather than leaking it. The flush
         keeps buffered output on the original descriptor before it moves. */
      if (is_bare_exec) {
        cxt.snapshot_subshell_descriptor(redir.fd);
        shit::flush();
        const bool was_replaced = os::replace_descriptor(redir.fd, file_fd);
#if SHIT_PLATFORM_IS WIN32
        /* A Windows descriptor is a HANDLE, so the opened file is compared
           against the handle that now occupies the shell slot rather than
           against the bare fd number. */
        if (file_fd != os::descriptor_for_shell_fd(redir.fd))
          os::close_fd(file_fd);
#else
        if (file_fd != redir.fd) os::close_fd(file_fd);
#endif
        if (!was_replaced) {
          redirection_open_failed = true;
          throw ErrorWithLocation{redir.target->source_location(),
                                  utils::int_to_text(redir.fd) +
                                      ": Bad file descriptor"};
        }
        continue;
      }

      /* Every file redirect, the standard input, output, and error included, is
         staged onto the real shell fd N in source order so a later 2>&1 copies
         the descriptor fd N points at now rather than the one the spawn would
         place last. A redirect onto fd 1 or fd 2 mutates the shell's own
         standard output or error in place, so the buffered output is flushed
         first to land on the original descriptor. open returns the lowest free
         fd, which is at least three while fds 0, 1, and 2 hold the shell's
         stdio, so the file never lands on a standard fd itself. The higher fd,
         such as 3>file, takes the same in-order path the numbered heredoc and
         the compound redirect path use. */
      if (redir.fd == 1 || redir.fd == 2) shit::flush();
#if SHIT_PLATFORM_IS WIN32
      /* A Windows descriptor is a HANDLE, so the opened file never shares the
         identity of a bare fd number and the save then replace path runs. */
      const bool file_is_target_fd =
          file_fd == os::descriptor_for_shell_fd(redir.fd);
#else
      const bool file_is_target_fd = file_fd == redir.fd;
#endif
      if (file_is_target_fd) {
        /* open returned fd N itself, since fd N was the lowest free descriptor,
           so the file already sits on its target. The generic save then dup2
           would back up the file and the close would shut fd N, leaving the
           child without it, so the collision is recorded for restore without a
           close, the same way the numbered heredoc handles it. */
        dup_saved_descriptors.push(
            os::saved_descriptor{.shell_fd = redir.fd, .was_open = false});
      } else {
        dup_saved_descriptors.push(
            os::save_and_replace_descriptor(redir.fd, file_fd));
        os::close_fd(file_fd);
      }
    }
  } catch (const ErrorWithLocation &redirection_error) {
    /* An expansion error in a target word, such as ${x?msg} on an unset name or
       a division by zero, is fatal the way it is anywhere else, so only an open
       or dup failure is caught here. */
    if (!redirection_open_failed) throw;
    /* A redirection that cannot open its target, or names a closed descriptor,
       fails the command rather than the shell, the way dash continues past it.
       A special builtin is the exception, since its redirection error exits a
       non-interactive shell. The descriptor and heredoc defers above still put
       the partially applied redirections back. */
    if (command_is_special_builtin) throw;
    const String *source = cxt.current_source();
    show_message(redirection_error.to_string(source != nullptr ? source->view()
                                                               : StringView{}));
    /* dash reports a redirection failure with status 2, the value a script
       reads in $? after the failed command. */
    cxt.set_last_exit_status(2);
    return 2;
  }

  /* An expansion may drop every word, for example an unset $x used as the whole
     command. There is None to run then, but the redirections above already
     took effect. A command-less line still carries its assignments, which
     persist in the current shell rather than apply only to a child. The
     location was set at the top of this function, so a $LINENO in any value on
     the line reports the same line, the way dash reports it. */
  if (program_args.is_empty()) {
    /* The assignments commit left to right, each before the next is expanded,
       so a later value reads an earlier same-line one and a repeated name or a
       += accumulates against what the store already holds. */
    for (const prefix_assignment &var : m_local_vars) {
      const StringView name = var.name.view();
      let value = cxt.expand_word_for_assignment(var.value);
      if (var.is_append) {
        /* The joined text is transient, copied by the store and the
           environment write, so it lives on the per-command scratch arena. An
           integer name evaluates the join to its decimal here, since the
           environment write takes the value verbatim. */
        let appended = String{cxt.scratch_allocator()};
        if (let const existing = cxt.get_variable_value(name))
          appended.append(existing->view());
        if (cxt.is_integer_variable(name)) {
          cxt.append_integer_expression(appended, value.view());
          char decimal[24];
          value = String{
              cxt.scratch_allocator(),
              utils::int_to_text_into(cxt.evaluate_arithmetic(appended.view()),
                                      decimal, sizeof(decimal))};
        } else {
          appended += value;
          value = steal(appended);
        }
      }
      cxt.set_shell_variable(name, value);
      if (cxt.export_all()) {
        cxt.record_environment_change(name);
        os::set_environment_variable(name, value.view());
        cxt.mark_exported(name);
      }
    }
    /* A command-less line may also carry bare array assignments, such as the
       pvars=() of flags= pvars=() specs=(), applied after the scalars in source
       order. */
    for (const array_builtin_assignment &assignment : m_array_args) {
      ArrayList<String> values = cxt.process_args(assignment.elements);
      cxt.assign_indexed_array_elements(assignment.name, steal(values),
                                        assignment.is_append);
    }
    cxt.set_last_exit_status(0);
    return 0;
  }

  /* A prefix assignment before a special builtin persists after the command as
     a regular shell variable, the way POSIX keeps it.
     command_is_special_builtin, computed above the redirection loop, already
     excludes a function-shadowed name. The persisted form commits to the store
     below rather than the process environment, so it stays unexported, the way
     dash leaves it. */

  /* Per-command assignments apply to the environment for this command, a
     function call included, so a child inherits them and a function sees them.
     The previous values are restored on every exit path. */
  /* The environment value a prefix assignment shadowed, restored on exit. */
  struct saved_env_var
  {
    String name;
    Maybe<String> previous_value;
  };
  ArrayList<saved_env_var> saved_env{cxt.scratch_allocator()};
  /* A prefix IFS=... drives the shell's own word splitting for this command,
     read by the read builtin and by every expansion in the command, which take
     it from the live separator cache rather than the environment. The effective
     separators are saved before the first such prefix and restored on exit, the
     way the prefix PATH save below reverts the resolver. */
  bool ifs_was_assigned = false;
  String saved_ifs_separators{cxt.scratch_allocator()};
  /* The assignments apply left to right, each committed to the environment
     before the next is expanded, so a later value reads an earlier same-line
     one and a repeated name or a += accumulates. The previous environment
     values are saved for the restore on exit, which keeps the prefix temporary
     for this command. */
  for (const prefix_assignment &var : m_local_vars) {
    const StringView name = var.name.view();
    Maybe<String> previous = os::get_environment_variable(name);
    /* The value expansion throws a plain Error, an unset variable under set -u,
       so it is relocated to a caret at the command the prefix leads. */
    let expanded_value = String{};
    try {
      expanded_value = cxt.expand_word_for_assignment(var.value);
    } catch (const Error &e) {
      throw relocate_error(e, source_location());
    }
    /* The append form prepends the current value of NAME, which a prefix reads
       from the shell store before the environment so a non-exported shell
       variable still contributes, treating an unset name as empty. The joined
       text is transient, copied by the store and the environment write, so it
       lives on the per-command scratch arena. An integer name evaluates the
       join to its decimal here, since the environment write takes the value
       verbatim. */
    if (var.is_append) {
      let appended = String{cxt.scratch_allocator()};
      if (let const existing = cxt.get_variable_value(name))
        appended.append(existing->view());
      if (cxt.is_integer_variable(name)) {
        cxt.append_integer_expression(appended, expanded_value.view());
        char decimal[24];
        expanded_value = String{
            cxt.scratch_allocator(),
            utils::int_to_text_into(cxt.evaluate_arithmetic(appended.view()),
                                    decimal, sizeof(decimal))};
      } else {
        appended += expanded_value;
        expanded_value = steal(appended);
      }
    }

    /* A special builtin keeps the assignment, so it commits to the store and
       leaves nothing for the defer to restore. set_shell_variable refreshes the
       IFS and PATH caches itself, so the temporary-cache bookkeeping below is
       skipped on this path. */
    if (command_is_special_builtin) {
      cxt.set_shell_variable(name, expanded_value);
      if (cxt.export_all()) {
        cxt.record_environment_change(name);
        os::set_environment_variable(name, expanded_value.view());
        cxt.mark_exported(name);
      }
      continue;
    }

    saved_env.push(saved_env_var{String{name}, steal(previous)});
    os::set_environment_variable(name, expanded_value.view());
    /* The prefix exports the name for the command, so the set carries it until
       the defer below rewinds the environment and the set with it. */
    cxt.mark_exported(name);
    /* A prefix PATH=... applies only to this command, so the resolver follows
       the temporary value while the command runs and reverts on exit. The
       resolver reads its own MAYBE_PATH rather than the environment, so a write
       to the environment alone would not change the search order. */
    if (name == "PATH")
      utils::set_path_for_resolution(String{expanded_value.view()});
    /* A prefix IFS=... re-aims the separator cache for the command's duration.
       The value before the first IFS prefix is saved once, so a name repeated
       on the line still reverts to where it began. */
    if (name == "IFS") {
      if (!ifs_was_assigned) {
        ifs_was_assigned = true;
        saved_ifs_separators =
            cxt.get_variable_value("IFS").value_or(String{" \t\n"});
      }
      cxt.set_field_separators(expanded_value.view());
    }
  }
  defer
  {
    /* The restore runs newest first, so a name spelled more than once on the
       line restores to the value it held before the first of its assignments
       rather than to an intermediate one. */
    bool path_was_assigned = false;
    for (usize i = saved_env.count(); i > 0; i--) {
      const saved_env_var &restore = saved_env[i - 1];
      if (restore.name == "PATH") path_was_assigned = true;
      if (restore.previous_value)
        os::set_environment_variable(restore.name.view(),
                                     restore.previous_value->view());
      else
        os::unset_environment_variable(restore.name.view());
      /* The exported set follows the rewind, so a name the prefix introduced
         leaves the set and a name it shadowed stays exported. */
      cxt.sync_exported_after_restore(restore.name.view(),
                                      restore.previous_value.has_value());
    }
    /* The prefix PATH reverts to the shell's effective PATH, the store value
       when set and the restored environment otherwise, so the next command
       resolves the way it would have without the prefix. */
    if (path_was_assigned)
      utils::set_path_for_resolution(cxt.get_variable_value("PATH"));
    /* The prefix IFS reverts to the separators that were effective before the
       command, so the next command splits the way it would have without it. */
    if (ifs_was_assigned) cxt.set_field_separators(saved_ifs_separators.view());
  };

  /* A function shadows a builtin and a program. Run its body with the call
     words as the positional parameters, restoring them afterwards. A return
     builtin unwinds here and supplies the function exit status. */
  ASSERT(!program_args.is_empty());
  let const &program_name = program_args[0];

  /* The command name is copied for the array-argument application below, since
     the argument vector is moved into the exec context before that point. */
  let array_command_name = String{};
  if (!m_array_args.is_empty())
    array_command_name = String{program_args[0].view()};

  if (const Expression *function_body =
          cxt.has_functions() ? cxt.find_function(program_name) : nullptr;
      function_body != nullptr)
  {
    /* A heredoc, a here-string, or an input file on the call lands on the
       real fd 0 for the body's duration, so the in-process body and every
       child it spawns read the staged bytes, the way bash redirects a forked
       function's stdin. The descriptor defer above restores the caller's
       stdin once the call ends. */
    if (redirect_in_fd) {
      dup_saved_descriptors.push(
          os::save_and_replace_descriptor(0, *redirect_in_fd));
      os::close_fd(*redirect_in_fd);
      redirect_in_fd = shit::None;
      redirect_in_fd_handed_off = true;
    }

    /* The caller's parameters are moved out and restored by moving back, so a
       deep copy of the list is not paid on every call. The store is empty in
       the window between, which the call-param build below does not read. */
    let saved_params = cxt.take_positional_params();
    let call_params = ArrayList<String>{};
    call_params.reserve(program_args.count() - 1);
    for (usize i = 1; i < program_args.count(); i++)
      call_params.push_managed(program_args[i]);
    cxt.set_positional_params(steal(call_params));
    defer { cxt.set_positional_params(steal(saved_params)); };

    /* Bound the call nesting so a function that recurses without a base case
       errors with a caret here rather than exhausting the native stack. The
       defer decrements on every unwind path. */
    cxt.enter_function_call(source_location());
    defer { cxt.leave_function_call(); };

    /* A loop in the caller is not the body's to break, so the body starts with
       a fresh loop count and the caller's count returns when the call ends. */
    let const saved_loop_depth = cxt.loop_depth();
    cxt.set_loop_depth(0);
    defer { cxt.set_loop_depth(saved_loop_depth); };

    /* The function body's transient scratch is reclaimed when the call returns,
       so a recursive function or a call in a loop does not grow the arena
       across frames. The status is an integer and the scope pop restores the
       locals into the heap-backed store, so nothing the release frees is still
       read. The release is registered first, so it runs last, after the scope
       pop. */
    let const call_mark = cxt.scratch_mark();
    defer { cxt.scratch_release(call_mark); };

    /* Open a local scope so a local builtin in the body shadows a variable and
       the old value returns when the call ends. The call name rides the same
       lifetime, the frame FUNCNAME reads. */
    cxt.enter_function_scope();
    cxt.push_function_call_name(program_name.view());
    defer
    {
      cxt.pop_function_call_name();
      cxt.leave_function_scope();
    };

    /* A command at the tail of the body must not exec the shell in place even
       when the call itself is the terminal command, since the call's cleanup,
       the positional restore and the scope pop, has to run after the body. */
    let const saved_terminal_exec = cxt.terminal_exec_allowed();
    cxt.set_terminal_exec_allowed(false);
    defer { cxt.set_terminal_exec_allowed(saved_terminal_exec); };

    let function_ret = function_body->evaluate(cxt);

    /* A return inside the body unwinds to here and supplies the status. A break
       or a continue is scoped to a loop inside this function, so it does not
       escape into a caller's loop and is consumed here. An exit is not ours and
       stays pending for the shell. */
    if (cxt.has_pending_control_flow()) {
      const control_flow::Kind kind = cxt.pending_control_flow().kind;
      if (kind == control_flow::Kind::Return) {
        function_ret = cxt.pending_control_flow().value;
        cxt.clear_control_flow();
      } else if (kind == control_flow::Kind::Break ||
                 kind == control_flow::Kind::Continue)
      {
        cxt.clear_control_flow();
      }
    }

    cxt.set_last_exit_status(static_cast<i32>(function_ret));
    return function_ret;
  }

  if (cxt.should_retitle_for_command())
    toiletline::set_title(utils::merge_args_to_string(program_args));

  /* Reuse a memoized resolution when the command word is unchanged, otherwise
     search PATH once and remember the result for the next run. */
  const bool is_cache_valid =
      m_resolved_kind.has_value() && program_args[0] == m_resolved_name;

  /* A command word that resolves to nothing is non-fatal. Report it to stderr,
     set status 127, and continue so the surrounding list, and-or chain, and
     command substitution proceed on the 127 the way a normal failing command
     drives them. The catch is narrow, so a real located error from elsewhere
     still aborts the command. */
  Maybe<ExecContext> resolved_ec;
  try {
    resolved_ec =
        is_cache_valid
            ? ExecContext::from_resolved(source_location(), *m_resolved_kind,
                                         steal(program_args))
            : ExecContext::make_from(source_location(), steal(program_args));
  } catch (const CommandNotFound &e) {
    report_command_not_found(cxt, e);
    cxt.set_last_exit_status(127);
    return 127;
  }
  let ec = resolved_ec.take();

  if (!is_cache_valid) {
    if (ec.is_builtin())
      m_resolved_kind = ResolvedCommand::from_builtin(ec.builtin_kind());
    else
      m_resolved_kind = ResolvedCommand::from_program(ec.program_path());
    /* The argument vector moved into the context, so the cached name reads from
       it there rather than from the now-emptied local. */
    m_resolved_name = ec.program();
  }

  /* A heredoc on the standard input passes its staged descriptor through the
     in_fd slot, which the exec context now owns and closes. The standard output
     and error redirects already took effect in source order on the real shell
     fds above and are restored by the defer, so they need no slot here. */
  if (redirect_in_fd) ec.in_fd = redirect_in_fd;
  redirect_in_fd_handed_off = true;

  const i64 ret = utils::execute_context(steal(ec), cxt, is_async());

  /* An assignment builtin with NAME=(...) array arguments applies them after it
     runs, in the scope the builtin selects. local binds a local array, and a
     declare or typeset inside a function binds a local while one at the top
     level reaches the global store, where export marks the name for the
     environment. The builtin ran first, so a local outside a function has
     already errored and the elements never reach here. */
  if (!m_array_args.is_empty()) {
    let const is_local = array_command_name == "local";
    let const is_declare =
        array_command_name == "declare" || array_command_name == "typeset";
    let const is_function_local = is_declare && cxt.in_function_scope();
    let const is_export = array_command_name == "export";
    /* readonly NAME=(...), and declare -r NAME=(...), mark the array name so a
       later assignment to it is rejected the way bash refuses a readonly. The
       -r flag sits in the builtin's arguments, so it is read off them. */
    let is_readonly_request = array_command_name == "readonly";
    /* The -A flag declares an associative array, so local -A m=() and declare
       -A m=([k]=v) route to the string-keyed store rather than the indexed one.
     */
    let is_associative_request = false;
    if (is_declare || is_local)
      for (const Token *arg : m_args) {
        let const text = arg->raw_string();
        if (text.length() >= 2 && text.view()[0] == '-') {
          if (!is_readonly_request &&
              text.view().find_character('r').has_value())
            is_readonly_request = true;
          if (text.view().find_character('A').has_value())
            is_associative_request = true;
        }
      }
    for (const array_builtin_assignment &assignment : m_array_args) {
      if (is_local || is_function_local) cxt.declare_local(assignment.name);
      ArrayList<String> values = cxt.process_args(assignment.elements);
      if (is_associative_request) {
        /* The array is keyed by string, so the declaration registers the name
           and each [key]=value element fills one entry. A bare element with no
           bracketed key becomes a key with an empty value. */
        cxt.declare_associative_array(assignment.name);
        for (const String &element : values) {
          const StringView text = element.view();
          if (!text.is_empty() && text[0] == '[') {
            if (let const close = text.find_character(']');
                close.has_value() && *close + 1 < text.length &&
                text[*close + 1] == '=')
            {
              cxt.set_associative_element(
                  assignment.name, text.substring_of_length(1, *close - 1),
                  text.substring(*close + 2));
              continue;
            }
          }
          cxt.set_associative_element(assignment.name, text, StringView{});
        }
      } else {
        cxt.assign_indexed_array_elements(assignment.name, steal(values),
                                          assignment.is_append);
      }
      if (is_export) cxt.mark_exported(assignment.name);
      if (is_readonly_request) cxt.mark_readonly(assignment.name);
    }
  }

  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn SimpleCommand::to_string() const throws -> String
{
  String s = "SimpleCommand";

  /* A pipeline stage that is a bare assignment carries the assignment in the
     local variables and has no command word, so the argument list is empty. */
  if (!m_args.is_empty()) {
    s += " \"" + m_args[0]->raw_string() + "\"";
    for (usize i = 1; i < m_args.count(); i++) {
      s += " \"";
      s += m_args[i]->raw_string();
      s += "\"";
    }
  }
  if (is_async()) s += ", Async";

  return s;
}

cold fn SimpleCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

fn SimpleCommand::append_to(usize d, String &f, bool duplicate) throws -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

fn SimpleCommand::redirect_to(usize d, String &f, bool duplicate) throws -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

CompoundList::CompoundList() : Expression({0, 0}) {}

/* The condition nodes live in the arena, torn down once on reset. */
CompoundList::~CompoundList() = default;

pure fn CompoundList::is_empty() const wontthrow -> bool
{
  return m_nodes.is_empty();
}

fn CompoundList::append_node(const CompoundListCondition *node) throws -> void
{
  ASSERT(node != nullptr);

  m_location.length += node->source_location().length;
  m_nodes.push(node);
}

cold fn CompoundList::to_string() const throws -> String
{
  return "CompoundList";
}

cold fn CompoundList::to_ast_string(usize layer) const throws -> String
{
  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[" + to_string() + "]";
  for (const CompoundListCondition *n : m_nodes) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + n->to_ast_string(layer + 1);
  }

  return s;
}

hot fn CompoundList::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_nodes.count() > 0);

  static const i64 NOTHING_WAS_EXECUTED = -256;

  i64 ret = NOTHING_WAS_EXECUTED;

  /* Only the last node of the list yields the list's status, so a terminal exec
     rides into that node alone. Every earlier node runs with the flag cleared
     and so forks normally, and the saved value is restored after the list so a
     containing tail position is not disturbed. */
  const bool was_terminal_exec_allowed = cxt.terminal_exec_allowed();
  cxt.set_terminal_exec_allowed(false);
  defer { cxt.set_terminal_exec_allowed(was_terminal_exec_allowed); };

  for (usize index = 0; index < m_nodes.count(); index++) {
    const CompoundListCondition *n = m_nodes[index];
    ASSERT(n != nullptr);

    const bool is_last_node = index + 1 >= m_nodes.count();
    cxt.set_terminal_exec_allowed(was_terminal_exec_allowed && is_last_node);

    /* An Or operand runs only after a failure and an And operand only after a
       success, so a short-circuited operand does not run at all. The flag marks
       whether this node ran, since set -e keys off the command that actually
       produced the status, not a status carried over from a short-circuited
       sibling. */
    bool did_execute = false;
    /* In bash mood an evaluation error fails the command and the list goes
       on, the way bash continues after a readonly assignment or a [[ ]]
       operand error, while a script-fatal error, the set -u read and the
       ${name:?} report, still aborts the run. The error renders to stderr
       here since nothing above will see it. */
    auto run_node = [&]() throws -> i64 {
      try {
        return n->evaluate(cxt);
      } catch (const ErrorBase &error) {
        if (!cxt.is_bash_compatible() || error.is_script_fatal()) throw;
        LOG(verbosity::Debug,
            "bash mood converted the error to command status %lld: %s",
            static_cast<long long>(error.command_status()),
            error.message().c_str());
        const String *source = cxt.current_source();
        show_message(
            error.to_string(source != nullptr ? source->view() : StringView{}));
        cxt.set_last_exit_status(static_cast<i32>(error.command_status()));
        return error.command_status();
      }
    };
    switch (n->kind()) {
    case CompoundListCondition::Kind::None:
      ret = run_node();
      did_execute = true;
      break;

    case CompoundListCondition::Kind::Or:
      if (ret != 0) {
        ret = run_node();
        did_execute = true;
      }
      break;

    case CompoundListCondition::Kind::And:
      if (ret == 0) {
        ret = run_node();
        did_execute = true;
      }
      break;
    }

    /* A break, continue, return, or exit inside a node stops the rest of the
       list and unwinds to the boundary that consumes it. */
    if (cxt.has_pending_control_flow()) break;

    /* POSIX exempts set -e for a command that is an operand of && or || and not
       the last command of the and-or list, and for a command the ! reserved
       word negates. So the exit fires only when the node that ran is the last
       of its chain, the next node starts a fresh chain or the list ends, and
       the command carries no leading !. A short-circuited node did not run and
       so cannot trigger the exit, which is what keeps a failing non-last
       operand such as the false in `false && cmd` from exiting the shell. */
    const bool ends_and_or_chain =
        index + 1 >= m_nodes.count() ||
        m_nodes[index + 1]->kind() == CompoundListCondition::Kind::None;
    if (cxt.error_exit() && !cxt.in_condition() && did_execute &&
        !n->is_negated() && ends_and_or_chain && ret != 0 &&
        ret != NOTHING_WAS_EXECUTED)
    {
      cxt.set_last_exit_status(static_cast<i32>(ret));
      if (cxt.in_subshell()) {
        cxt.request_exit(ret, source_location());
        break;
      }
      utils::quit(static_cast<i32>(ret), true);
    }
  }

  ASSERT(ret != NOTHING_WAS_EXECUTED);

  return ret;
}

CompoundListCondition::CompoundListCondition(SourceLocation location, Kind kind,
                                             const Command *expr)
    : Expression(location), m_kind(kind), m_cmd(expr)
{}

/* The command lives in the arena, torn down once on reset. */
CompoundListCondition::~CompoundListCondition() = default;

pure fn CompoundListCondition::kind() const wontthrow -> Kind { return m_kind; }

pure fn CompoundListCondition::is_negated() const wontthrow -> bool
{
  ASSERT(m_cmd != nullptr);
  return m_cmd->is_negated();
}

cold fn CompoundListCondition::to_string() const throws -> String
{
  String k;
  switch (kind()) {
  case Kind::None: k = "None"; break;
  case Kind::And: k = "&&"; break;
  case Kind::Or: k = "||"; break;
  default: unreachable();
  }
  return "CompoundListCondition, " + k;
}

cold fn CompoundListCondition::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_cmd != nullptr);

  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[" + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_cmd->to_ast_string(layer + 1);

  return s;
}

hot fn CompoundListCondition::evaluate_impl(EvalContext &cxt) const throws
    -> i64
{
  ASSERT(m_cmd != nullptr);

  /* A negated or timed command must run to completion here rather than exec the
     shell in place, since the inverse has to apply and the report has to print
     after the command returns, which an exec would skip. */
  if (m_cmd->is_negated() || m_cmd->is_timed())
    cxt.set_terminal_exec_allowed(false);

  /* The timed command samples the child cpu and the monotonic clock around its
     run, so the difference is its own wall and cpu time. */
  double user_before = 0.0;
  double system_before = 0.0;
  u64 start_nanos = 0;
  if (m_cmd->is_timed()) {
    os::children_cpu_seconds(user_before, system_before);
    start_nanos = os::monotonic_nanos();
  }

  let status = m_cmd->evaluate(cxt);

  if (m_cmd->is_timed()) {
    const u64 elapsed_nanos = os::monotonic_nanos() - start_nanos;
    double user_after = 0.0;
    double system_after = 0.0;
    os::children_cpu_seconds(user_after, system_after);
    const double real_seconds =
        static_cast<double>(elapsed_nanos) / 1000000000.0;
    let const report =
        m_cmd->time_uses_posix_format()
            ? utils::format_time_report_posix(real_seconds,
                                              user_after - user_before,
                                              system_after - system_before)
            : utils::format_time_report_pretty(real_seconds,
                                               user_after - user_before,
                                               system_after - system_before);
    print_error(report);
    flush();
  }

  /* A pipeline prefixed with ! reports the inverse of its status, and that
     inverse is what $? sees. */
  if (m_cmd->is_negated()) {
    status = (status == 0) ? 1 : 0;
    cxt.set_last_exit_status(static_cast<i32>(status));
  }

  return status;
}

Pipeline::Pipeline(SourceLocation location) : Command(location) {}

/* The stage commands live in the arena, torn down once on reset. */
Pipeline::~Pipeline() = default;

pure fn Pipeline::is_empty() const wontthrow -> bool
{
  return m_commands.is_empty();
}

fn Pipeline::append_command(const Command *node) throws -> void
{
  ASSERT(node != nullptr);

  m_location.length += node->source_location().length;
  m_commands.push(node);
}

cold fn Pipeline::to_string() const throws -> String
{
  let s = String{"Pipeline"};
  if (is_async()) s += ", Async";
  return s;
}

cold fn Pipeline::to_ast_string(usize layer) const throws -> String
{
  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[" + to_string() + "]";
  for (const Command *e : m_commands) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + e->to_ast_string(layer + 1);
  }

  return s;
}

/* Run a pipeline that has at least one compound stage. Every stage forks, the
   way an external command does, so a compound stage evaluates its tree in a
   child with the pipe already on its standard descriptors. A simple stage forks
   too here and evaluates in the child, which is the rare mixed pipeline rather
   than the common all-simple path that execute_contexts_with_pipes serves. */
cold fn Pipeline::evaluate_with_compound_stages(EvalContext &cxt) const throws
    -> i64
{
  LOG(verbosity::Debug, "forking %zu pipeline stages, one child per stage",
      m_commands.count());

  let children = ArrayList<os::process>{};
  os::process last_child = SHIT_INVALID_PROCESS;
  os::descriptor last_stdin = SHIT_INVALID_FD;

  /* A make_pipe or fork failure mid-loop leaves the previous stage's pipe read
     end open and the already-forked children unreaped. On the error path the
     open read end is closed and every spawned child is waited, then the error
     is rethrown so the caller still reports it. The success path falls through
     untouched. */
  try {
    for (usize stage_index = 0; stage_index < m_commands.count(); stage_index++)
    {
      const Command *stage = m_commands[stage_index];
      ASSERT(stage != nullptr);

      cxt.add_evaluated_expression();

      let const is_first = (stage_index == 0);
      let const is_last = (stage_index + 1 == m_commands.count());

      let stage_in = Maybe<os::descriptor>{};
      let stage_out = Maybe<os::descriptor>{};
      let pipe = Maybe<os::Pipe>{};

      if (!is_last) {
        pipe = os::make_pipe();
        if (!pipe) {
          throw ErrorWithLocation{stage->source_location(),
                                  "Could not open a pipe"};
        }
        stage_out = pipe->out;
      }
      if (!is_first) stage_in = last_stdin;

#if SHIT_PLATFORM_IS WIN32
      /* Windows has no fork. A stage whose full source span the parser recorded
         re-execs in a fresh shell with the pipe ends as its standard input and
         output. A stage without a recorded span, an if or a case, falls through
         to fork_compound_stage, which reports it unsupported. */
      const SourceLocation stage_location = stage->source_location();
      const String *stage_source = cxt.current_source();
      os::process child = SHIT_INVALID_PROCESS;
      if (stage_source != nullptr &&
          stage->source_end_position() >
              stage_location.position + stage_location.length)
      {
        const StringView stage_text = stage_source->view().substring_of_length(
            stage_location.position,
            stage->source_end_position() - stage_location.position);
        Maybe<os::process> spawned_stage = os::spawn_subshell_stage(
            stage_text, stage_in, stage_out, cxt.is_bash_compatible());
        if (!spawned_stage.has_value())
          throw ErrorWithLocation{
              stage_location, "Could not spawn the compound pipeline stage"};
        child = *spawned_stage;
      } else {
        child = os::fork_compound_stage(stage_in, stage_out, {});
      }
#else
      const os::process child =
          os::fork_compound_stage(stage_in, stage_out, {});

      if (child == 0) {
        /* The child evaluates the stage in a subshell, so a variable change
           such as a while-read does not escape, then exits with its status. A
           diagnostic or an exit request inside still yields a child status
           rather than unwinding back into the parent's evaluator. */
        i32 stage_status = 0;
        try {
          cxt.enter_subshell();
          stage_status = static_cast<i32>(stage->evaluate(cxt));
          if (cxt.has_pending_control_flow() &&
              cxt.pending_control_flow().kind == control_flow::Kind::Exit)
          {
            stage_status = static_cast<i32>(cxt.pending_control_flow().value);
          }
        } catch (const ErrorWithLocation &e) {
          const String *source = cxt.current_source();
          shit::show_message(
              e.to_string(source != nullptr ? source->view() : StringView{}));
          stage_status = 1;
        } catch (const Error &e) {
          shit::show_message(e.to_string());
          stage_status = 1;
        } catch (...) {
          LOG(verbosity::Debug,
              "swallowed an unknown error in the pipeline stage child");
          stage_status = 1;
        }
        shit::flush();
        os::exit_process_immediately(stage_status);
      }
#endif

      /* The parent keeps neither pipe end open past the stage that owns it,
         otherwise a reader never sees the writer close. */
      if (stage_out) os::close_fd(*stage_out);
      if (stage_in) os::close_fd(*stage_in);
      if (!is_last) last_stdin = pipe->in;

      children.push(child);
      last_child = child;
    }
  } catch (...) {
    if (last_stdin != SHIT_INVALID_FD) os::close_fd(last_stdin);
    for (const os::process child : children) {
      /* The wait reaps the child, so a spawned stage never lingers as a zombie
         when the pipeline aborts. A failure to wait must not mask the original
         error, so it is swallowed here. */
      try {
        os::wait_and_monitor_process(child);
      } catch (...) {
        LOG(verbosity::Debug,
            "swallowed a wait error while reaping an aborted pipeline child");
      }
    }
    throw;
  }

  if (is_async()) {
    if (last_child != SHIT_INVALID_PROCESS) {
      cxt.set_last_background_pid(os::process_id_of(last_child));
      const i32 id = cxt.register_job(last_child, "pipeline");
      if (cxt.shell_is_interactive())
        shit::print_error("[" + utils::int_to_text(id) + "] " +
                          utils::uint_to_text(
                              static_cast<u64>(os::process_id_of(last_child))) +
                          "\n");
    }
    return 0;
  }

  /* The children are pushed in pipeline order, so their statuses are collected
     in order. pipefail reports the rightmost stage that failed, or zero when
     all succeeded, while the plain case reports the last stage alone. */
  let stage_status = ArrayList<i32>{};
  stage_status.reserve(children.count());
  for (const os::process child : children)
    stage_status.push(os::wait_and_monitor_process(child));

  i32 ret = stage_status.is_empty() ? 0 : stage_status.back();
  if (cxt.pipefail()) {
    ret = 0;
    for (usize i = stage_status.count(); i > 0; i--)
      if (stage_status[i - 1] != 0) {
        ret = stage_status[i - 1];
        break;
      }
  }

  LOG(verbosity::Debug, "the pipeline stages were reaped, %s status is %d",
      cxt.pipefail() ? "the pipefail" : "the last stage's", ret);

  cxt.set_last_exit_status(ret);
  return ret;
}

hot fn Pipeline::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_commands.count() > 1);

  /* A pipeline wires pipes and forks each stage, so its last stage cannot
     replace the shell. Clear the terminal exec permission so no stage execs in
     place. */
  cxt.set_terminal_exec_allowed(false);

  /* A pipeline of only simple commands keeps the fast path, which wires the
     pipes once and forks each external directly with no extra subshell. A
     compound stage cannot run that way, so a pipeline that holds one takes the
     fork-per-stage path. A simple stage that carries a prefix assignment, such
     as x=v cmd | cmd2, takes the fork path too, since the fast path builds the
     stage from its argument words alone and the prefix must reach only that
     stage's environment, which the per-stage fork applies in the child. */
  bool has_compound_stage = false;
  for (const Command *stage : m_commands) {
    if (!stage->is_simple_command()) {
      has_compound_stage = true;
      break;
    }
    /* A command-less stage of bare assignments keeps the fast path, whose
       empty-expansion check reports it, so the strict diagnostic for x=1 | cat
       is preserved. */
    const SimpleCommand *simple = static_cast<const SimpleCommand *>(stage);
    if (!simple->local_vars().is_empty() && !simple->args().is_empty()) {
      has_compound_stage = true;
      break;
    }
  }

  /* A stage whose command word names a user function must run through the
     per-stage fork path, since a function shadows a builtin and a program
     inside a pipeline the same way it does outside one. The fast path below
     builds an ExecContext that resolves only builtins and programs, so a
     function stage would wrongly run a like-named builtin. The literal command
     word is checked without expanding, which covers the common name-pipe form,
     and the whole scan is skipped when no function is defined. */
  if (!has_compound_stage && cxt.has_functions()) {
    for (const Command *stage : m_commands) {
      const SimpleCommand *simple = static_cast<const SimpleCommand *>(stage);
      if (simple->args().is_empty()) continue;
      const Token *first = simple->args()[0];
      if (first->kind() != Token::Kind::Word) continue;
      const Word &word = static_cast<const tokens::WordToken *>(first)->word();
      if (word.plain_literal_kind() == Word::PlainLiteral::NotPlain) continue;
      if (cxt.find_function(word.to_literal_string().view()) != nullptr) {
        has_compound_stage = true;
        break;
      }
    }
  }

  LOG(verbosity::Debug, "the pipeline has %zu stages, taking the %s path",
      m_commands.count(),
      has_compound_stage ? "fork-per-stage" : "all-simple fast");

  if (has_compound_stage) return evaluate_with_compound_stages(cxt);

  /* The stage exec contexts and their argument words live only until the
     pipeline is wired and run, so they are built on the scratch arena under one
     pipeline-scoped mark. The arena rewind runs no destructor, so any stage
     still holding open descriptors on an early exit, a non-resolving command or
     an empty stage, is closed by the defer before the release. The normal path
     moves ecs into execute, leaving the defer's loop empty. */
  let const pipeline_mark = cxt.scratch_mark();
  let ecs = ArrayList<ExecContext>{cxt.scratch_allocator()};
  defer
  {
    for (ExecContext &leftover : ecs)
      leftover.close_fds();
    cxt.scratch_release(pipeline_mark);
  };
  ecs.reserve(m_commands.count());

  for (const Command *stage : m_commands) {
    ASSERT(stage != nullptr);
    ASSERT(stage->is_simple_command());
    const SimpleCommand *e = static_cast<const SimpleCommand *>(stage);

    cxt.add_evaluated_expression();

    let stage_args = cxt.process_args(e->args(), /*args_are_transient=*/true);

    /* A stage that expands to no command word, such as a bare assignment or an
       unset variable, has no program to run. Report it instead of building an
       exec context from an empty argument list, which would read past the
       arguments. */
    if (stage_args.is_empty()) {
      throw ErrorWithLocation{e->source_location(),
                              "A pipeline stage expanded to no command to run"};
    }

    /* A stage whose command does not resolve is non-fatal. Bash reports it,
       runs the rest of the pipeline, and reports the last stage's status, so
       the unresolved stage becomes a no-op context that closes its pipe to give
       the next stage EOF and contributes 127 only under pipefail. Aborting here
       instead would wrongly make the not-found stage govern the pipeline. */
    Maybe<ExecContext> stage_ec;
    try {
      stage_ec =
          ExecContext::make_from(e->source_location(), steal(stage_args));
    } catch (const CommandNotFound &not_found) {
      report_command_not_found(cxt, not_found);
      /* The stage still applies its own redirections, the way bash and dash
         open a > target even for a command that was not found, then runs
         nothing. The opened descriptors close with the stage, and a > onto its
         standard output takes the slot ahead of the pipe, so the next stage
         still sees EOF. */
      let unresolved = ExecContext::make_unresolved(e->source_location());
      bool unresolved_handed_off = false;
      defer
      {
        if (!unresolved_handed_off) unresolved.close_fds();
      };
      e->redirect_exec_context(unresolved, cxt);
      unresolved_handed_off = true;
      ecs.push(steal(unresolved));
      continue;
    }
    let ec = stage_ec.take();
    /* Apply this stage's own redirections, such as 2>&1, before the pipe wires
       its descriptors. The pipe only sets stdin and stdout, so a stderr
       redirection composes with it. A later redirection in the same stage may
       throw after an earlier one already opened a descriptor into the stage's
       slots, so the descriptors opened so far are closed on that throw rather
       than leaked, the way the simple command path guards its own descriptors.
       The guard is disarmed once the stage is handed into the pipeline. */
    bool stage_redirect_handed_off = false;
    defer
    {
      if (!stage_redirect_handed_off) ec.close_fds();
    };
    e->redirect_exec_context(ec, cxt);
    stage_redirect_handed_off = true;
    ecs.push(steal(ec));
  }

  /* The pipeline status is the last stage's, and $? reads it from the store, so
     the result is committed here the way the compound-stage path commits it.
     The all-simple fast path otherwise returned the status without recording
     it, so `false | read x; echo $?` read the stale status from before the
     pipeline. */
  const i64 ret =
      utils::execute_contexts_with_pipes(steal(ecs), cxt, is_async());
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

fn Pipeline::append_to(usize d, String &f, bool duplicate) throws -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

fn Pipeline::redirect_to(usize d, String &f, bool duplicate) throws -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

CompoundCommand::CompoundCommand(SourceLocation location) : Command(location) {}

fn CompoundCommand::append_to(usize d, String &f, bool duplicate) throws -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(),
                          "Redirection on a compound command is not supported"};
}

fn CompoundCommand::redirect_to(usize d, String &f, bool duplicate) throws
    -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(),
                          "Redirection on a compound command is not supported"};
}

IfClause::IfClause(SourceLocation location, ArrayList<if_branch> &&branches,
                   const Expression *otherwise)
    : CompoundCommand(location), m_branches(steal(branches)),
      m_otherwise(otherwise)
{}

/* The branch conditions, the branch bodies, and the else body live in the
   arena, torn down once on reset. */
IfClause::~IfClause() = default;

cold fn IfClause::to_string() const throws -> String { return "IfClause"; }

cold fn IfClause::to_ast_string(usize layer) const throws -> String
{
  let const pad = indent_for_layer(layer);
  let const child_pad = pad + EXPRESSION_AST_INDENT;
  let s = pad + "[" + to_string() + "]";
  for (const auto &[condition, body] : m_branches) {
    ASSERT(condition != nullptr);
    ASSERT(body != nullptr);

    s += "\n" + child_pad + condition->to_ast_string(layer + 1);
    s += "\n" + child_pad + body->to_ast_string(layer + 1);
  }

  if (m_otherwise != nullptr)
    s += "\n" + child_pad + m_otherwise->to_ast_string(layer + 1);

  return s;
}

hot fn IfClause::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  /* A command inside a compound construct forks rather than replacing the
     shell, so the terminal exec stays confined to a top-level simple command.
   */
  cxt.set_terminal_exec_allowed(false);

  /* The analyze pass proved which branch runs, so the conditions are skipped
     and the chosen body runs straight away. An index past the last branch means
     every condition failed, so the else body runs or the if yields 0. */
  if (m_folded_branch.has_value()) {
    LOG(verbosity::Debug,
        "running the folded if branch %zu of %zu without testing conditions",
        *m_folded_branch, m_branches.count());
    if (*m_folded_branch < m_branches.count())
      return m_branches[*m_folded_branch].body->evaluate(cxt);
    if (m_otherwise != nullptr) return m_otherwise->evaluate(cxt);
    return 0;
  }

  for (const auto &[condition, body] : m_branches) {
    ASSERT(condition != nullptr);
    ASSERT(body != nullptr);

    i64 condition_status;
    {
      cxt.enter_condition();
      defer { cxt.leave_condition(); };
      condition_status = condition->evaluate(cxt);
    }

    /* A jump inside the condition stops the if and stays pending. */
    if (cxt.has_pending_control_flow()) return condition_status;
    if (condition_status == 0) return body->evaluate(cxt);
  }

  if (m_otherwise != nullptr) return m_otherwise->evaluate(cxt);

  return 0;
}

cold fn IfClause::analyze(AnalysisContext &actx,
                          bool is_unconditional) const throws -> void
{
  /* The dead-branch rule reads the constant table while it still holds the
     values recorded before this if. The verdict of a condition is decided by
     the command words alone and does not need the condition's own analyze to
     have run, so the fold runs first, before any child analyze mutates the
     table. */
  optimizer::optimize_node(this, actx);

  /* The first condition runs whenever the if runs. The elif conditions and all
     bodies are conditional, since a branch may not be reached. */
  let is_first_branch = true;
  for (const auto &[condition, body] : m_branches) {
    ASSERT(condition != nullptr);
    ASSERT(body != nullptr);

    condition->analyze(actx, is_unconditional && is_first_branch);
    body->analyze(actx, false);
    is_first_branch = false;
  }

  if (m_otherwise != nullptr) m_otherwise->analyze(actx, false);

  /* A branch ran conditionally and may have reassigned a name, so a value
     recorded before this if is no longer proven to hold in the straight-line
     block after it. Clearing the constant table at the if boundary keeps the
     propagation to a single straight-line run. */
  actx.constant_variables.clear();
}

cold fn IfClause::register_defined_functions(AnalysisContext &actx) const throws
    -> void
{
  /* Every branch body and the conditions run in the current shell, so a
     function defined in any of them is callable from a sibling and must be
     registered before the ordered walk warns about a forward reference. */
  for (const auto &[condition, body] : m_branches) {
    ASSERT(condition != nullptr);
    ASSERT(body != nullptr);
    condition->register_defined_functions(actx);
    body->register_defined_functions(actx);
  }

  if (m_otherwise != nullptr) m_otherwise->register_defined_functions(actx);
}

pure fn IfClause::branches() const wontthrow -> const ArrayList<if_branch> &
{
  return m_branches;
}

pure fn IfClause::otherwise() const wontthrow -> const Expression *
{
  return m_otherwise;
}

fn IfClause::set_folded_branch(usize index) const wontthrow -> void
{
  m_folded_branch = index;
}

pure fn IfClause::has_folded_branch() const wontthrow -> bool
{
  return m_folded_branch.has_value();
}

fn IfClause::as_if_clause() const wontthrow -> const IfClause * { return this; }

WhileLoop::WhileLoop(SourceLocation location, const Expression *condition,
                     const Expression *body, bool is_until)
    : CompoundCommand(location), m_condition(condition), m_body(body),
      m_is_until(is_until)
{}

/* The condition and the body live in the arena, torn down once on reset. */
WhileLoop::~WhileLoop() = default;

cold fn WhileLoop::to_string() const throws -> String
{
  return m_is_until ? "UntilLoop" : "WhileLoop";
}

cold fn WhileLoop::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_body != nullptr);

  let const pad = indent_for_layer(layer);
  let const child_pad = pad + EXPRESSION_AST_INDENT;
  let s = pad + "[" + to_string() + "]";
  s += "\n" + child_pad + m_condition->to_ast_string(layer + 1);
  s += "\n" + child_pad + m_body->to_ast_string(layer + 1);
  return s;
}

namespace {

/* What a loop does with the control flow pending after its body ran. */
enum class loop_disposition
{
  /* No jump, or a continue aimed here, so run the next iteration. */
  RunNext,
  /* A break aimed here, or a jump aimed at an outer loop that is now left
     pending, so this loop stops. */
  StopLoop,
};

fn resolve_loop_control(EvalContext &cxt) throws -> loop_disposition
{
  if (!cxt.has_pending_control_flow()) return loop_disposition::RunNext;

  let &control = cxt.pending_control_flow();
  if (control.kind != control_flow::Kind::Break &&
      control.kind != control_flow::Kind::Continue)
  {
    /* A return or an exit is not a loop's to consume, so this loop stops and
       leaves it pending for the function or the shell. */
    return loop_disposition::StopLoop;
  }

  /* A jump aimed at an outer loop decrements and stays pending, stopping this
     loop so the outer one consumes it. */
  if (control.value > 1) {
    control.value -= 1;
    LOG(verbosity::All,
        "the loop jump targets an outer loop, %lld levels stay pending",
        static_cast<long long>(control.value));
    return loop_disposition::StopLoop;
  }

  /* The jump targets this loop. A break stops it, a continue runs the next
     iteration. Either way the request is consumed here. */
  let const is_break = control.kind == control_flow::Kind::Break;
  cxt.clear_control_flow();
  LOG(verbosity::All, "consuming the %s aimed at this loop",
      is_break ? "break" : "continue");
  return is_break ? loop_disposition::StopLoop : loop_disposition::RunNext;
}

} /* namespace */

hot fn WhileLoop::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_body != nullptr);

  /* A loop body runs repeatedly in the shell process, so no command in it may
     replace the shell. */
  cxt.set_terminal_exec_allowed(false);

  LOG(verbosity::Debug, "entering the %s loop%s",
      m_is_until ? "until" : "while",
      m_folded_to_skip ? ", folded to skip the body" : "");

  /* The analyze pass proved the body never runs, so the loop yields 0 without
     evaluating the condition. A while false and an until true fold here. */
  if (m_folded_to_skip) {
    cxt.set_last_exit_status(0);
    return 0;
  }

  /* The body runs inside one more loop level, so a break or a continue clamps
     its level against the nesting that is actually live. */
  cxt.enter_loop();
  defer { cxt.leave_loop(); };

  i64 ret = 0;
  for (;;) {
    i64 condition_status;
    {
      cxt.enter_condition();
      defer { cxt.leave_condition(); };
      condition_status = m_condition->evaluate(cxt);
    }
    /* A jump inside the condition, such as an exit from a substitution, stops
       the loop and stays pending for the caller. */
    if (cxt.has_pending_control_flow()) break;

    let const should_run_body =
        m_is_until ? (condition_status != 0) : (condition_status == 0);
    if (!should_run_body) break;

    ret = m_body->evaluate(cxt);
    if (resolve_loop_control(cxt) == loop_disposition::StopLoop) break;
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn WhileLoop::analyze(AnalysisContext &actx,
                           bool is_unconditional) const throws -> void
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_body != nullptr);

  /* The loop re-evaluates its condition every iteration and the body may
     reassign any name, so a value recorded before the loop does not hold across
     iterations. The constant table is cleared before the condition, so a
     pre-loop constant is never inlined into it, which would freeze a loop whose
     counter was folded to its initial value. A literal condition such as while
     false still folds, since the verdict reads the literal words alone. */
  actx.constant_variables.clear();

  optimizer::optimize_node(this, actx);

  m_condition->analyze(actx, is_unconditional);
  m_body->analyze(actx, false);
}

cold fn WhileLoop::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_body != nullptr);

  /* The condition and the body both run in the current shell, so a function
     defined in either is registered before the ordered walk. */
  m_condition->register_defined_functions(actx);
  m_body->register_defined_functions(actx);
}

pure fn WhileLoop::condition() const wontthrow -> const Expression *
{
  return m_condition;
}

pure fn WhileLoop::is_until() const wontthrow -> bool { return m_is_until; }

fn WhileLoop::set_folded_to_skip() const wontthrow -> void
{
  m_folded_to_skip = true;
}

pure fn WhileLoop::is_folded_to_skip() const wontthrow -> bool
{
  return m_folded_to_skip;
}

fn WhileLoop::as_while_loop() const wontthrow -> const WhileLoop *
{
  return this;
}

SelectLoop::SelectLoop(SourceLocation location, StringView variable_name,
                       ArrayList<const Token *> &&words, bool has_in_clause,
                       const Expression *body)
    : CompoundCommand(location), m_variable_name(variable_name),
      m_has_in_clause(has_in_clause), m_body(body)
{
  m_words = steal(words);
}

SelectLoop::~SelectLoop() = default;

cold fn SelectLoop::to_string() const throws -> String
{
  return "SelectLoop \"" + StringView{m_variable_name} + "\"";
}

cold fn SelectLoop::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);
  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

fn SelectLoop::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);
  cxt.set_terminal_exec_allowed(false);

  /* The word list expands here, so a runtime warning from it carets this
     select rather than the statement before it. */
  cxt.set_current_location(source_location());

  let const values =
      m_has_in_clause ? cxt.process_args(m_words) : cxt.positional_params();
  if (values.is_empty()) return 0;

  LOG(verbosity::Debug, "the select loop offers %zu choices for '%s'",
      values.count(), m_variable_name.c_str());

  cxt.enter_loop();
  defer { cxt.leave_loop(); };

  i64 ret = 0;
  bool reprint_menu = true;
  for (;;) {
    /* The numbered menu and the prompt go to standard error, the way bash keeps
       them out of the command's captured output. The menu reprints only after
       an empty line. */
    if (reprint_menu) {
      let menu = String{};
      for (usize i = 0; i < values.count(); i++) {
        menu += utils::int_to_text(static_cast<i64>(i + 1));
        menu += ") ";
        menu.append(values[i].view());
        menu += '\n';
      }
      shit::print_error(menu.view());
      reprint_menu = false;
    }
    shit::print_error(cxt.get_variable_value("PS3").value_or(String{"#? "}));

    bool was_newline_terminated = false;
    let const input =
        utils::read_line_from_fd(SHIT_STDIN, was_newline_terminated);
    /* End of input ends the loop, and bash echoes a newline to standard output
       the way a terminal end-of-file does, so the captured output gains a final
       blank line that a break does not. */
    if (!input) {
      shit::print("\n");
      break;
    }

    let const reply = String{StringView{*input}};
    LOG(verbosity::All, "the select prompt read the reply '%s'", reply.c_str());
    cxt.set_shell_variable("REPLY", reply.view());
    if (reply.is_empty()) {
      reprint_menu = true;
      continue;
    }

    /* A line that is a valid menu number binds the name to that word, any other
       input binds the name to the empty string, the way bash reports a bad
       choice. */
    let const choice = utils::parse_decimal_integer(reply.view());
    if (!choice.is_error() && choice.value() >= 1 &&
        static_cast<usize>(choice.value()) <= values.count())
    {
      cxt.set_shell_variable(
          m_variable_name,
          values[static_cast<usize>(choice.value()) - 1].view());
    } else {
      cxt.set_shell_variable(m_variable_name, "");
    }

    ret = m_body->evaluate(cxt);
    if (resolve_loop_control(cxt) == loop_disposition::StopLoop) break;
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

ForLoop::ForLoop(SourceLocation location, StringView variable_name,
                 ArrayList<const Token *> &&words, bool has_in_clause,
                 const Expression *body)
    : CompoundCommand(location), m_variable_name(variable_name),
      m_has_in_clause(has_in_clause), m_body(body)
{
  m_words = steal(words);
}

/* The word tokens and the body live in the arena, torn down once on reset. */
ForLoop::~ForLoop() = default;

cold fn ForLoop::to_string() const throws -> String
{
  let result = String{"ForLoop \""};
  result += StringView{m_variable_name};
  result += "\"";
  return result;
}

cold fn ForLoop::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);

  let const pad = indent_for_layer(layer);
  let s = pad + "[" + to_string() + "]";
  s += "\n" + pad + EXPRESSION_AST_INDENT + m_body->to_ast_string(layer + 1);
  return s;
}

hot fn ForLoop::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  /* A loop body runs repeatedly in the shell process, so no command in it may
     replace the shell. */
  cxt.set_terminal_exec_allowed(false);
  /* The word list expands here, so a runtime warning from it carets this for
     rather than the statement before it. */
  cxt.set_current_location(source_location());
  /* Without an in clause the loop walks the positional parameters. */
  let const values =
      m_has_in_clause ? cxt.process_args(m_words) : cxt.positional_params();

  /* The default mood scopes the loop variable, putting its prior value back
     once the loop ends so the name does not leak, while the bash and posix
     moods leave it set the way those shells do. */
  let const scope_variable = !(cxt.is_bash_compatible() || cxt.is_posix_mode());
  Maybe<String> saved_value =
      scope_variable ? cxt.get_variable_value(m_variable_name) : None;
  defer
  {
    if (scope_variable) {
      if (saved_value.has_value())
        cxt.set_shell_variable(m_variable_name, saved_value->view());
      else
        cxt.unset_shell_variable(m_variable_name);
    }
  };

  LOG(verbosity::Debug, "the for loop binds '%s' over %zu values",
      m_variable_name.c_str(), values.count());

  /* The body runs inside one more loop level, so a break or a continue clamps
     its level against the nesting that is actually live. */
  cxt.enter_loop();
  defer { cxt.leave_loop(); };

  i64 ret = 0;
  for (const String &value : values) {
    cxt.set_shell_variable(m_variable_name, value);
    ret = m_body->evaluate(cxt);
    if (resolve_loop_control(cxt) == loop_disposition::StopLoop) break;
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn ForLoop::analyze(AnalysisContext &actx,
                         bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  unused(is_unconditional);

  /* for over the words of an unquoted command substitution iterates IFS-split
     words rather than lines, so a name with a space breaks apart. A $(cat
     file) is shellcheck SC2013, read the lines with while read -r. A
     $(find ...) is shellcheck SC2044, use find -exec or a read loop. */
  for (const Token *t : m_words) {
    if (t->kind() != Token::Kind::Word) continue;
    let const &word = static_cast<const tokens::WordToken *>(t)->word();
    for (const WordSegment &segment : word.segments) {
      if (segment.kind != WordSegment::Kind::CommandSubstitution ||
          segment.is_in_double_quotes)
        continue;
      let const body = segment.text.view();
      usize start = 0;
      while (start < body.length && (body[start] == ' ' || body[start] == '\t'))
        start++;
      let const trimmed = body.substring(start);
      if (trimmed.starts_with(StringView{"cat "}))
        actx.warn(t->source_location(),
                  "A for over the cat output iterates IFS-split words rather "
                  "than lines, read the lines with 'while IFS= read -r line' "
                  "instead");
      else if (trimmed.starts_with(StringView{"find "}) || trimmed == "find")
        actx.warn(t->source_location(),
                  "A for over the find output breaks a name with whitespace "
                  "apart, use find -exec or a 'while read -r' loop over "
                  "find -print0");
    }
  }

  /* The body runs repeatedly over the loop list and may reassign a name, so a
     value recorded before the loop does not hold inside it. Clearing the
     constant table before the body keeps a pre-loop constant from being inlined
     into a counter the body increments. */
  actx.constant_variables.clear();
  m_body->analyze(actx, false);
}

cold fn ForLoop::register_defined_functions(AnalysisContext &actx) const throws
    -> void
{
  ASSERT(m_body != nullptr);

  /* The loop body runs in the current shell, so a function it defines is
     registered before the ordered walk. */
  m_body->register_defined_functions(actx);
}

CaseClause::CaseClause(SourceLocation location, const Token *word,
                       ArrayList<case_item> &&items)
    : CompoundCommand(location), m_word(word)
{
  m_items = steal(items);
}

/* The subject word, the pattern tokens, and the item bodies live in the arena,
   torn down once on reset. */
CaseClause::~CaseClause() = default;

cold fn CaseClause::to_string() const throws -> String { return "CaseClause"; }

cold fn CaseClause::to_ast_string(usize layer) const throws -> String
{
  let const pad = indent_for_layer(layer);
  let const child_pad = pad + EXPRESSION_AST_INDENT;
  let s = pad + "[" + to_string() + "]";
  for (const case_item &item : m_items) {
    ASSERT(item.body != nullptr);
    s += "\n" + child_pad + item.body->to_ast_string(layer + 1);
  }
  return s;
}

fn CaseClause::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_word != nullptr);

  /* A command inside a case branch forks rather than replacing the shell, so
     the terminal exec stays confined to a top-level simple command. */
  cxt.set_terminal_exec_allowed(false);

  /* The subject word and the patterns expand here, so a runtime warning from
     them carets this case rather than the statement before it. */
  cxt.set_current_location(source_location());

  /* A case word and its patterns expand with variables and tilde but no field
     splitting and no pathname globbing, so a pattern keeps its metacharacters
     for matching. */
  auto expand_no_glob = [&cxt](const Token *t) -> String {
    ASSERT(t != nullptr);
    if (t->kind() == Token::Kind::Word) {
      /* The subject expansion throws a plain Error, an unset variable under set
         -u, so it is relocated to a caret at the case word. */
      try {
        return cxt.expand_word_for_assignment(
            static_cast<const tokens::WordToken *>(t)->word());
      } catch (const Error &e) {
        throw relocate_error(e, t->source_location());
      }
    }
    return t->raw_string();
  };

  let const subject = expand_no_glob(m_word);

  LOG(verbosity::Debug, "the case subject expanded to '%s'", subject.c_str());

  auto arm_matches = [&](const case_item &item) throws -> bool {
    for (const Token *pattern_token : item.patterns) {
      /* A pattern keeps its glob metacharacters for matching, yet a quoted or
         escaped metacharacter in the pattern is a literal, so the expansion
         carries a parallel mask the matcher reads. A pattern token that is not
         a plain word, such as a reserved word arm, has no quoting structure and
         stays fully active. */
      let pattern_active = ArrayList<bool>{cxt.scratch_allocator()};
      let pattern = String{};
      if (pattern_token->kind() == Token::Kind::Word) {
        try {
          pattern = cxt.expand_case_pattern_masked(
              static_cast<const tokens::WordToken *>(pattern_token)->word(),
              pattern_active);
        } catch (const Error &e) {
          throw relocate_error(e, pattern_token->source_location());
        }
      } else {
        pattern = pattern_token->raw_string();
        for (usize k = 0; k < pattern.count(); k++)
          pattern_active.push(true);
      }
      if (utils::glob_matches(pattern, subject, pattern_active, 0,
                              cxt.extglob_enabled()))
        return true;
    }
    return false;
  };

  /* The index walks the arms so a ;& fall-through can run the next arm body
     without matching it, and a ;;& can resume matching at the arms past the one
     that just ran. */
  i64 result = 0;
  bool did_run_a_body = false;
  usize i = 0;
  while (i < m_items.count()) {
    if (!arm_matches(m_items[i])) {
      i++;
      continue;
    }

    LOG(verbosity::All, "case arm %zu matched, running its body", i);

    bool should_resume_matching = false;
    for (;;) {
      ASSERT(m_items[i].body != nullptr);
      result = m_items[i].body->evaluate(cxt);
      cxt.set_last_exit_status(static_cast<i32>(result));
      did_run_a_body = true;

      let const terminator = m_items[i].terminator;
      if (terminator == case_terminator::FallThrough && i + 1 < m_items.count())
      {
        i++;
        continue;
      }
      if (terminator == case_terminator::ContinueMatch) {
        i++;
        should_resume_matching = true;
      }
      break;
    }
    if (should_resume_matching) continue;
    return result;
  }

  if (!did_run_a_body) {
    LOG(verbosity::Debug, "no case arm matched the subject");
    cxt.set_last_exit_status(0);
  }
  return result;
}

cold fn CaseClause::analyze(AnalysisContext &actx,
                            bool is_unconditional) const throws -> void
{
  unused(is_unconditional);
  for (const case_item &item : m_items) {
    ASSERT(item.body != nullptr);
    item.body->analyze(actx, false);
  }

  /* A case with no catch-all *) arm silently does nothing for a value none of
     the listed patterns match, so a typo or a new input slips through. This is
     shellcheck SC2249. The catch-all is an unquoted * glob, a single
     UnquotedText segment whose text is *. A quoted '*' matches only a literal
     asterisk, so it is not a default. */
  bool has_default_arm = false;
  for (const case_item &item : m_items) {
    for (const Token *pattern : item.patterns) {
      if (pattern->kind() != Token::Kind::Word) continue;
      let const &pattern_word =
          static_cast<const tokens::WordToken *>(pattern)->word();
      if (pattern_word.segments.count() == 1 &&
          pattern_word.segments[0].kind == WordSegment::Kind::UnquotedText &&
          pattern_word.segments[0].text.view() == "*")
      {
        has_default_arm = true;
        break;
      }
    }
    if (has_default_arm) break;
  }
  if (!has_default_arm) {
    ASSERT(m_word != nullptr);
    actx.warn(m_word->source_location(),
              "This case has no default *) branch, a value no pattern "
              "matches "
              "is silently ignored");
  }

  /* An arm body runs only when its pattern matches and may reassign a name, so
     a value recorded before the case is no longer proven after it. */
  actx.constant_variables.clear();
}

cold fn CaseClause::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  /* Each arm body runs in the current shell when its pattern matches, so a
     function defined in any arm is registered before the ordered walk. */
  for (const case_item &item : m_items) {
    ASSERT(item.body != nullptr);
    item.body->register_defined_functions(actx);
  }
}

BraceGroup::BraceGroup(SourceLocation location, const Expression *body)
    : CompoundCommand(location), m_body(body)
{}

/* The body lives in the arena, torn down once on reset. */
BraceGroup::~BraceGroup() = default;

cold fn BraceGroup::to_string() const throws -> String { return "BraceGroup"; }

cold fn BraceGroup::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);

  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

fn BraceGroup::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  /* A command inside a brace group forks rather than replacing the shell, so
     the terminal exec stays confined to a top-level simple command. */
  cxt.set_terminal_exec_allowed(false);

  return m_body->evaluate(cxt);
}

cold fn BraceGroup::analyze(AnalysisContext &actx,
                            bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  m_body->analyze(actx, is_unconditional);
}

cold fn BraceGroup::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_body != nullptr);

  /* A brace group runs in the current shell, so a function defined in its body
     leaks to the enclosing scope and is registered before the ordered walk.
     Subshell does not forward here, since a function defined in a subshell does
     not escape it. */
  m_body->register_defined_functions(actx);
}

ConditionalCommand::ConditionalCommand(SourceLocation location,
                                       ArrayList<conditional_element> elements)
    : CompoundCommand(location), m_elements(steal(elements))
{}

ConditionalCommand::~ConditionalCommand() = default;

cold fn ConditionalCommand::to_string() const throws -> String
{
  return "ConditionalCommand";
}

cold fn ConditionalCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + "]";
}

fn ConditionalCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  /* The [[ ]] evaluator reports a malformed expression as a plain error, so it
     is relocated to this command's position to carry a caret at the [[ in a
     script, the same as the shell's other located diagnostics. A diagnostic
     that is already located passes through untouched. The operand expansion
     also reads variables, so the current location moves here first and a
     runtime warning carets this [[ rather than the statement before it. */
  cxt.set_current_location(source_location());
  i64 status;
  try {
    /* A true conditional exits zero, the way the [[ ]] command reports success.
     */
    status = cxt.evaluate_conditional(m_elements) ? 0 : 1;
  } catch (const Error &e) {
    throw relocate_error(e, source_location());
  }
  LOG(verbosity::Debug, "the [[ ]] conditional yielded status %lld",
      static_cast<long long>(status));
  cxt.set_last_exit_status(static_cast<i32>(status));
  return status;
}

ArithmeticCommand::ArithmeticCommand(SourceLocation location, String expression)
    : CompoundCommand(location), m_expression(steal(expression))
{}

ArithmeticCommand::~ArithmeticCommand() = default;

cold fn ArithmeticCommand::to_string() const throws -> String
{
  return "ArithmeticCommand";
}

cold fn ArithmeticCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + " \"" +
         m_expression.view() + "\"]";
}

/* A clause that holds only spaces or tabs is treated as omitted, the way bash
   reads for (( ; ; )) as an empty header rather than three blank expressions.
 */
static pure fn is_blank_clause(StringView text) wontthrow -> bool
{
  for (usize i = 0; i < text.length; i++)
    if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n') return false;
  return true;
}

fn ArithmeticCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  LOG(verbosity::Debug, "evaluating the arithmetic command '%s'",
      m_expression.c_str());

  /* The expression reads variables, so a runtime warning from it carets this
     (( )) rather than the statement before it. */
  cxt.set_current_location(source_location());

  /* An empty (( )) is a failure with no evaluation, the way bash reads it as a
     null expression that yields status 1 rather than a parse error. */
  if (is_blank_clause(m_expression.view())) {
    cxt.set_last_exit_status(1);
    return 1;
  }

  /* A non-zero arithmetic value is success, a zero value is failure, the
     opposite of the value-to-status convention of the rest of the shell. The
     arithmetic evaluator reports a malformed expression or a division by zero
     as a plain error, so it is relocated to this command's position to carry a
     caret at the (( in the source, including the source an eval runs. */
  i64 value;
  try {
    value = cxt.evaluate_arithmetic(m_expression.view());
  } catch (const Error &e) {
    throw relocate_error(e, source_location());
  }
  const i64 status = value != 0 ? 0 : 1;
  cxt.set_last_exit_status(static_cast<i32>(status));
  return status;
}

cold fn ArithmeticCommand::analyze(AnalysisContext &actx,
                                   bool is_unconditional) const throws -> void
{
  unused(is_unconditional);
  /* The expression may assign any name, as in (( i = 10 )), and the prepass
     does not parse it, so every recorded constant is forgotten rather than risk
     folding a later read to a value this command overwrote. */
  actx.constant_variables.clear();
}

cold fn SelectLoop::analyze(AnalysisContext &actx,
                            bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);
  unused(is_unconditional);
  /* The body runs repeatedly and may reassign a name, so a value recorded
     before the loop does not hold inside it, the same reason ForLoop clears the
     constant table before analyzing its body. */
  actx.constant_variables.clear();
  m_body->analyze(actx, false);
}

cold fn SelectLoop::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_body != nullptr);
  m_body->register_defined_functions(actx);
}

CStyleForLoop::CStyleForLoop(SourceLocation location, String init,
                             String condition, String step,
                             const Expression *body)
    : CompoundCommand(location), m_init(steal(init)),
      m_condition(steal(condition)), m_step(steal(step)), m_body(body)
{}

CStyleForLoop::~CStyleForLoop() = default;

cold fn CStyleForLoop::to_string() const throws -> String
{
  return "CStyleForLoop";
}

cold fn CStyleForLoop::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);
  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + " \"" + m_init.view() + ";" +
         m_condition.view() + ";" + m_step.view() + "\"]\n" + pad +
         EXPRESSION_AST_INDENT + m_body->to_ast_string(layer + 1);
}

ArrayAssignCommand::ArrayAssignCommand(SourceLocation location, StringView name,
                                       ArrayList<const Token *> elements,
                                       bool is_append)
    : Command(location), m_name(name), m_elements(steal(elements)),
      m_is_append(is_append)
{}

ArrayAssignCommand::~ArrayAssignCommand() = default;

cold fn ArrayAssignCommand::to_string() const throws -> String
{
  return "ArrayAssignCommand";
}

cold fn ArrayAssignCommand::to_ast_string(usize layer) const throws -> String
{
  return indent_for_layer(layer) + "[" + to_string() + " " + m_name.view() +
         (m_is_append ? "+=(...)" : "=(...)") + "]";
}

fn ArrayAssignCommand::redirect_to(usize d, String &f, bool duplicate) throws
    -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

fn ArrayAssignCommand::append_to(usize d, String &f, bool duplicate) throws
    -> void
{
  redirect_to(d, f, duplicate);
}

fn ArrayAssignCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  /* The element words expand here, so a runtime warning from them carets this
     assignment rather than the statement before it. */
  cxt.set_current_location(source_location());

  /* The elements expand the way command arguments do, with field splitting and
     globbing, so a=( $list *.txt ) builds the array bash would. */
  ArrayList<String> values = cxt.process_args(m_elements);
  LOG(verbosity::Debug, "assigning %zu elements to the array '%s'",
      values.count(), m_name.c_str());
  cxt.assign_indexed_array_elements(m_name.view(), steal(values), m_is_append);
  cxt.set_last_exit_status(0);
  return 0;
}

cold fn ArrayAssignCommand::analyze(AnalysisContext &actx,
                                    bool is_unconditional) const throws -> void
{
  unused(is_unconditional);
  /* An array assignment makes the name no longer a scalar literal, so the
     constant table forgets it rather than letting a prior scalar constant fold
     a later $name or $((name)) to a stale value. */
  actx.constant_variables.erase(m_name.view());
}

fn CStyleForLoop::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  /* A loop body runs repeatedly in the shell process, so no command in it may
     replace the shell. */
  cxt.set_terminal_exec_allowed(false);

  /* The three arithmetic sections read variables, so a runtime warning from
     them carets this loop rather than the statement before it. */
  cxt.set_current_location(source_location());

  LOG(verbosity::Debug,
      "entering the c-style for loop with init '%s', condition '%s', step "
      "'%s'",
      m_init.c_str(), m_condition.c_str(), m_step.c_str());

  if (!is_blank_clause(m_init.view())) cxt.evaluate_arithmetic(m_init.view());

  /* The body runs inside one more loop level, so a break or a continue clamps
     its level against the nesting that is actually live. */
  cxt.enter_loop();
  defer { cxt.leave_loop(); };

  i64 ret = 0;
  /* An empty condition is always true, the way for ((;;)) loops forever. */
  while (is_blank_clause(m_condition.view()) ||
         cxt.evaluate_arithmetic(m_condition.view()) != 0)
  {
    ret = m_body->evaluate(cxt);
    if (resolve_loop_control(cxt) == loop_disposition::StopLoop) break;
    /* The step runs after the body on every iteration, including one ended by a
       continue, the way bash advances the counter. */
    if (!is_blank_clause(m_step.view())) cxt.evaluate_arithmetic(m_step.view());
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn CStyleForLoop::analyze(AnalysisContext &actx,
                               bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);
  unused(is_unconditional);
  /* The header and body reassign the counter on every iteration, so a constant
     recorded before the loop does not hold inside or after it, the same reason
     ForLoop clears the constant table before analyzing its body. */
  actx.constant_variables.clear();
  m_body->analyze(actx, false);
}

cold fn CStyleForLoop::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_body != nullptr);
  m_body->register_defined_functions(actx);
}

Subshell::Subshell(SourceLocation location, const Expression *body)
    : CompoundCommand(location), m_body(body)
{}

/* The body lives in the arena, torn down once on reset. */
Subshell::~Subshell() = default;

cold fn Subshell::to_string() const throws -> String { return "Subshell"; }

cold fn Subshell::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);

  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

fn Subshell::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  /* A subshell isolates its commands, so none of them may replace the shell
     process. The in_subshell guard already blocks the terminal exec, and the
     flag is cleared here as well so the intent reads at the boundary. */
  cxt.set_terminal_exec_allowed(false);

  /* This shell has no process-level subshell, so isolate by snapshot. A cd or
     an assignment inside does not leak, but the exit status propagates. An exit
     inside ends only the subshell. */
  /* A loop in the parent is not the subshell's to break, so the body runs with
     a fresh loop count and the parent's count returns afterward. */
  let const saved_loop_depth = cxt.loop_depth();
  cxt.set_loop_depth(0);
  defer { cxt.set_loop_depth(saved_loop_depth); };

  LOG(verbosity::Debug, "entering the snapshot subshell");

  let snapshot = cxt.snapshot_state();

  /* The subshell body's transient scratch is reclaimed at the boundary, so a
     ( ... ) inside a loop does not grow the arena across iterations. The status
     is an integer and restore_state reverts the inner state, so nothing the
     release frees is still read. The defer runs after restore_state on both the
     normal and the thrown path. */
  let const subshell_mark = cxt.scratch_mark();
  defer { cxt.scratch_release(subshell_mark); };
  cxt.enter_subshell();
  /* The inherited EXIT action belongs to the parent, so it does not fire at the
     subshell's end. An EXIT action the body sets survives this clear and fires
     below. */
  cxt.clear_inherited_exit_trap();
  i64 ret = 0;
  /* A diagnostic thrown by the body, such as a readonly violation or a missing
     command, must still restore the snapshot and leave the subshell, otherwise
     the parent stays stuck in subshell mode with the inner state leaked. In
     bash mood a script-fatal error, the set -u read and the ${name:?} report,
     ends only the subshell the way bash confines the abort to the child
     process, reported here as status 1 the way bash answers it. */
  try {
    ret = m_body->evaluate(cxt);
  } catch (const ErrorBase &error) {
    /* A forked subshell would confine the abort to the child, so the snapshot
       subshell confines a script-fatal error the same way in every mood,
       status 1 the way bash answers it and 2 the way dash does, which the
       default mood follows. */
    if (error.is_script_fatal()) {
      LOG(verbosity::Debug, "the subshell confined a script-fatal error: %s",
          error.message().c_str());
      const String *source = cxt.current_source();
      show_message(
          error.to_string(source != nullptr ? source->view() : StringView{}));
      ret = cxt.is_bash_compatible() ? 1 : 2;
      cxt.set_last_exit_status(static_cast<i32>(ret));
      cxt.clear_control_flow();
    } else {
      cxt.run_subshell_exit_trap();
      cxt.leave_subshell();
      cxt.restore_state(steal(snapshot));
      throw;
    }
  } catch (...) {
    cxt.run_subshell_exit_trap();
    cxt.leave_subshell();
    cxt.restore_state(steal(snapshot));
    throw;
  }

  /* An exit inside the subshell ends only the subshell and supplies its status.
     A break or a continue is scoped to a loop inside the subshell, so it does
     not escape into the parent's loop and is consumed here. A return stays
     pending and propagates after the state is restored. */
  if (cxt.has_pending_control_flow()) {
    const control_flow::Kind kind = cxt.pending_control_flow().kind;
    if (kind == control_flow::Kind::Exit) {
      ret = cxt.pending_control_flow().value;
      cxt.clear_control_flow();
    } else if (kind == control_flow::Kind::Break ||
               kind == control_flow::Kind::Continue)
    {
      cxt.clear_control_flow();
    }
  }

  /* The subshell ends here, so its own EXIT action runs now, in the subshell's
     state and before the parent's traps return. */
  cxt.run_subshell_exit_trap();
  cxt.leave_subshell();
  cxt.restore_state(steal(snapshot));
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn Subshell::analyze(AnalysisContext &actx,
                          bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  /* A subshell runs in a forked child, so an assignment in its body never
     changes a parent variable. The outer constants are saved and restored
     around the body, and the body itself starts from an empty table so it does
     not carry a propagation across the subshell boundary. */
  let saved_constants = actx.constant_variables.clone();
  actx.constant_variables.clear();
  m_body->analyze(actx, is_unconditional);
  actx.constant_variables = steal(saved_constants);
}

FunctionDefinition::FunctionDefinition(SourceLocation location, StringView name,
                                       const Expression *body)
    : CompoundCommand(location), m_name(name), m_body(body)
{}

/* The body lives in the persistent function arena, owned by the function table
   rather than this node, so it is not deleted here. */
FunctionDefinition::~FunctionDefinition() = default;

pure fn FunctionDefinition::name() const wontthrow -> const String &
{
  return m_name;
}

pure fn FunctionDefinition::body() const wontthrow -> const Expression *
{
  return m_body;
}

cold fn FunctionDefinition::to_string() const throws -> String
{
  let result = String{"FunctionDefinition \""};
  result += StringView{m_name};
  result += "\"";
  return result;
}

cold fn FunctionDefinition::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_body != nullptr);

  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

fn FunctionDefinition::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_body != nullptr);

  /* The recorded definition is the bare name, a "name () " line, then the
     body's source span, the shape bash prints from declare -f. A consumer
     such as ble.sh clones a function by replacing the leading name in this
     text and re-evaling it, and greps the "name ()" line, so both properties
     matter. The source string dies with its frame while the function lives
     on, so the store keeps its own copy. An unrecorded body span stores empty
     text and declare -f prints nothing for the name. */
  let definition_text = String{cxt.scratch_allocator()};
  if (const String *source = cxt.current_source();
      source != nullptr &&
      m_body->source_end_position() > m_body->source_location().position &&
      m_body->source_end_position() <= source->count())
  {
    definition_text.append(m_name.view());
    definition_text.append(StringView{" () \n"});
    definition_text.append(source->view().substring_of_length(
        m_body->source_location().position,
        m_body->source_end_position() - m_body->source_location().position));
  }
  LOG(verbosity::Info, "registering the function '%s'%s", m_name.c_str(),
      definition_text.is_empty() ? " without recorded definition text" : "");
  cxt.register_function(m_name, m_body, definition_text.view(),
                        m_body->source_location().position, source_location());
  cxt.set_last_exit_status(0);
  return 0;
}

cold fn FunctionDefinition::analyze(AnalysisContext &actx,
                                    bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  unused(is_unconditional);
  actx.defined_functions.add(m_name);

  /* The body runs later when the function is called, not where it is defined,
     so a constant recorded before the definition does not prove a value inside
     the body, and an assignment in the body does not reach the straight-line
     block around the definition. The outer constants are saved and restored
     around the body, and the body starts from an empty table. */
  let saved_constants = actx.constant_variables.clone();
  actx.constant_variables.clear();
  let saved_locals = steal(actx.function_local_names);
  actx.function_local_names = HashSet{heap_allocator()};
  actx.function_scope_depth++;
  m_body->analyze(actx, false);
  actx.function_scope_depth--;
  actx.function_local_names = steal(saved_locals);
  actx.constant_variables = steal(saved_constants);
}

cold fn FunctionDefinition::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  actx.defined_functions.add(m_name);
}

RedirectedCommand::RedirectedCommand(SourceLocation location,
                                     const Command *child,
                                     ArrayList<Redirection> &&redirections)
    : Command(location), m_child(child)
{
  for (const Redirection &redir : redirections)
    m_redirections.push(redir);
}

/* The child and the redirection target tokens live in the arena, torn down once
   on reset. */
RedirectedCommand::~RedirectedCommand() = default;

cold fn RedirectedCommand::to_string() const throws -> String
{
  return "RedirectedCommand";
}

cold fn RedirectedCommand::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_child != nullptr);

  let const pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_child->to_ast_string(layer + 1);
}

cold fn RedirectedCommand::analyze(AnalysisContext &actx,
                                   bool is_unconditional) const throws -> void
{
  ASSERT(m_child != nullptr);

  m_child->analyze(actx, is_unconditional);
}

cold fn RedirectedCommand::register_defined_functions(
    AnalysisContext &actx) const throws -> void
{
  ASSERT(m_child != nullptr);

  /* The redirected command runs in the current shell, so a function defined in
     it is registered before the ordered walk. */
  m_child->register_defined_functions(actx);
}

fn RedirectedCommand::append_to(usize d, String &f, bool duplicate) throws
    -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

fn RedirectedCommand::redirect_to(usize d, String &f, bool duplicate) throws
    -> void
{
  unused(d);
  unused(f);
  unused(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

fn RedirectedCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_child != nullptr);

  LOG(verbosity::Debug, "applying %zu redirections around the compound command",
      m_redirections.count());

  /* The redirection targets expand here, so a runtime warning from done < $f
     carets this redirection rather than the statement before it. */
  cxt.set_current_location(source_location());

  /* A <(...) or >(...) in a redirection target, as in done < <(cmd), opens a
     pipe and forks a child or leaves a temp file during the expansion below.
     The mark is taken before it so this command reaps only the substitution its
     own redirection opens, reaped and its temp file deleted once the redirected
     command returns. Registered first so it runs last, after the descriptor
     backups restore. */
  let const substitution_mark = cxt.mark_process_substitutions();
  defer { cxt.cleanup_process_substitutions(substitution_mark); };

  /* The child runs around saved descriptor backups that restore afterward, so
     it forks rather than replacing the shell. */
  cxt.set_terminal_exec_allowed(false);

  /* The child runs in the shell process, so each redirection points one of the
     shell's own descriptors at the target and the saved backups put them back.
     The backups restore in reverse on every exit path, a normal return, a
     thrown diagnostic, or a pending break, continue, return, or exit that
     propagates through the child. */
  ArrayList<os::saved_descriptor> saved_descriptors{cxt.scratch_allocator()};
  defer
  {
    /* Any buffered shell output belongs on the redirected target, so it is
       flushed before the descriptors go back. */
    shit::flush();
    for (usize i = saved_descriptors.count(); i > 0; i--)
      os::restore_descriptor(saved_descriptors[i - 1]);
  };

  /* Stale buffered output from before the redirection belongs on the original
     descriptor, so it is flushed before the descriptors move. */
  shit::flush();

  for (const Redirection &redir : m_redirections) {
    /* A heredoc body becomes the standard input through an anonymous temp file,
       expanded when the delimiter was unquoted. */
    if (redir.kind == Redirection::Kind::Heredoc ||
        redir.kind == Redirection::Kind::HereString)
    {
      let body = String{};
      if (redir.kind == Redirection::Kind::Heredoc) {
        ASSERT(redir.heredoc_body != nullptr);
        body = redir.heredoc_body->clone();
        if (redir.heredoc_expand) body = cxt.expand_heredoc_body(body);
      } else {
        ASSERT(redir.target != nullptr);
        body = cxt.expand_word_for_assignment(
            static_cast<const tokens::WordToken *>(redir.target)->word());
        body += "\n";
      }

      let opened = os::write_to_temp_file(body);
      if (!opened)
        throw Error{"Could not stage the heredoc body: " +
                    os::last_system_error_message()};

      const os::descriptor temp_fd = opened.take();
      saved_descriptors.push(
          os::save_and_replace_descriptor(redir.fd, temp_fd));
      os::close_fd(temp_fd);
      continue;
    }

    /* A duplication like 2>&1 points one shell descriptor at another, with no
       file opened. The descriptor may come from a dynamic word such as >&$5. */
    if (redir.kind == Redirection::Kind::DuplicateOutput ||
        redir.kind == Redirection::Kind::DuplicateInput)
    {
      const i32 from_fd = resolve_duplication_fd(redir, cxt);

      /* The close form backs the descriptor up, then closes it. The backup is
         saved so restore reopens it when the compound command finishes. */
      if (from_fd == Redirection::DUP_FD_CLOSE) {
        saved_descriptors.push(os::save_and_replace_descriptor(
            redir.fd, os::descriptor_for_shell_fd(redir.fd)));
        os::close_fd(os::descriptor_for_shell_fd(redir.fd));
        continue;
      }

      const os::descriptor source = os::descriptor_for_shell_fd(from_fd);
      const os::saved_descriptor saved =
          os::save_and_replace_descriptor(redir.fd, source);
      saved_descriptors.push(saved);
      /* A duplication onto a closed or invalid descriptor, as in { echo hi; }
         >&7 with fd 7 closed, fails the dup2. The compound command fails with a
         located error rather than writing to the original descriptor, matching
         dash. */
      if (!saved.dup2_ok) {
        const SourceLocation location = redir.target != nullptr
                                            ? redir.target->source_location()
                                            : source_location();
        throw ErrorWithLocation{location, utils::int_to_text(from_fd) +
                                              ": Bad file descriptor"};
      }
      continue;
    }

    ASSERT(redir.target != nullptr);

    ArrayList<const Token *> target_tokens{cxt.scratch_allocator()};
    target_tokens.push(redir.target);
    const ArrayList<String> target = cxt.process_args(target_tokens);
    if (target.count() != 1) {
      throw ErrorWithLocation{redir.target->source_location(),
                              "Redirection target is not a single file"};
    }

    let mode = redirection_open_mode(redir.kind, cxt.no_clobber());

    const String &target_path = target[0];
    let opened = os::open_file_descriptor(target_path, mode);
    if (!opened) {
      throw ErrorWithLocation{redir.target->source_location(),
                              "Could not open '" + target_path +
                                  "': " + os::last_system_error_message()};
    }
    const os::descriptor file_fd = opened.take();

    /* The dup leaves a live copy on the shell descriptor, so the opened file
       descriptor itself is closed at once to avoid leaking it. */
    saved_descriptors.push(os::save_and_replace_descriptor(redir.fd, file_fd));
    os::close_fd(file_fd);
  }

  const i64 result = m_child->evaluate(cxt);
  return result;
}

UnaryExpression::UnaryExpression(SourceLocation location, const Expression *rhs)
    : Expression(location), m_rhs(rhs)
{}

/* The operand lives in the arena, torn down once on reset. */
UnaryExpression::~UnaryExpression() = default;

cold fn UnaryExpression::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_rhs != nullptr);

  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[Unary " + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_rhs->to_ast_string(layer + 1);
  return s;
}

BinaryExpression::BinaryExpression(SourceLocation location,
                                   const Expression *lhs, const Expression *rhs)
    : Expression(location), m_lhs(lhs), m_rhs(rhs)
{}

/* The operands live in the arena, torn down once on reset. */
BinaryExpression::~BinaryExpression() = default;

cold fn BinaryExpression::to_ast_string(usize layer) const throws -> String
{
  ASSERT(m_lhs != nullptr);
  ASSERT(m_rhs != nullptr);

  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[Binary " + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_lhs->to_ast_string(layer + 1) + "\n";
  s += pad + EXPRESSION_AST_INDENT + m_rhs->to_ast_string(layer + 1);

  return s;
}

ConstantNumber::ConstantNumber(SourceLocation location, i64 value)
    : Expression(location), m_value(value)
{}

ConstantNumber::~ConstantNumber() = default;

fn ConstantNumber::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  unused(cxt);
  return m_value;
}

cold fn ConstantNumber::to_ast_string(usize layer) const throws -> String
{
  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[Number " + to_string() + "]";
  return s;
}

cold fn ConstantNumber::to_string() const throws -> String
{
  return utils::int_to_text(m_value);
}

ConstantString::ConstantString(SourceLocation location, StringView value)
    : Expression(location), m_value(value)
{}

ConstantString::~ConstantString() = default;

fn ConstantString::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  unused(cxt);
  unreachable();
}

cold fn ConstantString::to_ast_string(usize layer) const throws -> String
{
  let s = String{};
  let const pad = indent_for_layer(layer);

  s += pad + "[String \"" + to_string() + "\"]";
  return s;
}

cold fn ConstantString::to_string() const throws -> String { return m_value; }

#define UNARY_EXPRESSION_DECLS(e, expr)                                        \
  e::e(SourceLocation location, const Expression *rhs)                         \
      : UnaryExpression(location, rhs)                                         \
  {}                                                                           \
  String e::to_string() const throws { return #expr; }                         \
  i64 e::evaluate_impl(EvalContext &cxt) const throws                          \
  {                                                                            \
    return expr m_rhs->evaluate(cxt);                                          \
  }

UNARY_EXPRESSION_DECLS(Negate, -);
UNARY_EXPRESSION_DECLS(Unnegate, +);
UNARY_EXPRESSION_DECLS(LogicalNot, !);
UNARY_EXPRESSION_DECLS(BinaryComplement, ~);

BinaryDummyExpression::BinaryDummyExpression(SourceLocation location,
                                             const Expression *lhs,
                                             const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

cold fn BinaryDummyExpression::to_string() const throws -> String
{
  return "BinaryDummyExpression";
}

fn BinaryDummyExpression::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  unused(cxt);
  return 0;
}

Divide::Divide(SourceLocation location, const Expression *lhs,
               const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

cold fn Divide::to_string() const throws -> String { return "/"; }

/* Custom evaluation, since we can't divide by zero. */
fn Divide::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_lhs != nullptr);
  ASSERT(m_rhs != nullptr);

  let const denom = m_rhs->evaluate(cxt);
  if (denom == 0)
    throw ErrorWithLocation{m_rhs->source_location(), "Division by 0"};

  return m_lhs->evaluate(cxt) / denom;
}

#define BINARY_EXPRESSION_DECLS(e, expr)                                       \
  e::e(SourceLocation location, const Expression *lhs, const Expression *rhs)  \
      : BinaryExpression(location, lhs, rhs)                                   \
  {}                                                                           \
  String e::to_string() const throws { return #expr; }                         \
  i64 e::evaluate_impl(EvalContext &cxt) const throws                          \
  {                                                                            \
    return m_lhs->evaluate(cxt) expr m_rhs->evaluate(cxt);                     \
  }

BINARY_EXPRESSION_DECLS(Add, +);
BINARY_EXPRESSION_DECLS(Subtract, -);
BINARY_EXPRESSION_DECLS(Multiply, *);
BINARY_EXPRESSION_DECLS(Module, %);
BINARY_EXPRESSION_DECLS(BinaryAnd, &);
BINARY_EXPRESSION_DECLS(LogicalAnd, &&);
BINARY_EXPRESSION_DECLS(GreaterThan, >);
BINARY_EXPRESSION_DECLS(GreaterOrEqual, >=);
BINARY_EXPRESSION_DECLS(RightShift, >>);
BINARY_EXPRESSION_DECLS(LessThan, <);
BINARY_EXPRESSION_DECLS(LessOrEqual, <=);
BINARY_EXPRESSION_DECLS(LeftShift, <<);
BINARY_EXPRESSION_DECLS(BinaryOr, |);
BINARY_EXPRESSION_DECLS(LogicalOr, ||);
BINARY_EXPRESSION_DECLS(Xor, ^);
BINARY_EXPRESSION_DECLS(Equal, ==);
BINARY_EXPRESSION_DECLS(NotEqual, !=);

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

/* True when every segment of the word is plain text with no expansion, so the
   runtime value equals the source text. Read by the constant-test and the
   glob-pattern lints. */
cold fn word_is_fully_literal(const Word &word) wontthrow -> bool
{
  for (const WordSegment &segment : word.segments)
    if (segment.kind != WordSegment::Kind::LiteralText &&
        segment.kind != WordSegment::Kind::UnquotedText &&
        segment.kind != WordSegment::Kind::DoubleQuotedText)
      return false;
  return true;
}

/* True when an optional minus is followed by one or more digits and nothing
   else, the operand shape the numeric test operators accept. */
cold pure fn view_is_integer_literal(StringView view) wontthrow -> bool
{
  usize i = view.length >= 1 && view[0] == '-' ? 1 : 0;
  if (i >= view.length) return false;
  for (; i < view.length; i++)
    if (view[i] < '0' || view[i] > '9') return false;
  return true;
}

/* True when the view carries the needle anywhere, the substring probe
   StringView itself does not offer. Both sides are tiny lint inputs, so the
   plain scan costs nothing measurable. */
cold pure fn view_contains(StringView view, StringView needle) wontthrow -> bool
{
  if (needle.length == 0 || needle.length > view.length) return false;
  for (usize i = 0; i + needle.length <= view.length; i++)
    if (view.substring(i).starts_with(needle)) return true;
  return false;
}

/* Whether any operand names standard input explicitly, the - and /dev/stdin
   spellings, the exemption the never-reads-stdin warnings share. */
cold fn args_have_stdin_operand(const ArrayList<const Token *> &args) throws
    -> bool
{
  for (usize i = 1; i < args.count(); i++) {
    let const raw = args[i]->raw_string();
    if (raw.view() == "-" || raw.view() == "/dev/stdin") return true;
  }
  return false;
}

/* True when one of the leading short-option clusters carries the letter, the
   way -rf carries r and f. A long option or a plain operand is not a
   cluster. */
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
      return true;
  }
  return false;
}

/* The commands that never read stdin, so a pipe or an input redirect into one
   of them silently discards the upstream data, shellcheck SC2216 for the pipe
   and SC2217 for the redirect. The value is unused, the membership is the
   answer. */
constexpr StaticStringMap<bool>::entry NON_STDIN_READER_ENTRIES[] = {
    {PackedStringKey::from_literal("rm"),       true},
    {PackedStringKey::from_literal("echo"),     true},
    {PackedStringKey::from_literal("printf"),   true},
    {PackedStringKey::from_literal("true"),     true},
    {PackedStringKey::from_literal("false"),    true},
    {PackedStringKey::from_literal("mkdir"),    true},
    {PackedStringKey::from_literal("rmdir"),    true},
    {PackedStringKey::from_literal("touch"),    true},
    {PackedStringKey::from_literal("chmod"),    true},
    {PackedStringKey::from_literal("chown"),    true},
    {PackedStringKey::from_literal("cp"),       true},
    {PackedStringKey::from_literal("mv"),       true},
    {PackedStringKey::from_literal("ln"),       true},
    {PackedStringKey::from_literal("kill"),     true},
    {PackedStringKey::from_literal("basename"), true},
    {PackedStringKey::from_literal("dirname"),  true},
    {PackedStringKey::from_literal("sleep"),    true},
    {PackedStringKey::from_literal("unlink"),   true},
};
constexpr StaticStringMap<bool> NON_STDIN_READERS{
    NON_STDIN_READER_ENTRIES,
    sizeof(NON_STDIN_READER_ENTRIES) / sizeof(NON_STDIN_READER_ENTRIES[0])};

/* The top-level system directories rm -r must never aim at, the SC2114
   table. */
constexpr StaticStringMap<bool>::entry SYSTEM_DIRECTORY_ENTRIES[] = {
    {PackedStringKey::from_literal("/"),     true},
    {PackedStringKey::from_literal("/bin"),  true},
    {PackedStringKey::from_literal("/boot"), true},
    {PackedStringKey::from_literal("/dev"),  true},
    {PackedStringKey::from_literal("/etc"),  true},
    {PackedStringKey::from_literal("/home"), true},
    {PackedStringKey::from_literal("/lib"),  true},
    {PackedStringKey::from_literal("/proc"), true},
    {PackedStringKey::from_literal("/root"), true},
    {PackedStringKey::from_literal("/sbin"), true},
    {PackedStringKey::from_literal("/sys"),  true},
    {PackedStringKey::from_literal("/usr"),  true},
    {PackedStringKey::from_literal("/var"),  true},
};
constexpr StaticStringMap<bool> SYSTEM_DIRECTORIES{
    SYSTEM_DIRECTORY_ENTRIES,
    sizeof(SYSTEM_DIRECTORY_ENTRIES) / sizeof(SYSTEM_DIRECTORY_ENTRIES[0])};

cold fn SimpleCommand::analyze(AnalysisContext &actx,
                               bool is_unconditional) const throws -> void
{
  /* A missing command is only warned about, so the conditional context no
     longer changes the prepass outcome for this node. */
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
    LOG(verbosity::Debug,
        "'%s' may define commands at run time, later resolution failures "
        "degrade to warnings",
        command_literal.c_str());
    actx.saw_runtime_definer = true;
  }

  /* A glob pattern with an unterminated bracket expression can never compile
     into a matcher, so the expansion would throw at run time. The malformed
     pattern is visible from the word bytes alone, so the prepass rejects it
     here at the located word. */
  for (const Token *t : m_args) {
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
      for (const WordSegment &segment : word.segments) {
        if (segment.kind == WordSegment::Kind::VariableReference &&
            segment.is_split_eligible())
        {
          actx.warn(
              m_args[i]->source_location(),
              "Unquoted variable in a test, quote it to avoid an empty or "
              "split argument");
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
              "A read without -r mangles a backslash in the input, add -r "
              "to "
              "read the line literally");
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
        actx.warn(m_args[1]->source_location(),
                  "An echo " + view +
                      " relies on a bash builtin, the POSIX echo prints the "
                      "flag as text, use printf instead under a sh shebang");
    }
    if (command_literal == "declare" || command_literal == "typeset")
      actx.warn(m_args[0]->source_location(),
                StringView{"The "} + command_literal.view() +
                    " builtin is not in POSIX, assign the variable plainly "
                    "under a sh shebang, or switch the shebang to bash");
    if (command_literal == "source")
      actx.warn(m_args[0]->source_location(),
                "The name source is the bash spelling, the POSIX dot command "
                "is '.', use '.' under a sh shebang");
    if (command_literal == "local")
      actx.warn(m_args[0]->source_location(),
                "The local builtin is not in POSIX sh, the value stays "
                "global, rework the function or switch the shebang to bash");
    if (command_literal == "printf" && m_args.count() >= 2 &&
        m_args[1]->kind() == Token::Kind::Word &&
        static_cast<const tokens::WordToken *>(m_args[1])
                ->word()
                .to_literal_string()
                .view() == "-v")
      actx.warn(m_args[1]->source_location(),
                "The printf -v form is a bash extension, the POSIX printf "
                "has no -v, capture the output with a command substitution "
                "under a sh shebang");
    /* mapfile and its readarray alias are bash array builtins with no POSIX
       counterpart. This is shellcheck SC3030, read the input with a while
       read loop under a sh shebang. */
    if (command_literal == "mapfile" || command_literal == "readarray")
      actx.warn(m_args[0]->source_location(),
                command_literal.view() +
                    " is a bash array builtin absent from POSIX sh, read the "
                    "input with a while read loop or switch the shebang to "
                    "bash");
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
      actx.warn(m_args[0]->source_location(),
                "The egrep command is deprecated, use grep -E for the "
                "extended regular "
                "expression match");
    else if (command_literal == "fgrep")
      actx.warn(m_args[0]->source_location(),
                "The fgrep command is deprecated, use grep -F for the fixed "
                "string match");
    else if (command_literal == "expr")
      actx.warn(m_args[0]->source_location(),
                "An expr forks for arithmetic the shell does natively, use "
                "$((...)) for the calculation");
    else if (command_literal == "let")
      actx.warn(m_args[0]->source_location(),
                "A let runs arithmetic as a command, use the ((...)) "
                "compound so "
                "the operands need no quoting");
    else if (command_literal == "local" && actx.function_scope_depth == 0)
      actx.warn(m_args[0]->source_location(),
                "A local outside a function has no scope to bind, declare "
                "the "
                "variable plainly or move it into a function");
  }

  if (command_literal == "echo" && !command_is_shadowed &&
      m_args.count() == 2 && m_args[1]->kind() == Token::Kind::Word)
  {
    let const &word = static_cast<const tokens::WordToken *>(m_args[1])->word();
    if (word.segments.count() == 1 &&
        word.segments[0].kind == WordSegment::Kind::CommandSubstitution)
      actx.warn(m_args[0]->source_location(),
                "An echo of a command substitution prints what the command "
                "already prints, run the command on its own instead");
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
    for (const WordSegment &segment : action.segments)
      if (segment.is_in_double_quotes &&
          (segment.kind == WordSegment::Kind::VariableReference ||
           segment.kind == WordSegment::Kind::CommandSubstitution))
      {
        action_expands_now = true;
        break;
      }
    if (action_expands_now)
      actx.warn(m_args[1]->source_location(),
                "The double-quoted trap action expands now, when the trap "
                "is "
                "set, not when it fires, single-quote it so it expands as the "
                "signal arrives");
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
      for (const WordSegment &segment : format.segments) {
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
                  "inject "
                  "format directives, use printf '%s' to print it");
    }
  }

  /* which is not in POSIX and its output and exit status vary across systems,
     so command -v is the portable lookup. This is shellcheck SC2230. A function
     or alias named which is the user's own, so the lint is off for it. */
  if (command_literal == "which" && !command_is_shadowed) {
    actx.warn(m_args[0]->source_location(),
              "The which command is non-standard, use command -v for a "
              "portable lookup");
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
      for (const WordSegment &segment : value.segments)
        if (segment.kind == WordSegment::Kind::CommandSubstitution) {
          value_has_substitution = true;
          break;
        }
      if (!value_has_substitution) continue;
      actx.warn(m_args[i]->source_location(),
                "Declaring and assigning from a command substitution in one "
                "command masks the command's exit status, split the "
                "declaration and the assignment so a failure is seen");
      break;
    }

  for (usize i = 1; i < m_args.count(); i++) {
    if (m_args[i]->kind() != Token::Kind::Word) continue;
    let const &word = static_cast<const tokens::WordToken *>(m_args[i])->word();
    bool word_has_unquoted_command_substitution = false;
    for (const WordSegment &segment : word.segments) {
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
              "An unquoted command substitution splits its output, quote "
              "it to keep one argument");
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
                      "/\" deletes '/' when the variable is empty, write ${" +
                      word.segments[0].text.view() +
                      ":?} so an empty value aborts the command instead");
      }
      if (word_is_fully_literal(word)) {
        let const literal = word.to_literal_string();
        if (SYSTEM_DIRECTORIES.find(literal.view()).has_value())
          actx.warn(m_args[i]->source_location(),
                    "A rm -r aimed at the system directory '" + literal.view() +
                        "', double-check the path before running this");
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
                  "The unquoted grep pattern can glob against the local "
                  "files "
                  "before grep sees it, quote the pattern");
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
        let const parsed = utils::parse_decimal_integer(view);
        is_in_range = !parsed.is_error() && parsed.value() <= 255;
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
    for (const WordSegment &segment : word.segments) {
      if (segment.kind != WordSegment::Kind::VariableReference ||
          segment.text.view() != "@")
        continue;
      if (word.segments.count() == 1 && !segment.is_in_double_quotes) {
        actx.warn(m_args[i]->source_location(),
                  "An unquoted $@ word-splits and globs each argument, "
                  "quote "
                  "it as \"$@\" to pass the arguments through unchanged");
      } else if (word.segments.count() > 1) {
        actx.warn(m_args[i]->source_location(),
                  "$@ inside a longer word concatenates the surrounding text "
                  "onto the first and last argument, use $* for one joined "
                  "string or a separate \"$@\" word");
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
    for (const WordSegment &segment : word.segments) {
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
                  "A useless echo inside the command substitution, the text "
                  "can be used directly without the subshell");
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
    for (const Redirection &redirection : m_redirections) {
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
                  "stderr stays on the terminal, put the file redirect first "
                  "as in '>file 2>&1'");
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
          actx.warn(redirection.target->source_location(),
                    "The command reads and truncates '" + read_target.view() +
                        "' at once, the truncation empties the input before "
                        "it is read, write to a temporary and move it over");
      }
    }

    if (!m_redirections.is_empty() && !command_is_shadowed &&
        NON_STDIN_READERS.find(command_literal.view()).has_value())
    {
      if (!args_have_stdin_operand(m_args))
        for (const Redirection &redirection : m_redirections)
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
        actx.warn(m_args[i]->source_location(),
                  "== is undefined in POSIX test, use = for string equality");
      }
      let const previous_is_bang = previous_literal.view() == "!";
      if (i >= 2 && !previous_is_bang && (view == "-a" || view == "-o")) {
        actx.warn(m_args[i]->source_location(),
                  "A test with -a or -o is obsolescent, join two tests with && "
                  "or "
                  "|| instead");
      } else if (view == "!" && i + 1 < m_args.count() &&
                 m_args[i + 1]->kind() == Token::Kind::Word)
      {
        let const next = static_cast<const tokens::WordToken *>(m_args[i + 1])
                             ->word()
                             .to_literal_string();
        if (next.view() == "-z") {
          actx.warn(m_args[i]->source_location(),
                    "A negated -z is just -n, test with -n instead");
        } else if (next.view() == "-n") {
          actx.warn(m_args[i]->source_location(),
                    "A negated -n is just -z, test with -z instead");
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
                          inverse.value() + ", drop the ! and use " +
                          inverse.value());
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
                  "A one-operand test is the nonempty-string test, write it "
                  "with -n to read clearer");
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
                        " test is constant, test a variable or drop the "
                        "check");
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
            actx.warn(m_args[i + 1]->source_location(),
                      "[ and test compare strings byte for byte and never "
                      "glob-match, use a case or the [[ ]] form for the "
                      "pattern");
        }
      }

      for (const WordSegment &segment : word.segments) {
        if (segment.kind != WordSegment::Kind::CommandSubstitution) continue;
        if (view_contains(segment.text.view(), StringView{"grep"}) &&
            !view_contains(segment.text.view(), StringView{"grep -c"}))
        {
          actx.warn(m_args[i]->source_location(),
                    "The test buffers the whole grep output only to check "
                    "it "
                    "is nonempty, run grep -q directly and test its exit "
                    "status");
          break;
        }
      }

      /* A test against $? checks the exit status indirectly, where the command
         can be tested directly with if or &&. This is shellcheck SC2181, which
         also avoids a $? clobbered by an intervening command. */
      if (word.segments.count() == 1 &&
          word.segments[0].kind == WordSegment::Kind::VariableReference &&
          word.segments[0].text.view() == "?")
        actx.warn(m_args[i]->source_location(),
                  "Testing $? checks the exit status indirectly, test the "
                  "command directly with if or && so an intervening command "
                  "cannot clobber the status");
    }
  }

  /* A prefix assignment does not affect the expansion on the same command, so a
     reference to one of its names reads the old value. */
  if (m_local_vars.count() > 0) {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      for (const WordSegment &segment : word.segments) {
        if (segment.kind != WordSegment::Kind::VariableReference) continue;
        const StringView referenced{segment.text.data(), segment.text.count()};
        bool names_a_prefix = false;
        for (const prefix_assignment &var : m_local_vars) {
          if (var.name.view() == referenced) {
            names_a_prefix = true;
            break;
          }
        }
        if (names_a_prefix) {
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

  if (name && !actx.silence_unresolved_commands &&
      !command_resolves(actx, *name) &&
      !actx.defined_functions.contains(
          StringView{name->data(), name->count()}) &&
      !actx.known_aliases.contains(StringView{name->data(), name->count()}))
  {
    let message = StringView{"Command '"} + StringView{*name} +
                  StringView{"' was not found"};
    /* A close function, alias, builtin, or PATH program is offered as a
       did-you-mean hint, so a typo points at the command it resembles. */
    let local_names = ArrayList<String>{};
    actx.defined_functions.for_each(
        [&](StringView n) throws { local_names.push(String{n}); });
    actx.known_aliases.for_each([&](StringView n)
                                    throws { local_names.push(String{n}); });
    if (Maybe<String> suggestion =
            utils::suggest_command(StringView{*name}, local_names))
    {
      message += ", did you mean '" + *suggestion + "'?";
    }
    /* Point at the command word, not at the whole command. With an assignment
       prefix the command location is the assignment, not the program name. A
       missing command is a fatal analysis error, so the file does not run with
       a command that cannot resolve. After a dot, source, or eval the command
       may be defined by code the prepass cannot see, so it is only a warning
       there.
       POSIX mode skips the analysis, so the file runs and the runtime
       resolution sets 127 per command the way dash does. */
    if (actx.saw_runtime_definer)
      actx.warn(m_args[0]->source_location(), message);
    else
      actx.fail(m_args[0]->source_location(), message);
  }

  /* A command may change a variable out of the prepass's static view, so a
     constant recorded for a later straight-line reference is no longer proven.
     A small set of builtins never writes a shell variable and never runs code
     the prepass cannot see, so a constant survives across them. Every other
     command, including a function call, an unset, an export, or a command
     substitution argument, forgets the whole table. */
  let const is_variable_neutral_builtin =
      command_literal == "echo" || command_literal == "printf" ||
      command_literal == "true" || command_literal == "false" ||
      command_literal == ":" || command_literal == "test" ||
      command_literal == "[" || command_literal == "pwd";

  bool clears_constants = !is_variable_neutral_builtin;
  if (!clears_constants) {
    /* A command substitution runs arbitrary code, so even a neutral builtin
       carrying one forgets the table to stay conservative. */
    for (const Token *t : m_args) {
      if (t->kind() != Token::Kind::Word) continue;
      let const &word = static_cast<const tokens::WordToken *>(t)->word();
      for (const WordSegment &segment : word.segments) {
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
    clears_constants = true;

  if (clears_constants) {
    LOG(verbosity::Debug,
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
  for (const Command *command : m_commands) {
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
          actx.warn(cat_args[0]->source_location(),
                    "A useless cat, give the file to the next command "
                    "directly instead of piping cat");
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
                  "An xargs splits the find output on whitespace and quotes, "
                  "pair find -print0 with xargs -0 or use find -exec");
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
                "the grep itself, use pgrep to match a process by name");

    /* ls piped into grep parses the formatted listing, which mangles a name
       with a space or a newline, shellcheck SC2010, use a glob or find. */
    if (stage_name->view() == "ls" && !stage_is_user && next_is_grep)
      actx.warn(next->args()[0]->source_location(),
                "Grepping the ls listing mangles a name with a space or a "
                "newline, match the names with a glob or with find instead");

    /* grep whose output only feeds wc -l counts matches with a second
       process, shellcheck SC2126, use grep -c. */
    if (stage_name->view() == "grep" && !stage_is_user &&
        next_name->view() == "wc" && !next_is_user &&
        next->args().count() == 2 &&
        next->args()[1]->raw_string().view() == "-l")
      actx.warn(stage->args()[0]->source_location(),
                "Counting grep output with wc -l runs an extra process, use "
                "grep -c to count the matching lines directly");
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
  for (const CompoundListCondition *node : m_nodes) {
    ASSERT(node != nullptr);
    node->register_defined_functions(actx);
  }

  for (const CompoundListCondition *node : m_nodes) {
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
  for (const CompoundListCondition *node : m_nodes) {
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

  /* The condition and both branches run in the current shell, so a function
     defined in any of them is registered before the ordered walk. */
  m_condition->register_defined_functions(actx);
  m_then->register_defined_functions(actx);
  if (m_otherwise != nullptr) m_otherwise->register_defined_functions(actx);
}

} /* namespace expressions */

} /* namespace shit */
