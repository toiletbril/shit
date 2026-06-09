#include "Expressions.hpp"

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Lexer.hpp"
#include "Optimizer.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <optional>
#include <utility>

namespace shit {

static fn indent_for_layer(usize layer) throws -> String
{
  let pad = String{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad;
}

Expression::Expression(SourceLocation location) : m_location(steal(location)) {}

pure fn Expression::source_location() const wontthrow -> SourceLocation
{
  return m_location;
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
  /* A trapped signal arrived since the last node, so its action runs here at the
     command boundary before the next node. The single flag keeps the common
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

  String name{};
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

  if (const bool *cached = actx.command_resolution_cache.find(name.view()))
    return *cached;

  const bool was_resolved =
      utils::search_program_path(name.view()).count() != 0;
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
               bool errors_are_warnings) throws -> bool
{
  ASSERT(root != nullptr);

  AnalysisContext actx{source};
  actx.errors_are_warnings = errors_are_warnings;

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

  String s{};
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

  /* A conditional or nested assignment may not run, and a runtime definer such
     as eval or dot may already have changed the name out of view, so neither
     proves the value. The append form NAME+=VALUE depends on the prior value,
     which the prepass does not track. Each of these forgets the name rather
     than record it. */
  if (!is_unconditional || actx.saw_runtime_definer ||
      m_assignment->is_append())
  {
    actx.constant_variables.erase(name.view());
    return;
  }

  let const literal = optimizer::literal_word_value(m_assignment->value_word());
  if (literal.has_value())
    actx.constant_variables.set(name.view(), literal->view());
  else
    /* The value is only known at run time, so a constant recorded for this name
       under an earlier assignment no longer holds and is forgotten. */
    actx.constant_variables.erase(name.view());
}

hot fn AssignCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_assignment != nullptr);

  /* Record where this assignment sits so a $LINENO in its value reports its
     line, since a script may read it as x=$LINENO. */
  cxt.set_current_location(source_location());

  /* The status defaults to 0, but a command substitution in the value sets it
     to the status of that substitution, which the assignment then reports. */
  cxt.set_last_exit_status(0);
  let value = cxt.expand_word_for_assignment(m_assignment->value_word());

  /* The append form NAME+=VALUE prepends the current value of NAME, treating an
     unset name as empty, so the store receives the concatenation. */
  if (m_assignment->is_append()) {
    String appended{cxt.get_variable_value(m_assignment->key()).value_or("")};
    appended += value;
    value = steal(appended);
  }

  /* The assignment goes through set_shell_variable first, so it still rejects a
     readonly name and refreshes the cached IFS. Under allexport it is then
     marked for the environment so a child inherits it, while a later lookup
     still finds the shell copy. */
  cxt.set_shell_variable(m_assignment->key(), value);
  if (cxt.export_all()) {
    let const &key = m_assignment->key();
    cxt.record_environment_change(key);
    os::set_environment_variable(key, value);
  }
  return cxt.last_exit_status();
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
    : Command(location)
{
  for (const Token *arg : args)
    m_args.push(arg);

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
  for (const Redirection &redir : redirections)
    m_redirections.push(redir);
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

  ArrayList<const Token *> target_tokens{heap_allocator()};
  target_tokens.push(redir.target);
  const ArrayList<String> fields = cxt.process_args(target_tokens);
  if (fields.count() != 1) {
    throw ErrorWithLocation{redir.target->source_location(),
                            "Duplication target is not a single descriptor"};
  }

  const String &field = fields[0];
  if (field == "-") return Redirection::DUP_FD_CLOSE;

  const let parsed = utils::parse_decimal_integer(field.view());
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
  for (const Redirection &redir : m_redirections) {
    if (redir.kind == Redirection::Kind::Heredoc) {
      ASSERT(redir.heredoc_body != nullptr);

      String body{*redir.heredoc_body};
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

    ArrayList<const Token *> target_tokens{heap_allocator()};
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

    /* The alias body replaces the first word, so the split words go in front of
       the remaining arguments. ArrayList has no in-place erase, so the new list
       is built and swapped in. */
    ArrayList<String> rebuilt{};
    String current{};
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
     or only assignments, such as a=1 b=2, so the redirections and the
     assignments still run below. */
  ASSERT(m_args.count() > 0 || !m_redirections.is_empty() ||
         m_local_vars.count() > 0);

  /* Record where this command sits so a $LINENO in its words reports its line.
   */
  cxt.set_current_location(source_location());

  if (cxt.should_echo()) {
    shit::print(utils::merge_tokens_to_string(m_args) + "\n");
    shit::flush();
  }

  let program_args = cxt.process_args(m_args);
  expand_command_aliases(cxt, program_args);

  /* A bare exec, the word exec with no further argument, applies its
     redirections to the shell's own descriptors permanently rather than around
     a single command. A function named exec shadows the builtin the same way a
     function shadows any command, so a shadowing function takes the ordinary
     path. The redirection loop below routes each entry to the permanent path
     instead of the temporary save and restore path when this is set. */
  const bool is_bare_exec =
      program_args.count() == 1 && program_args[0] == "exec" &&
      !(cxt.has_functions() && cxt.find_function(program_args[0]) != nullptr);

  /* Whether the command word resolves to a POSIX special builtin not shadowed by
     a function. It decides both that a redirection error exits the shell rather
     than failing the command, and that a prefix assignment persists, so it is
     computed once here and read on both paths. An empty command word, a bare
     redirection or assignment line, is not a special builtin. */
  const bool command_is_special_builtin =
      !program_args.is_empty() &&
      !(cxt.has_functions() &&
        cxt.find_function(program_args[0]) != nullptr) &&
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
  ArrayList<os::saved_descriptor> dup_saved_descriptors{heap_allocator()};
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
    if (redir.kind == Redirection::Kind::Heredoc) {
      ASSERT(redir.heredoc_body != nullptr);

      String body{*redir.heredoc_body};
      if (redir.heredoc_expand) {
        body = cxt.expand_heredoc_body(body);
      }

      let opened = os::write_to_temp_file(body);
      if (!opened) {
        redirection_open_failed = true;
        throw ErrorWithLocation{source_location(),
                                "Could not stage the heredoc body: " +
                                    os::last_system_error_message()};
      }

      /* A bare exec heredoc points the shell's standard input at the staged
         body for good and drops the temporary descriptor. */
      if (is_bare_exec) {
        shit::flush();
        const os::descriptor body_fd = opened.take();
        os::replace_descriptor(redir.fd, body_fd);
        if (body_fd != redir.fd) os::close_fd(body_fd);
        continue;
      }

      /* A heredoc on the standard input takes the in_fd slot. A numbered
         heredoc such as 3<<EOF targets descriptor N instead, which the three
         standard slots cannot express, so the body descriptor is staged onto
         the real shell fd N around the command and restored afterward, the same
         way a duplication onto an arbitrary descriptor is. */
      if (redir.fd == 0) {
        if (redirect_in_fd) os::close_fd(*redirect_in_fd);
        redirect_in_fd = opened.take();
        continue;
      }

      const os::descriptor body_fd = opened.take();
      /* The temp file already lands on fd N when mkstemp handed back that very
         number, since the standard descriptors took the lower slots. The
         generic save then dup2 would back up the body itself and leave it open
         on N after the command, so the collision is handled directly. The
         restore closes fd N, which fd N was free before mkstemp claimed it
         makes correct. */
      if (body_fd == redir.fd) {
        dup_saved_descriptors.push(
            os::saved_descriptor{.shell_fd = redir.fd, .was_open = false});
        continue;
      }
      dup_saved_descriptors.push(
          os::save_and_replace_descriptor(redir.fd, body_fd));
      os::close_fd(body_fd);
      continue;
    }

    /* A duplication like 2>&1 routes one descriptor to another without a file.
       The descriptor may come from a dynamic word such as >&$5, resolved here.
     */
    if (redir.kind == Redirection::Kind::DuplicateOutput ||
        redir.kind == Redirection::Kind::DuplicateInput)
    {
      const i32 from_fd = resolve_duplication_fd(redir, cxt);

      /* A bare exec applies a duplication to the shell's own descriptor for
         good, with no backup, so the copy or the close stays in effect for
         every later command. The flush keeps buffered output on the original
         descriptor before it moves. */
      if (is_bare_exec) {
        shit::flush();

        if (from_fd == Redirection::DUP_FD_CLOSE) {
          os::close_shell_fd(redir.fd);
          continue;
        }

        /* A duplication onto a closed or invalid descriptor, as in exec 6>&9
           with fd 9 closed, fails the dup2. The exec fails with a located error
           and the shell keeps the descriptor unchanged, matching dash. */
        if (!os::replace_descriptor(redir.fd,
                                    os::descriptor_for_shell_fd(from_fd)))
        {
          const SourceLocation location = redir.target != nullptr
                                              ? redir.target->source_location()
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

      /* A cross-route between the standard output and error, as in 2>&1, points
         the real shell descriptor at the target in source order so a later file
         redirect on the source does not change what the copy already captured.
         The shell's buffered output is flushed first, so it lands on the
         original descriptor rather than the duplication target. The arbitrary
         descriptor and the close form take the same in-order path. */
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
      /* A duplication onto a closed or invalid descriptor, as in >&5 with fd 5
         closed, fails the dup2. The command fails with a located error rather
         than writing to the original descriptor, matching dash. */
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

    ArrayList<const Token *> target_tokens{heap_allocator()};
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
      shit::flush();
      const bool was_replaced = os::replace_descriptor(redir.fd, file_fd);
      if (file_fd != redir.fd) os::close_fd(file_fd);
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
       place last. A redirect onto fd 1 or fd 2 mutates the shell's own standard
       output or error in place, so the buffered output is flushed first to land
       on the original descriptor. open returns the lowest free fd, which is at
       least three while fds 0, 1, and 2 hold the shell's stdio, so the file
       never lands on a standard fd itself. The higher fd, such as 3>file, takes
       the same in-order path the numbered heredoc and the compound redirect
       path use. */
    if (redir.fd == 1 || redir.fd == 2) shit::flush();
    if (file_fd == redir.fd) {
      /* open returned fd N itself, since fd N was the lowest free descriptor,
         so the file already sits on its target. The generic save then dup2
         would back up the file and the close would shut fd N, leaving the child
         without it, so the collision is recorded for restore without a close,
         the same way the numbered heredoc handles it. */
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
       fails the command rather than the shell, the way dash continues past it. A
       special builtin is the exception, since its redirection error exits a
       non-interactive shell. The descriptor and heredoc defers above still put
       the partially applied redirections back. */
    if (command_is_special_builtin) throw;
    const String *source = cxt.current_source();
    show_message(redirection_error.to_string(
        source != nullptr ? source->view() : StringView{}));
    /* dash reports a redirection failure with status 2, the value a script reads
       in $? after the failed command. */
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
        String appended{cxt.get_variable_value(name).value_or("")};
        appended += value;
        value = steal(appended);
      }
      cxt.set_shell_variable(name, value);
      if (cxt.export_all()) {
        cxt.record_environment_change(name);
        os::set_environment_variable(name, value.view());
      }
    }
    cxt.set_last_exit_status(0);
    return 0;
  }

  /* A prefix assignment before a special builtin persists after the command as a
     regular shell variable, the way POSIX keeps it. command_is_special_builtin,
     computed above the redirection loop, already excludes a function-shadowed
     name. The persisted form commits to the store below rather than the process
     environment, so it stays unexported, the way dash leaves it. */

  /* Per-command assignments apply to the environment for this command, a
     function call included, so a child inherits them and a function sees them.
     The previous values are restored on every exit path. */
  /* The environment value a prefix assignment shadowed, restored on exit. */
  struct saved_env_var
  {
    String name;
    Maybe<String> previous_value;
  };
  ArrayList<saved_env_var> saved_env{heap_allocator()};
  /* A prefix IFS=... drives the shell's own word splitting for this command,
     read by the read builtin and by every expansion in the command, which take
     it from the live separator cache rather than the environment. The effective
     separators are saved before the first such prefix and restored on exit, the
     way the prefix PATH save below reverts the resolver. */
  bool ifs_was_assigned = false;
  String saved_ifs_separators{heap_allocator()};
  /* The assignments apply left to right, each committed to the environment
     before the next is expanded, so a later value reads an earlier same-line
     one and a repeated name or a += accumulates. The previous environment
     values are saved for the restore on exit, which keeps the prefix temporary
     for this command. */
  for (const prefix_assignment &var : m_local_vars) {
    const StringView name = var.name.view();
    Maybe<String> previous = os::get_environment_variable(name);
    let expanded_value = cxt.expand_word_for_assignment(var.value);
    /* The append form prepends the current value of NAME, which a prefix reads
       from the shell store before the environment so a non-exported shell
       variable still contributes, treating an unset name as empty. */
    if (var.is_append) {
      String appended{cxt.get_variable_value(name).value_or("")};
      appended += expanded_value;
      expanded_value = steal(appended);
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
      }
      continue;
    }

    saved_env.push(saved_env_var{String{name}, steal(previous)});
    os::set_environment_variable(name, expanded_value.view());
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
  if (const Expression *function_body =
          cxt.has_functions() ? cxt.find_function(program_name) : nullptr;
      function_body != nullptr)
  {
    let saved_params = cxt.positional_params();
    ArrayList<String> call_params{};
    call_params.reserve(program_args.count() - 1);
    for (usize i = 1; i < program_args.count(); i++)
      call_params.push(String{heap_allocator(), program_args[i]});
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

    /* Open a local scope so a local builtin in the body shadows a variable and
       the old value returns when the call ends. */
    cxt.enter_function_scope();
    defer { cxt.leave_function_scope(); };

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

  if (cxt.shell_is_interactive())
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
  String s{};
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
    switch (n->kind()) {
    case CompoundListCondition::Kind::None:
      ret = n->evaluate(cxt);
      did_execute = true;
      break;

    case CompoundListCondition::Kind::Or:
      if (ret != 0) {
        ret = n->evaluate(cxt);
        did_execute = true;
      }
      break;

    case CompoundListCondition::Kind::And:
      if (ret == 0) {
        ret = n->evaluate(cxt);
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

  String s{};
  let const pad = indent_for_layer(layer);

  s += pad + "[" + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_cmd->to_ast_string(layer + 1);

  return s;
}

hot fn CompoundListCondition::evaluate_impl(EvalContext &cxt) const throws
    -> i64
{
  ASSERT(m_cmd != nullptr);

  /* A negated command reports the inverse of its status, so the shell's exit
     status differs from what the exec'd program returns. Clear the terminal
     exec permission here, since an exec in place would let the program's own
     status escape uninverted. */
  if (m_cmd->is_negated()) cxt.set_terminal_exec_allowed(false);

  let status = m_cmd->evaluate(cxt);

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
  ArrayList<os::process> children{};
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

      Maybe<os::descriptor> stage_in{};
      Maybe<os::descriptor> stage_out{};
      Maybe<os::Pipe> pipe{};

      if (!is_last) {
        pipe = os::make_pipe();
        if (!pipe) {
          throw ErrorWithLocation{stage->source_location(),
                                  "Could not open a pipe"};
        }
        stage_out = pipe->out;
      }
      if (!is_first) stage_in = last_stdin;

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
          stage_status = 1;
        }
        shit::flush();
        os::exit_process_immediately(stage_status);
      }

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
      } catch (...) {}
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

  i32 ret = 0;
  for (const os::process child : children) {
    const i32 status = os::wait_and_monitor_process(child);
    if (child == last_child) ret = status;
  }

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
     fork-per-stage path. */
  bool has_compound_stage = false;
  for (const Command *stage : m_commands) {
    if (!stage->is_simple_command()) {
      has_compound_stage = true;
      break;
    }
  }

  if (has_compound_stage) return evaluate_with_compound_stages(cxt);

  let ecs = ArrayList<ExecContext>{heap_allocator()};
  ecs.reserve(m_commands.count());

  for (const Command *stage : m_commands) {
    ASSERT(stage != nullptr);
    ASSERT(stage->is_simple_command());
    const SimpleCommand *e = static_cast<const SimpleCommand *>(stage);

    cxt.add_evaluated_expression();

    let stage_args = cxt.process_args(e->args());

    /* A stage that expands to no command word, such as a bare assignment or an
       unset variable, has no program to run. Report it instead of building an
       exec context from an empty argument list, which would read past the
       arguments. */
    if (stage_args.is_empty()) {
      throw ErrorWithLocation{e->source_location(),
                              "A pipeline stage expanded to no command to run"};
    }

    /* A stage whose command does not resolve is non-fatal. Report it to stderr
       and yield 127 for the pipeline rather than aborting the shell. The
       expansions of the earlier stages already ran, so the build is not
       retried. */
    Maybe<ExecContext> stage_ec;
    try {
      stage_ec =
          ExecContext::make_from(e->source_location(), steal(stage_args));
    } catch (const CommandNotFound &not_found) {
      report_command_not_found(cxt, not_found);
      cxt.set_last_exit_status(127);
      return 127;
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
    : CompoundCommand(location), m_otherwise(otherwise)
{
  for (const auto &branch : branches)
    m_branches.push(branch);
  /* The branch nodes live in the arena. Empty the moved-from source. */
  branches.clear();
}

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
enum class LoopDisposition
{
  /* No jump, or a continue aimed here, so run the next iteration. */
  RunNext,
  /* A break aimed here, or a jump aimed at an outer loop that is now left
     pending, so this loop stops. */
  StopLoop,
};

fn resolve_loop_control(EvalContext &cxt) throws -> LoopDisposition
{
  if (!cxt.has_pending_control_flow()) return LoopDisposition::RunNext;

  let &control = cxt.pending_control_flow();
  if (control.kind != control_flow::Kind::Break &&
      control.kind != control_flow::Kind::Continue)
  {
    /* A return or an exit is not a loop's to consume, so this loop stops and
       leaves it pending for the function or the shell. */
    return LoopDisposition::StopLoop;
  }

  /* A jump aimed at an outer loop decrements and stays pending, stopping this
     loop so the outer one consumes it. */
  if (control.value > 1) {
    control.value -= 1;
    return LoopDisposition::StopLoop;
  }

  /* The jump targets this loop. A break stops it, a continue runs the next
     iteration. Either way the request is consumed here. */
  let const is_break = control.kind == control_flow::Kind::Break;
  cxt.clear_control_flow();
  return is_break ? LoopDisposition::StopLoop : LoopDisposition::RunNext;
}

} /* namespace */

hot fn WhileLoop::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_body != nullptr);

  /* A loop body runs repeatedly in the shell process, so no command in it may
     replace the shell. */
  cxt.set_terminal_exec_allowed(false);

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
    if (resolve_loop_control(cxt) == LoopDisposition::StopLoop) break;
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

ForLoop::ForLoop(SourceLocation location, StringView variable_name,
                 ArrayList<const Token *> &&words, bool has_in_clause,
                 const Expression *body)
    : CompoundCommand(location), m_variable_name(variable_name),
      m_has_in_clause(has_in_clause), m_body(body)
{
  for (const Token *word : words)
    m_words.push(word);
  /* The word tokens live in the arena. Empty the moved-from source. */
  words.clear();
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
  /* Without an in clause the loop walks the positional parameters. */
  let const values =
      m_has_in_clause ? cxt.process_args(m_words) : cxt.positional_params();

  /* The body runs inside one more loop level, so a break or a continue clamps
     its level against the nesting that is actually live. */
  cxt.enter_loop();
  defer { cxt.leave_loop(); };

  i64 ret = 0;
  for (const String &value : values) {
    cxt.set_shell_variable(m_variable_name, value);
    ret = m_body->evaluate(cxt);
    if (resolve_loop_control(cxt) == LoopDisposition::StopLoop) break;
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn ForLoop::analyze(AnalysisContext &actx,
                         bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  unused(is_unconditional);

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
  for (case_item &item : items)
    m_items.push(steal(item));
  /* The item tokens and bodies live in the arena. Empty the moved-from source.
   */
  items.clear();
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

  /* A case word and its patterns expand with variables and tilde but no field
     splitting and no pathname globbing, so a pattern keeps its metacharacters
     for matching. */
  auto expand_no_glob = [&cxt](const Token *t) -> String {
    ASSERT(t != nullptr);
    if (t->kind() == Token::Kind::Word) {
      return cxt.expand_word_for_assignment(
          static_cast<const tokens::WordToken *>(t)->word());
    }
    return t->raw_string();
  };

  let const subject = expand_no_glob(m_word);

  for (const case_item &item : m_items) {
    ASSERT(item.body != nullptr);

    for (const Token *pattern_token : item.patterns) {
      /* A pattern keeps its glob metacharacters for matching, yet a quoted or
         escaped metacharacter in the pattern is a literal, so the expansion
         carries a parallel mask the matcher reads. A pattern token that is not
         a plain word, such as a reserved word arm, has no quoting structure and
         stays fully active. */
      let pattern_active = ArrayList<bool>{heap_allocator()};
      String pattern{};
      if (pattern_token->kind() == Token::Kind::Word) {
        pattern = cxt.expand_case_pattern_masked(
            static_cast<const tokens::WordToken *>(pattern_token)->word(),
            pattern_active);
      } else {
        pattern = pattern_token->raw_string();
        for (usize k = 0; k < pattern.count(); k++)
          pattern_active.push(true);
      }
      if (utils::glob_matches(pattern, subject, pattern_active, 0)) {
        let const ret = item.body->evaluate(cxt);
        cxt.set_last_exit_status(static_cast<i32>(ret));
        return ret;
      }
    }
  }

  cxt.set_last_exit_status(0);
  return 0;
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
              "this case has no default *) branch, a value no pattern matches "
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

  let snapshot = cxt.snapshot_state();
  cxt.enter_subshell();
  /* The inherited EXIT action belongs to the parent, so it does not fire at the
     subshell's end. An EXIT action the body sets survives this clear and fires
     below. */
  cxt.clear_inherited_exit_trap();
  i64 ret = 0;
  /* A diagnostic thrown by the body, such as a readonly violation or a missing
     command, must still restore the snapshot and leave the subshell, otherwise
     the parent stays stuck in subshell mode with the inner state leaked. */
  try {
    ret = m_body->evaluate(cxt);
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
  HashMap<String> saved_constants = actx.constant_variables;
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

  cxt.register_function(m_name, m_body);
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
  HashMap<String> saved_constants = actx.constant_variables;
  actx.constant_variables.clear();
  m_body->analyze(actx, false);
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

  /* The child runs around saved descriptor backups that restore afterward, so
     it forks rather than replacing the shell. */
  cxt.set_terminal_exec_allowed(false);

  /* The child runs in the shell process, so each redirection points one of the
     shell's own descriptors at the target and the saved backups put them back.
     The backups restore in reverse on every exit path, a normal return, a
     thrown diagnostic, or a pending break, continue, return, or exit that
     propagates through the child. */
  ArrayList<os::saved_descriptor> saved_descriptors{heap_allocator()};
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
    if (redir.kind == Redirection::Kind::Heredoc) {
      ASSERT(redir.heredoc_body != nullptr);

      String body{*redir.heredoc_body};
      if (redir.heredoc_expand) body = cxt.expand_heredoc_body(body);

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

    ArrayList<const Token *> target_tokens{heap_allocator()};
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
     since the split may be intended, and --bash-compatible skips the analysis
     entirely so a POSIX script that relies on it runs quietly. */
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
  if (command_literal == "read" && !command_is_shadowed) {
    bool has_raw_flag = false;
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const literal = static_cast<const tokens::WordToken *>(m_args[i])
                              ->word()
                              .to_literal_string();
      let const view = literal.view();
      if (view.length >= 2 && view[0] == '-' && view[1] != '-' &&
          view.find_character('r').has_value())
      {
        has_raw_flag = true;
        break;
      }
    }
    if (!has_raw_flag)
      actx.warn(source_location(),
                "read without -r mangles a backslash in the input, add -r to "
                "read the line literally");
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
                  "printf format comes from a variable, the data can inject "
                  "format directives, use printf '%s' to print it");
    }
  }

  /* which is not in POSIX and its output and exit status vary across systems,
     so command -v is the portable lookup. This is shellcheck SC2230. A function
     or alias named which is the user's own, so the lint is off for it. */
  if (command_literal == "which" && !command_is_shadowed) {
    actx.warn(m_args[0]->source_location(),
              "which is non-standard, use command -v for a portable lookup");
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
              "an unquoted command substitution splits its output, quote "
              "it to keep one argument");
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
        actx.warn(
            m_args[i]->source_location(),
            "test with -a or -o is obsolescent, join two tests with && or "
            "|| instead");
      } else if (view == "!" && i + 1 < m_args.count() &&
                 m_args[i + 1]->kind() == Token::Kind::Word)
      {
        let const next = static_cast<const tokens::WordToken *>(m_args[i + 1])
                             ->word()
                             .to_literal_string();
        if (next.view() == "-z") {
          actx.warn(m_args[i]->source_location(),
                    "a negated -z is just -n, test with -n instead");
        } else if (next.view() == "-n") {
          actx.warn(m_args[i]->source_location(),
                    "a negated -n is just -z, test with -z instead");
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
                      StringView{"a negated "} + op + " is just " +
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
                  "a one-operand test is the nonempty-string test, write it "
                  "with -n to read clearer");
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

  if (name && !command_resolves(actx, *name) &&
      !actx.defined_functions.contains(
          StringView{name->data(), name->count()}) &&
      !actx.known_aliases.contains(StringView{name->data(), name->count()}))
  {
    let message = StringView{"Command '"} + StringView{*name} +
                  StringView{"' was not found"};
    /* A close function, alias, builtin, or PATH program is offered as a
       did-you-mean hint, so a typo points at the command it resembles. */
    ArrayList<String> local_names{};
    actx.defined_functions.for_each(
        [&](StringView n) throws { local_names.push(String{n}); });
    actx.known_aliases.for_each(
        [&](StringView n) throws { local_names.push(String{n}); });
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
       --bash-compatible skips the analysis, so the file runs and the runtime
       resolution sets 127 per command the way bash does. */
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

  if (clears_constants) actx.constant_variables.clear();
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
                    "a useless cat, give the file to the next command directly "
                    "instead of piping cat");
        }
      }
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
