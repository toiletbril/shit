#include "Expressions.hpp"

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <optional>
#include <utility>

namespace shit {

Expression::Expression(SourceLocation location) : m_location(location) {}

pure fn Expression::source_location() const wontthrow -> SourceLocation
{
  return m_location;
}

cold fn Expression::to_ast_string(usize layer) const throws -> String
{
  String pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
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

cold fn Expression::register_defined_functions(AnalysisContext &actx) const
    throws -> void
{
  unused(actx);
}

fn Expression::is_simple_command() const wontthrow -> bool { return false; }

fn Expression::is_dummy() const wontthrow -> bool { return false; }

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
   run many times across the file scans PATH at most once. A name holding a slash
   is a path, so it is not cached, since the filesystem may differ per run. */
fn command_resolves(AnalysisContext &actx, const String &name) throws -> bool
{
  if (name.is_empty()) return false;
  if (search_builtin(name.view()).has_value()) return true;
  if (name.find_character('/').has_value())
    return utils::canonicalize_path(name.view()).has_value();

  if (const bool *cached = actx.command_resolution_cache.find(name.view()))
    return *cached;

  const bool was_resolved = utils::search_program_path(name.view()).count() != 0;
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

    /* A leading '^' negates the class and a ']' right after '[' or '[^' is a
       member, so the search for the closing ']' starts past both, matching the
       matcher's prescan. */
    if (scan < bytes.count() && bytes[scan].ch == '^') scan++;
    if (scan < bytes.count() && bytes[scan].ch == ']') scan++;

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

} /* namespace */

fn analyze_ast(const Expression *root, StringView source,
               const HashSet &known_functions, const HashSet &known_aliases,
               bool warn_missing_commands) throws -> bool
{
  ASSERT(root != nullptr);

  AnalysisContext actx{source};
  actx.warn_missing_commands = warn_missing_commands;

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
  String pad{};

  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;

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

fn Command::set_local_vars(HashMap<prefix_assignment> &&vars) throws -> void
{
  m_local_vars = steal(vars);
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
  String pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
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

hot fn AssignCommand::evaluate_impl(EvalContext &cxt) const throws -> i64
{
  ASSERT(m_assignment != nullptr);

  /* Record where this assignment sits so a $LINENO in its value reports its
     line, since a script may read it as x=$LINENO. */
  cxt.set_current_location(source_location());

  /* The status defaults to 0, but a command substitution in the value sets it
     to the status of that substitution, which the assignment then reports. */
  cxt.set_last_exit_status(0);
  String value = cxt.expand_word_for_assignment(m_assignment->value_word());

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
  String pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
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

/* Resolve the descriptor a duplication copies from. A literal descriptor and the
   close form were settled at parse time and pass straight through. A dynamic
   word such as $4 is expanded to one field here, where a dash means close and a
   numeric field names the descriptor. The result is a non-negative descriptor or
   Redirection::DUP_FD_CLOSE for the close form. A field that is neither throws a
   located error at the word. */
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
      } else if (redir.fd == 1 && from_fd == 2) {
        ec.dup_out_to_err = true;
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

    let mode = os::file_open_mode::Read;
    if (redir.kind == Redirection::Kind::TruncateOutput)
      mode = cxt.no_clobber() ? os::file_open_mode::TruncateNoClobber
                              : os::file_open_mode::Truncate;
    else if (redir.kind == Redirection::Kind::AppendOutput)
      mode = os::file_open_mode::Append;

    const String &target_path = target[0];
    let opened = os::open_file_descriptor(target_path, mode);
    if (!opened) {
      throw ErrorWithLocation{redir.target->source_location(),
                              "Could not open '" + target_path +
                                  "': " + os::last_system_error_message()};
    }
    const os::descriptor file_fd = opened.take();

    if (redir.fd == 0) {
      if (ec.in_fd) os::close_fd(*ec.in_fd);
      ec.in_fd = file_fd;
    } else if (redir.fd == 2) {
      if (ec.err_fd) os::close_fd(*ec.err_fd);
      ec.err_fd = file_fd;
    } else {
      if (ec.out_fd) os::close_fd(*ec.out_fd);
      ec.out_fd = file_fd;
    }
  }
}

fn SimpleCommand::is_simple_command() const wontthrow -> bool { return true; }

pure fn SimpleCommand::args() const wontthrow
    -> const ArrayList<const Token *> &
{
  return m_args;
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
  ArrayList<String> already_expanded{};

  while (!args.is_empty()) {
    let const &word = args[0];

    bool seen = false;
    for (const String &name : already_expanded)
      if (name == word) seen = true;
    if (seen) break;

    let const body = cxt.get_alias(word);
    if (!body.has_value()) break;
    already_expanded.push(String{
        heap_allocator(), StringView{word.c_str(), word.count()}
    });

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

  /* Record where this command sits so a $LINENO in its words reports its line. */
  cxt.set_current_location(source_location());

  if (cxt.should_echo()) {
    shit::print(utils::merge_tokens_to_string(m_args) + "\n");
    shit::flush();
  }

  let program_args = cxt.process_args(m_args);
  expand_command_aliases(cxt, program_args);

  /* Open the redirection targets. A redirection takes effect even when the
     command expands to None, so > file with no command still creates the
     file. The final descriptors pass to the exec context, which closes them,
     and the guard closes them on any path that does not hand them off. */
  Maybe<os::descriptor> redirect_in_fd;
  Maybe<os::descriptor> redirect_out_fd;
  Maybe<os::descriptor> redirect_err_fd;
  bool dup_err_to_out = false;
  bool dup_out_to_err = false;
  bool redirect_fds_handed_off = false;
  /* A duplication onto an arbitrary descriptor, such as >&5 or the close form
     >&-, points a real shell descriptor at the target around this command. The
     three descriptor slots above cannot express it, since they only carry the
     standard input, output, and error. The backups put the descriptors back
     once the command finishes, restored in reverse on every exit path. */
  ArrayList<os::saved_descriptor> dup_saved_descriptors{heap_allocator()};
  defer
  {
    for (usize i = dup_saved_descriptors.count(); i > 0; i--)
      os::restore_descriptor(dup_saved_descriptors[i - 1]);
  };
  defer
  {
    if (!redirect_fds_handed_off) {
      if (redirect_in_fd) os::close_fd(*redirect_in_fd);
      if (redirect_out_fd) os::close_fd(*redirect_out_fd);
      if (redirect_err_fd) os::close_fd(*redirect_err_fd);
    }
  };

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
      if (!opened)
        throw Error{"Could not stage the heredoc body: " +
                    os::last_system_error_message()};

      if (redirect_in_fd) os::close_fd(*redirect_in_fd);
      redirect_in_fd = opened.take();
      continue;
    }

    /* A duplication like 2>&1 routes one descriptor to another without a file.
       The descriptor may come from a dynamic word such as >&$5, resolved here.
     */
    if (redir.kind == Redirection::Kind::DuplicateOutput ||
        redir.kind == Redirection::Kind::DuplicateInput)
    {
      const i32 from_fd = resolve_duplication_fd(redir, cxt);

      /* A descriptor copied onto itself, as in 1>&1, changes nothing. The
         standard error and output cross-routing keeps the flag fast path, since
         the spawn applies it after the files are placed. */
      if (from_fd == redir.fd) {
        /* Self copy is a no-op. */
        continue;
      }
      if (redir.fd == 2 && from_fd == 1) {
        dup_err_to_out = true;
        continue;
      }
      if (redir.fd == 1 && from_fd == 2) {
        dup_out_to_err = true;
        continue;
      }

      /* An arbitrary descriptor or the close form points the real shell
         descriptor at the target around the command, restored afterward. The
         shell's buffered output is flushed first, so it lands on the original
         descriptor rather than the duplication target. */
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

    let mode = os::file_open_mode::Read;
    if (redir.kind == Redirection::Kind::TruncateOutput)
      mode = cxt.no_clobber() ? os::file_open_mode::TruncateNoClobber
                              : os::file_open_mode::Truncate;
    else if (redir.kind == Redirection::Kind::AppendOutput)
      mode = os::file_open_mode::Append;

    const String &target_path = target[0];
    let opened = os::open_file_descriptor(target_path, mode);
    if (!opened) {
      throw ErrorWithLocation{redir.target->source_location(),
                              "Could not open '" + target_path +
                                  "': " + os::last_system_error_message()};
    }
    const os::descriptor file_fd = opened.take();

    /* The last redirection of a descriptor wins, so a superseded open closes
       at once. */
    if (redir.fd == 0) {
      if (redirect_in_fd) os::close_fd(*redirect_in_fd);
      redirect_in_fd = file_fd;
    } else if (redir.fd == 2) {
      if (redirect_err_fd) os::close_fd(*redirect_err_fd);
      redirect_err_fd = file_fd;
    } else {
      if (redirect_out_fd) os::close_fd(*redirect_out_fd);
      redirect_out_fd = file_fd;
    }
  }

  /* An expansion may drop every word, for example an unset $x used as the whole
     command. There is None to run then, but the redirections above already
     took effect. A command-less line still carries its assignments, which
     persist in the current shell rather than apply only to a child. The
     location was set at the top of this function, so a $LINENO in any value on
     the line reports the same line, the way dash reports it. */
  if (program_args.is_empty()) {
    m_local_vars.for_each([&](StringView name, const prefix_assignment &var) {
      String value = cxt.expand_word_for_assignment(var.value);
      if (var.is_append) {
        String appended{cxt.get_variable_value(name).value_or("")};
        appended += value;
        value = steal(appended);
      }
      cxt.set_shell_variable(name, value);
      if (cxt.export_all()) os::set_environment_variable(name, value.view());
    });
    cxt.set_last_exit_status(0);
    return 0;
  }

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
  m_local_vars.for_each([&](StringView name, const prefix_assignment &var) {
    Maybe<String> previous = os::get_environment_variable(name);
    String expanded_value = cxt.expand_word_for_assignment(var.value);
    /* The append form prepends the current value of NAME, which a prefix reads
       from the shell store before the environment so a non-exported shell
       variable still contributes, treating an unset name as empty. */
    if (var.is_append) {
      String appended{cxt.get_variable_value(name).value_or("")};
      appended += expanded_value;
      expanded_value = steal(appended);
    }
    saved_env.push(saved_env_var{String{name}, steal(previous)});
    os::set_environment_variable(name, expanded_value.view());
  });
  defer
  {
    for (const auto &[name, old_value] : saved_env) {
      if (old_value)
        os::set_environment_variable(name.view(), old_value->view());
      else
        os::unset_environment_variable(name.view());
    }
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
    for (usize i = 1; i < program_args.count(); i++)
      call_params.push(String{
          heap_allocator(),
          StringView{program_args[i].c_str(), program_args[i].count()}
      });
    cxt.set_positional_params(steal(call_params));
    defer { cxt.set_positional_params(steal(saved_params)); };

    /* Bound the call nesting so a function that recurses without a base case
       errors with a caret here rather than exhausting the native stack. The
       defer decrements on every unwind path. */
    cxt.enter_function_call(source_location());
    defer { cxt.leave_function_call(); };

    /* Open a local scope so a local builtin in the body shadows a variable and
       the old value returns when the call ends. */
    cxt.enter_function_scope();
    defer { cxt.leave_function_scope(); };

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
                                         program_args)
            : ExecContext::make_from(source_location(), program_args);
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
    m_resolved_name = program_name;
  }

  /* The redirections override the inherited descriptors for this command. The
     exec context now owns the opened files and closes them. */
  if (redirect_in_fd) ec.in_fd = redirect_in_fd;
  if (redirect_out_fd) ec.out_fd = redirect_out_fd;
  if (redirect_err_fd) ec.err_fd = redirect_err_fd;
  ec.dup_err_to_out = dup_err_to_out;
  ec.dup_out_to_err = dup_out_to_err;
  redirect_fds_handed_off = true;

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
  String pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
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
  String pad{};

  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
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

  for (usize index = 0; index < m_nodes.count(); index++) {
    const CompoundListCondition *n = m_nodes[index];
    ASSERT(n != nullptr);

    switch (n->kind()) {
    case CompoundListCondition::Kind::None: ret = n->evaluate(cxt); break;

    case CompoundListCondition::Kind::Or:
      if (ret != 0) ret = n->evaluate(cxt);
      break;

    case CompoundListCondition::Kind::And:
      if (ret == 0) ret = n->evaluate(cxt);
      break;
    }

    /* A break, continue, return, or exit inside a node stops the rest of the
       list and unwinds to the boundary that consumes it. */
    if (cxt.has_pending_control_flow()) break;

    /* Under set -e the shell exits once a command at the end of its and-or
       chain fails, unless this list is the condition of an if, a while, or an
       and-or operand. */
    const bool ends_and_or_chain =
        index + 1 >= m_nodes.count() ||
        m_nodes[index + 1]->kind() == CompoundListCondition::Kind::None;
    if (cxt.error_exit() && !cxt.in_condition() && ends_and_or_chain &&
        ret != 0 && ret != NOTHING_WAS_EXECUTED)
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
  String pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;

  s += pad + "[" + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_cmd->to_ast_string(layer + 1);

  return s;
}

hot fn CompoundListCondition::evaluate_impl(EvalContext &cxt) const throws
    -> i64
{
  ASSERT(m_cmd != nullptr);
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
  let pad = String{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }

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
        /* The child evaluates the stage in a subshell, so a variable change such
           as a while-read does not escape, then exits with its status. A
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
      } catch (...) {
      }
    }
    throw;
  }

  if (is_async()) {
    if (last_child != SHIT_INVALID_PROCESS) {
      cxt.set_last_background_pid(os::process_id_of(last_child));
      const i32 id = cxt.register_job(last_child, "pipeline");
      if (cxt.shell_is_interactive())
        shit::print_error(
            "[" + utils::int_to_text(id) + "] " +
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
      stage_ec = ExecContext::make_from(e->source_location(), steal(stage_args));
    } catch (const CommandNotFound &not_found) {
      report_command_not_found(cxt, not_found);
      cxt.set_last_exit_status(127);
      return 127;
    }
    let ec = stage_ec.take();
    /* Apply this stage's own redirections, such as 2>&1, before the pipe wires
       its descriptors. The pipe only sets stdin and stdout, so a stderr
       redirection composes with it. */
    e->redirect_exec_context(ec, cxt);
    ecs.push(steal(ec));
  }

  return utils::execute_contexts_with_pipes(steal(ecs), cxt, is_async());
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

static fn indent_for_layer(usize layer) throws -> String
{
  let pad = String{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad;
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
}

cold fn IfClause::register_defined_functions(AnalysisContext &actx) const throws
    -> void
{
  /* Every branch body and the conditions run in the current shell, so a function
     defined in any of them is callable from a sibling and must be registered
     before the ordered walk warns about a forward reference. */
  for (const auto &[condition, body] : m_branches) {
    ASSERT(condition != nullptr);
    ASSERT(body != nullptr);
    condition->register_defined_functions(actx);
    body->register_defined_functions(actx);
  }

  if (m_otherwise != nullptr) m_otherwise->register_defined_functions(actx);
}

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

  /* The condition runs at least once, the body may run zero times. */
  m_condition->analyze(actx, is_unconditional);
  m_body->analyze(actx, false);
}

cold fn WhileLoop::register_defined_functions(AnalysisContext &actx) const throws
    -> void
{
  ASSERT(m_condition != nullptr);
  ASSERT(m_body != nullptr);

  /* The condition and the body both run in the current shell, so a function
     defined in either is registered before the ordered walk. */
  m_condition->register_defined_functions(actx);
  m_body->register_defined_functions(actx);
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
  /* Without an in clause the loop walks the positional parameters. */
  let const values =
      m_has_in_clause ? cxt.process_args(m_words) : cxt.positional_params();

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
      let const pattern = expand_no_glob(pattern_token);
      let all_active = ArrayList<bool>{heap_allocator()};
      for (usize k = 0; k < pattern.count(); k++)
        all_active.push(true);
      if (utils::glob_matches(pattern, subject, all_active, 0)) {
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
}

cold fn CaseClause::register_defined_functions(AnalysisContext &actx) const throws
    -> void
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
  return m_body->evaluate(cxt);
}

cold fn BraceGroup::analyze(AnalysisContext &actx,
                            bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  m_body->analyze(actx, is_unconditional);
}

cold fn BraceGroup::register_defined_functions(AnalysisContext &actx) const
    throws -> void
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

  /* This shell has no process-level subshell, so isolate by snapshot. A cd or
     an assignment inside does not leak, but the exit status propagates. An exit
     inside ends only the subshell. */
  let snapshot = cxt.snapshot_state();
  cxt.enter_subshell();
  i64 ret = 0;
  /* A diagnostic thrown by the body, such as a readonly violation or a missing
     command, must still restore the snapshot and leave the subshell, otherwise
     the parent stays stuck in subshell mode with the inner state leaked. */
  try {
    ret = m_body->evaluate(cxt);
  } catch (...) {
    cxt.leave_subshell();
    cxt.restore_state(steal(snapshot));
    throw;
  }

  /* An exit inside the subshell ends only the subshell and supplies its status.
     A break, a continue, or a return stays pending and propagates after the
     state is restored, the same as the old re-throw did. */
  if (cxt.has_pending_control_flow() &&
      cxt.pending_control_flow().kind == control_flow::Kind::Exit)
  {
    ret = cxt.pending_control_flow().value;
    cxt.clear_control_flow();
  }

  cxt.leave_subshell();
  cxt.restore_state(steal(snapshot));
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

cold fn Subshell::analyze(AnalysisContext &actx,
                          bool is_unconditional) const throws -> void
{
  ASSERT(m_body != nullptr);

  m_body->analyze(actx, is_unconditional);
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
  m_body->analyze(actx, false);
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

    let mode = os::file_open_mode::Read;
    if (redir.kind == Redirection::Kind::TruncateOutput)
      mode = cxt.no_clobber() ? os::file_open_mode::TruncateNoClobber
                              : os::file_open_mode::Truncate;
    else if (redir.kind == Redirection::Kind::AppendOutput)
      mode = os::file_open_mode::Append;

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
  let pad = String{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
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
  let pad = String{};

  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
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
  let pad = String{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
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
  let pad = String{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
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

cold fn SimpleCommand::analyze(AnalysisContext &actx,
                               bool is_unconditional) const throws -> void
{
  /* A missing command is only warned about, so the conditional context no
     longer changes the prepass outcome for this node. */
  unused(is_unconditional);

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

  /* A dot, source, eval, or alias runs or defines code the prepass cannot see,
     so any later unresolved command must not be a hard failure. */
  if (command_literal == "." || command_literal == "source" ||
      command_literal == "eval" || command_literal == "alias")
  {
    actx.saw_runtime_definer = true;
  }

  /* An alias defined earlier in the same input resolves at runtime, so record
     each name this alias command defines for the later resolution check. */
  if (command_literal == "alias") {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const literal = static_cast<const tokens::WordToken *>(m_args[i])
                              ->word()
                              .to_literal_string();
      let const equals_position = literal.find_character('=');
      if (equals_position.has_value() && *equals_position > 0)
        actx.known_aliases.add(StringView{literal.data(), *equals_position});
    }
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
     splits into several words. */
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

  /* A prefix assignment does not affect the expansion on the same command, so a
     reference to one of its names reads the old value. */
  if (m_local_vars.count() > 0) {
    for (usize i = 1; i < m_args.count(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      let const &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      for (const WordSegment &segment : word.segments) {
        if (segment.kind == WordSegment::Kind::VariableReference &&
            m_local_vars.find(StringView{segment.text.data(),
                                         segment.text.count()}) != nullptr)
        {
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

  if (actx.warn_missing_commands && name && !command_resolves(actx, *name) &&
      !actx.defined_functions.contains(
          StringView{name->data(), name->count()}) &&
      !actx.known_aliases.contains(StringView{name->data(), name->count()}))
  {
    let const message = StringView{"Command '"} + StringView{*name} +
                        StringView{"' was not found"};
    /* Point at the command word, not at the whole command. With an assignment
       prefix the command location is the assignment, not the program name. The
       prepass only warns, never aborts, since a missing command at runtime is
       non-fatal under POSIX and yields exit status 127 while evaluation
       continues. The runtime resolution in SimpleCommand prints the same caret
       to stderr and sets 127 when the command actually runs. */
    actx.warn(m_args[0]->source_location(), message);
  }
}

cold fn Pipeline::analyze(AnalysisContext &actx,
                          bool is_unconditional) const throws -> void
{
  for (const Command *command : m_commands) {
    ASSERT(command != nullptr);
    command->analyze(actx, is_unconditional);
  }
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

cold fn CompoundList::register_defined_functions(AnalysisContext &actx) const
    throws -> void
{
  for (const CompoundListCondition *node : m_nodes) {
    ASSERT(node != nullptr);
    node->register_defined_functions(actx);
  }
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
}

cold fn IfStatement::register_defined_functions(AnalysisContext &actx) const
    throws -> void
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
