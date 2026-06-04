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

#include <iostream>
#include <optional>
#include <utility>

namespace shit {

Expression::Expression(SourceLocation location) : m_location(location) {}

SourceLocation
Expression::source_location() const
{
  return m_location;
}

std::string
Expression::to_ast_string(usize layer) const
{
  std::string pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
}

i64
Expression::evaluate(EvalContext &cxt) const
{
  cxt.add_evaluated_expression();
  return evaluate_impl(cxt);
}

void
Expression::operator delete(void *pointer)
{
  if (g_ast_arena != nullptr && g_ast_arena->owns(pointer)) return;
  ::operator delete(pointer);
}

void
AnalysisContext::warn(SourceLocation location, const std::string &message)
{
  ErrorWithLocation located{location, message};
  show_message(located.to_string(source, "Warning"));
}

void
AnalysisContext::fail(SourceLocation location, const std::string &message)
{
  ErrorWithLocation located{location, message};
  show_message(located.to_string(source, "Error"));
  has_fatal = true;
}

void
Expression::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  SHIT_UNUSED(actx);
  SHIT_UNUSED(is_unconditional);
}

bool
Expression::is_simple_command() const
{
  return false;
}

bool
Expression::is_dummy() const
{
  return false;
}

namespace {

/* The literal name of a command when it is statically known. A word that holds
   a variable reference or a live glob metacharacter is dynamic, so its target
   cannot be checked before run time. */
std::optional<std::string>
static_command_name(const Token *token)
{
  if (token->kind() != Token::Kind::Word) return std::nullopt;

  const Word &word = static_cast<const tokens::WordToken *>(token)->word();

  std::string name{};
  for (const WordSegment &segment : word.segments) {
    if (segment.kind == WordSegment::Kind::VariableReference)
      return std::nullopt;
    if (segment.kind == WordSegment::Kind::UnquotedText) {
      for (char ch : segment.text) {
        if (lexer::is_expandable_char(ch)) return std::nullopt;
      }
    }
    name += segment.text;
  }
  return name;
}

/* A command resolves when it is a builtin, a program on PATH, or an existing
   path. This shares the PATH cache with execution, so an unconditional command
   is resolved only once. */
bool
command_resolves(const std::string &name)
{
  if (name.empty()) return false;
  if (search_builtin(name).has_value()) return true;
  if (name.find('/') != std::string::npos)
    return utils::canonicalize_path(name).has_value();
  return !utils::search_program_path(name).empty();
}

bool
word_has_backtick(const Word &word)
{
  for (const WordSegment &segment : word.segments) {
    if (segment.text.find('`') != std::string::npos) return true;
  }
  return false;
}

} /* namespace */

bool
analyze_ast(const Expression *root, std::string_view source)
{
  AnalysisContext actx{source};
  root->analyze(actx, true);
  return !actx.has_fatal;
}

namespace expressions {

IfStatement::IfStatement(SourceLocation location, const Expression *condition,
                         const Expression *then, const Expression *otherwise)
    : Expression(location), m_condition(condition), m_then(then),
      m_otherwise(otherwise)
{
  SHIT_ASSERT(condition != nullptr);
  SHIT_ASSERT(then != nullptr);
  /* And *otherwise may be NULL. */
}

IfStatement::~IfStatement()
{
  delete m_condition;
  delete m_then;

  if (m_otherwise != nullptr) delete m_otherwise;
}

i64
IfStatement::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  if (m_condition->evaluate(cxt))
    return m_then->evaluate(cxt);
  else if (m_otherwise != nullptr)
    return m_otherwise->evaluate(cxt);

  return 0;
}

std::string
IfStatement::to_string() const
{
  return "If";
}

std::string
IfStatement::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};

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

void
Command::make_async()
{
  m_is_async = true;
}

bool
Command::is_async() const
{
  return m_is_async;
}

void
Command::set_negated()
{
  m_is_negated = true;
}

bool
Command::is_negated() const
{
  return m_is_negated;
}

void
Command::set_local_vars(std::unordered_map<std::string, Word> &&vars)
{
  m_local_vars = std::move(vars);
}

bool
Command::is_assignment() const
{
  return false;
}

DummyExpression::DummyExpression(SourceLocation location) : Expression(location)
{}

bool
DummyExpression::is_dummy() const
{
  return true;
}

i64
DummyExpression::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  return 0;
}

std::string
DummyExpression::to_string() const
{
  return "Dummy";
}

std::string
DummyExpression::to_ast_string(usize layer) const
{
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

AssignCommand::AssignCommand(SourceLocation location, const Assignment *a)
    : Command(location), m_assignment(a)
{}

AssignCommand::~AssignCommand() { delete m_assignment; }

const Assignment *
AssignCommand::assignment() const
{
  return m_assignment;
}

bool
AssignCommand::is_assignment() const
{
  return true;
}

i64
AssignCommand::evaluate_impl(EvalContext &cxt) const
{
  /* The status defaults to 0, but a command substitution in the value sets it
     to the status of that substitution, which the assignment then reports. */
  cxt.set_last_exit_status(0);
  cxt.set_shell_variable(m_assignment->key(), cxt.expand_word_for_assignment(
                                                  m_assignment->value_word()));
  return cxt.last_exit_status();
}

std::string
AssignCommand::to_string() const
{
  std::string s = "Assign " + m_assignment->to_ast_string();

  return s;
}

std::string
AssignCommand::to_ast_string(usize layer) const
{
  std::string pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
}

void
AssignCommand::redirect_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

void
AssignCommand::append_to(usize d, std::string &f, bool duplicate)
{
  redirect_to(d, f, duplicate);
}

SimpleCommand::SimpleCommand(SourceLocation location,
                             const std::vector<const Token *> &&args)
    : Command(location), m_args(args)
{}

SimpleCommand::~SimpleCommand()
{
  for (const Token *t : m_args) {
    delete t;
  }
  for (const Redirection &redirection : m_redirections) {
    delete redirection.target;
  }
}

void
SimpleCommand::set_redirections(std::vector<Redirection> &&redirections)
{
  m_redirections = std::move(redirections);
}

void
SimpleCommand::redirect_exec_context(ExecContext &ec, EvalContext &cxt) const
{
  for (const Redirection &redirection : m_redirections) {
    if (redirection.kind == Redirection::Kind::Heredoc) {
      std::string body = *redirection.heredoc_body;
      if (redirection.heredoc_expand) body = cxt.expand_heredoc_body(body);
      std::optional<os::descriptor> opened = os::write_to_temp_file(body);
      if (!opened) throw Error{"Could not stage the heredoc body"};
      if (ec.in_fd) os::close_fd(*ec.in_fd);
      ec.in_fd = opened;
      continue;
    }

    if (redirection.kind == Redirection::Kind::DuplicateOutput) {
      if (redirection.fd == 2 && redirection.dup_fd == 1)
        ec.dup_err_to_out = true;
      else if (redirection.fd == 1 && redirection.dup_fd == 2)
        ec.dup_out_to_err = true;
      continue;
    }

    std::vector<std::string> target = cxt.process_args({redirection.target});
    if (target.size() != 1) {
      throw ErrorWithLocation{redirection.target->source_location(),
                              "Redirection target is not a single file"};
    }

    os::FileOpenMode mode = os::FileOpenMode::Read;
    if (redirection.kind == Redirection::Kind::TruncateOutput)
      mode = os::FileOpenMode::Truncate;
    else if (redirection.kind == Redirection::Kind::AppendOutput)
      mode = os::FileOpenMode::Append;

    std::optional<os::descriptor> opened =
        os::open_file_descriptor(target[0], mode);
    if (!opened) {
      throw ErrorWithLocation{redirection.target->source_location(),
                              "Could not open '" + target[0] + "': " +
                                  os::last_system_error_message()};
    }

    if (redirection.fd == 0) {
      if (ec.in_fd) os::close_fd(*ec.in_fd);
      ec.in_fd = opened;
    } else if (redirection.fd == 2) {
      if (ec.err_fd) os::close_fd(*ec.err_fd);
      ec.err_fd = opened;
    } else {
      if (ec.out_fd) os::close_fd(*ec.out_fd);
      ec.out_fd = opened;
    }
  }
}

bool
SimpleCommand::is_simple_command() const
{
  return true;
}

const std::vector<const Token *> &
SimpleCommand::args() const
{
  return m_args;
}

i64
SimpleCommand::evaluate_impl(EvalContext &cxt) const
{
  /* A command may have no words when it is only a redirection, such as > file,
     so the redirections still run below. */
  SHIT_ASSERT(m_args.size() > 0 || !m_redirections.empty());

  if (cxt.should_echo())
    std::cout << utils::merge_tokens_to_string(m_args) << std::endl;

  std::vector<std::string> program_args = cxt.process_args(m_args);

  /* Open the redirection targets. A redirection takes effect even when the
     command expands to nothing, so > file with no command still creates the
     file. The final descriptors pass to the exec context, which closes them,
     and the guard closes them on any path that does not hand them off. */
  std::optional<os::descriptor> redirect_in_fd;
  std::optional<os::descriptor> redirect_out_fd;
  std::optional<os::descriptor> redirect_err_fd;
  bool dup_err_to_out = false;
  bool dup_out_to_err = false;
  bool redirect_fds_handed_off = false;
  SHIT_DEFER
  {
    if (!redirect_fds_handed_off) {
      if (redirect_in_fd) os::close_fd(*redirect_in_fd);
      if (redirect_out_fd) os::close_fd(*redirect_out_fd);
      if (redirect_err_fd) os::close_fd(*redirect_err_fd);
    }
  };

  for (const Redirection &redirection : m_redirections) {
    /* A heredoc body becomes the standard input through an anonymous temp
       file, expanded when the delimiter was unquoted. */
    if (redirection.kind == Redirection::Kind::Heredoc) {
      std::string body = *redirection.heredoc_body;
      if (redirection.heredoc_expand) body = cxt.expand_heredoc_body(body);
      std::optional<os::descriptor> opened = os::write_to_temp_file(body);
      if (!opened) throw Error{"Could not stage the heredoc body"};
      if (redirect_in_fd) os::close_fd(*redirect_in_fd);
      redirect_in_fd = opened;
      continue;
    }

    /* A duplication like 2>&1 routes one descriptor to another without a file.
     */
    if (redirection.kind == Redirection::Kind::DuplicateOutput) {
      if (redirection.fd == 2 && redirection.dup_fd == 1)
        dup_err_to_out = true;
      else if (redirection.fd == 1 && redirection.dup_fd == 2)
        dup_out_to_err = true;
      continue;
    }

    std::vector<std::string> target = cxt.process_args({redirection.target});
    if (target.size() != 1) {
      throw ErrorWithLocation{redirection.target->source_location(),
                              "Redirection target is not a single file"};
    }

    os::FileOpenMode mode = os::FileOpenMode::Read;
    if (redirection.kind == Redirection::Kind::TruncateOutput)
      mode = os::FileOpenMode::Truncate;
    else if (redirection.kind == Redirection::Kind::AppendOutput)
      mode = os::FileOpenMode::Append;

    std::optional<os::descriptor> opened =
        os::open_file_descriptor(target[0], mode);
    if (!opened) {
      throw ErrorWithLocation{redirection.target->source_location(),
                              "Could not open '" + target[0] + "': " +
                                  os::last_system_error_message()};
    }

    /* The last redirection of a descriptor wins, so a superseded open closes
       at once. */
    if (redirection.fd == 0) {
      if (redirect_in_fd) os::close_fd(*redirect_in_fd);
      redirect_in_fd = opened;
    } else if (redirection.fd == 2) {
      if (redirect_err_fd) os::close_fd(*redirect_err_fd);
      redirect_err_fd = opened;
    } else {
      if (redirect_out_fd) os::close_fd(*redirect_out_fd);
      redirect_out_fd = opened;
    }
  }

  /* An expansion may drop every word, for example an unset $x used as the whole
     command. There is nothing to run then, but the redirections above already
     took effect. */
  if (program_args.empty()) {
    cxt.set_last_exit_status(0);
    return 0;
  }

  /* Per-command assignments apply to the environment for this command, a
     function call included, so a child inherits them and a function sees them.
     The previous values are restored on every exit path. */
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_env{};
  if (m_local_vars) {
    for (const auto &[name, value_word] : *m_local_vars) {
      saved_env.emplace_back(name, os::get_environment_variable(name));
      os::set_environment_variable(name,
                                   cxt.expand_word_for_assignment(value_word));
    }
  }
  SHIT_DEFER
  {
    for (const auto &[name, old_value] : saved_env) {
      if (old_value)
        os::set_environment_variable(name, *old_value);
      else
        os::unset_environment_variable(name);
    }
  };

  /* A function shadows a builtin and a program. Run its body with the call
     words as the positional parameters, restoring them afterwards. A return
     builtin unwinds here and supplies the function exit status. */
  if (const Expression *function_body =
          cxt.has_functions() ? cxt.find_function(program_args[0]) : nullptr;
      function_body != nullptr)
  {
    std::vector<std::string> saved_params = cxt.positional_params();
    cxt.set_positional_params(
        std::vector<std::string>{program_args.begin() + 1, program_args.end()});
    SHIT_DEFER { cxt.set_positional_params(std::move(saved_params)); };

    i64 function_ret = 0;
    try {
      function_ret = function_body->evaluate(cxt);
    } catch (const FunctionReturn &returned) {
      function_ret = returned.status;
    }

    cxt.set_last_exit_status(static_cast<i32>(function_ret));
    return function_ret;
  }

  if (cxt.shell_is_interactive())
    toiletline::set_title(utils::merge_args_to_string(program_args));

  /* Reuse a memoized resolution when the command word is unchanged, otherwise
     search PATH once and remember the result for the next run. */
  bool is_cache_valid =
      m_resolved_kind.has_value() && m_resolved_name == program_args[0];

  ExecContext ec =
      is_cache_valid ? ExecContext::from_resolved(
                           source_location(), *m_resolved_kind, program_args)
                     : ExecContext::make_from(source_location(), program_args);

  if (!is_cache_valid) {
    if (ec.is_builtin())
      m_resolved_kind = ec.builtin_kind();
    else
      m_resolved_kind = ec.program_path();
    m_resolved_name = program_args[0];
  }

  /* The redirections override the inherited descriptors for this command. The
     exec context now owns the opened files and closes them. */
  if (redirect_in_fd) ec.in_fd = redirect_in_fd;
  if (redirect_out_fd) ec.out_fd = redirect_out_fd;
  if (redirect_err_fd) ec.err_fd = redirect_err_fd;
  ec.dup_err_to_out = dup_err_to_out;
  ec.dup_out_to_err = dup_out_to_err;
  redirect_fds_handed_off = true;

  i64 ret = utils::execute_context(std::move(ec), cxt, is_async());

  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

std::string
SimpleCommand::to_string() const
{
  std::string s = "SimpleCommand";

  /* A pipeline stage that is a bare assignment carries the assignment in the
     local variables and has no command word, so the argument list is empty. */
  if (!m_args.empty()) {
    s += " \"" + m_args[0]->raw_string() + "\"";
    for (usize i = 1; i < m_args.size(); i++) {
      s += " \"";
      s += m_args[i]->raw_string();
      s += "\"";
    }
  }
  if (is_async()) s += ", Async";

  return s;
}

std::string
SimpleCommand::to_ast_string(usize layer) const
{
  std::string pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
}

void
SimpleCommand::append_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

void
SimpleCommand::redirect_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

CompoundList::CompoundList() : Expression({0, 0}), m_nodes() {}

CompoundList::CompoundList(
    SourceLocation location,
    const std::vector<const CompoundListCondition *> &nodes)
    : Expression(location), m_nodes(nodes)
{}

CompoundList::~CompoundList()
{
  for (const CompoundListCondition *e : m_nodes) {
    delete e;
  }
}

bool
CompoundList::is_empty() const
{
  return m_nodes.empty();
}

void
CompoundList::append_node(const CompoundListCondition *node)
{
  m_location.add_length(node->source_location().length());
  m_nodes.emplace_back(node);
}

std::string
CompoundList::to_string() const
{
  return "CompoundList";
}

std::string
CompoundList::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};

  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  s += pad + "[" + to_string() + "]";
  for (const CompoundListCondition *n : m_nodes) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + n->to_ast_string(layer + 1);
  }

  return s;
}

i64
CompoundList::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_nodes.size() > 0);

  static const i64 NOTHING_WAS_EXECUTED = -256;

  i64 ret = NOTHING_WAS_EXECUTED;

  for (const CompoundListCondition *n : m_nodes) {
    switch (n->kind()) {
    case CompoundListCondition::Kind::None: ret = n->evaluate(cxt); break;

    case CompoundListCondition::Kind::Or:
      if (ret != 0) ret = n->evaluate(cxt);
      break;

    case CompoundListCondition::Kind::And:
      if (ret == 0) ret = n->evaluate(cxt);
      break;
    }
  }

  SHIT_ASSERT(ret != NOTHING_WAS_EXECUTED);

  return ret;
}

CompoundListCondition::CompoundListCondition(SourceLocation location, Kind kind,
                                             const Command *expr)
    : Expression(location), m_kind(kind), m_cmd(expr)
{}

CompoundListCondition::~CompoundListCondition() { delete m_cmd; }

CompoundListCondition::Kind
CompoundListCondition::kind() const
{
  return m_kind;
}

std::string
CompoundListCondition::to_string() const
{
  std::string k;
  switch (kind()) {
  case Kind::None: k = "None"; break;
  case Kind::And: k = "&&"; break;
  case Kind::Or: k = "||"; break;
  default: SHIT_UNREACHABLE();
  }
  return "CompoundListCondition, " + k;
}

std::string
CompoundListCondition::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;

  s += pad + "[" + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_cmd->to_ast_string(layer + 1);

  return s;
}

i64
CompoundListCondition::evaluate_impl(EvalContext &cxt) const
{
  i64 status = m_cmd->evaluate(cxt);

  /* A pipeline prefixed with ! reports the inverse of its status, and that
     inverse is what $? sees. */
  if (m_cmd->is_negated()) {
    status = (status == 0) ? 1 : 0;
    cxt.set_last_exit_status(static_cast<i32>(status));
  }

  return status;
}

Pipeline::Pipeline(SourceLocation location,
                   const std::vector<const SimpleCommand *> &commands)
    : Command(location), m_commands(commands)
{}

Pipeline::Pipeline(SourceLocation location) : Command(location), m_commands({})
{}

Pipeline::~Pipeline()
{
  for (const SimpleCommand *e : m_commands) {
    delete e;
  }
}

bool
Pipeline::is_empty() const
{
  return m_commands.empty();
}

void
Pipeline::append_command(const SimpleCommand *node)
{
  m_location.add_length(node->source_location().length());
  m_commands.emplace_back(node);
}

std::string
Pipeline::to_string() const
{
  std::string s = "Pipeline";
  if (is_async()) s += ", Async";
  return s;
}

std::string
Pipeline::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }

  s += pad + "[" + to_string() + "]";
  for (const SimpleCommand *e : m_commands) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + e->to_ast_string(layer + 1);
  }

  return s;
}

i64
Pipeline::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_commands.size() > 1);

  std::vector<ExecContext> ecs;
  ecs.reserve(m_commands.size());

  for (const SimpleCommand *e : m_commands) {
    cxt.add_evaluated_expression();

    std::vector<std::string> stage_args = cxt.process_args(e->args());

    /* A stage that expands to no command word, such as a bare assignment or an
       unset variable, has no program to run. Report it instead of building an
       exec context from an empty argument list, which would read past the
       arguments. */
    if (stage_args.empty()) {
      throw ErrorWithLocation{
          e->source_location(),
          "A pipeline stage expanded to no command to run"};
    }

    ExecContext ec =
        ExecContext::make_from(e->source_location(), std::move(stage_args));
    /* Apply this stage's own redirections, such as 2>&1, before the pipe wires
       its descriptors. The pipe only sets stdin and stdout, so a stderr
       redirection composes with it. */
    e->redirect_exec_context(ec, cxt);
    ecs.emplace_back(std::move(ec));
  }

  return utils::execute_contexts_with_pipes(std::move(ecs), cxt, is_async());
}

void
Pipeline::append_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

void
Pipeline::redirect_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

CompoundCommand::CompoundCommand(SourceLocation location) : Command(location) {}

void
CompoundCommand::append_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(),
                          "Redirection on a compound command is not supported"};
}

void
CompoundCommand::redirect_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(),
                          "Redirection on a compound command is not supported"};
}

static std::string
indent_for_layer(usize layer)
{
  std::string pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad;
}

IfClause::IfClause(
    SourceLocation location,
    std::vector<std::pair<const Expression *, const Expression *>> &&branches,
    const Expression *otherwise)
    : CompoundCommand(location), m_branches(std::move(branches)),
      m_otherwise(otherwise)
{}

IfClause::~IfClause()
{
  for (const auto &[condition, body] : m_branches) {
    delete condition;
    delete body;
  }
  delete m_otherwise;
}

std::string
IfClause::to_string() const
{
  return "IfClause";
}

std::string
IfClause::to_ast_string(usize layer) const
{
  std::string pad = indent_for_layer(layer);
  std::string child_pad = pad + EXPRESSION_AST_INDENT;
  std::string s = pad + "[" + to_string() + "]";
  for (const auto &[condition, body] : m_branches) {
    s += '\n' + child_pad + condition->to_ast_string(layer + 1);
    s += '\n' + child_pad + body->to_ast_string(layer + 1);
  }
  if (m_otherwise != nullptr)
    s += '\n' + child_pad + m_otherwise->to_ast_string(layer + 1);
  return s;
}

i64
IfClause::evaluate_impl(EvalContext &cxt) const
{
  for (const auto &[condition, body] : m_branches) {
    if (condition->evaluate(cxt) == 0) return body->evaluate(cxt);
  }
  if (m_otherwise != nullptr) return m_otherwise->evaluate(cxt);
  return 0;
}

void
IfClause::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  /* The first condition runs whenever the if runs. The elif conditions and all
     bodies are conditional, since a branch may not be reached. */
  bool is_first_branch = true;
  for (const auto &[condition, body] : m_branches) {
    condition->analyze(actx, is_unconditional && is_first_branch);
    body->analyze(actx, false);
    is_first_branch = false;
  }
  if (m_otherwise != nullptr) m_otherwise->analyze(actx, false);
}

WhileLoop::WhileLoop(SourceLocation location, const Expression *condition,
                     const Expression *body, bool is_until)
    : CompoundCommand(location), m_condition(condition), m_body(body),
      m_is_until(is_until)
{}

WhileLoop::~WhileLoop()
{
  delete m_condition;
  delete m_body;
}

std::string
WhileLoop::to_string() const
{
  return m_is_until ? "UntilLoop" : "WhileLoop";
}

std::string
WhileLoop::to_ast_string(usize layer) const
{
  std::string pad = indent_for_layer(layer);
  std::string child_pad = pad + EXPRESSION_AST_INDENT;
  std::string s = pad + "[" + to_string() + "]";
  s += '\n' + child_pad + m_condition->to_ast_string(layer + 1);
  s += '\n' + child_pad + m_body->to_ast_string(layer + 1);
  return s;
}

i64
WhileLoop::evaluate_impl(EvalContext &cxt) const
{
  i64 ret = 0;
  for (;;) {
    i64 condition_status = m_condition->evaluate(cxt);
    bool should_run_body =
        m_is_until ? (condition_status != 0) : (condition_status == 0);
    if (!should_run_body) break;

    try {
      ret = m_body->evaluate(cxt);
    } catch (LoopControl &control) {
      /* A break or continue aimed at an outer loop keeps unwinding. */
      if (control.level > 1) {
        control.level--;
        throw;
      }
      if (control.kind == LoopControl::Kind::Break) break;
      /* A continue falls through to the next iteration. */
    }
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

void
WhileLoop::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  /* The condition runs at least once, the body may run zero times. */
  m_condition->analyze(actx, is_unconditional);
  m_body->analyze(actx, false);
}

ForLoop::ForLoop(SourceLocation location, std::string variable_name,
                 std::vector<const Token *> &&words, bool has_in_clause,
                 const Expression *body)
    : CompoundCommand(location), m_variable_name(std::move(variable_name)),
      m_words(std::move(words)), m_has_in_clause(has_in_clause), m_body(body)
{}

ForLoop::~ForLoop()
{
  for (const Token *t : m_words)
    delete t;
  delete m_body;
}

std::string
ForLoop::to_string() const
{
  return "ForLoop \"" + m_variable_name + "\"";
}

std::string
ForLoop::to_ast_string(usize layer) const
{
  std::string pad = indent_for_layer(layer);
  std::string s = pad + "[" + to_string() + "]";
  s += '\n' + pad + EXPRESSION_AST_INDENT + m_body->to_ast_string(layer + 1);
  return s;
}

i64
ForLoop::evaluate_impl(EvalContext &cxt) const
{
  /* Without an in clause the loop walks the positional parameters. */
  std::vector<std::string> values =
      m_has_in_clause ? cxt.process_args(m_words) : cxt.positional_params();

  i64 ret = 0;
  for (const std::string &value : values) {
    cxt.set_shell_variable(m_variable_name, value);
    try {
      ret = m_body->evaluate(cxt);
    } catch (LoopControl &control) {
      if (control.level > 1) {
        control.level--;
        throw;
      }
      if (control.kind == LoopControl::Kind::Break) break;
    }
  }
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

void
ForLoop::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  SHIT_UNUSED(is_unconditional);
  m_body->analyze(actx, false);
}

CaseClause::CaseClause(SourceLocation location, const Token *word,
                       std::vector<CaseItem> &&items)
    : CompoundCommand(location), m_word(word), m_items(std::move(items))
{}

CaseClause::~CaseClause()
{
  delete m_word;
  for (const CaseItem &item : m_items) {
    for (const Token *pattern : item.patterns)
      delete pattern;
    delete item.body;
  }
}

std::string
CaseClause::to_string() const
{
  return "CaseClause";
}

std::string
CaseClause::to_ast_string(usize layer) const
{
  std::string pad = indent_for_layer(layer);
  std::string child_pad = pad + EXPRESSION_AST_INDENT;
  std::string s = pad + "[" + to_string() + "]";
  for (const CaseItem &item : m_items)
    s += '\n' + child_pad + item.body->to_ast_string(layer + 1);
  return s;
}

i64
CaseClause::evaluate_impl(EvalContext &cxt) const
{
  /* A case word and its patterns expand with variables and tilde but no field
     splitting and no pathname globbing, so a pattern keeps its metacharacters
     for matching. */
  auto expand_no_glob = [&cxt](const Token *t) -> std::string {
    if (t->kind() == Token::Kind::Word)
      return cxt.expand_word_for_assignment(
          static_cast<const tokens::WordToken *>(t)->word());
    return t->raw_string();
  };

  std::string subject = expand_no_glob(m_word);

  for (const CaseItem &item : m_items) {
    for (const Token *pattern_token : item.patterns) {
      std::string pattern = expand_no_glob(pattern_token);
      std::vector<bool> all_active(pattern.size(), true);
      if (utils::glob_matches(pattern, subject, all_active, 0)) {
        i64 ret = item.body->evaluate(cxt);
        cxt.set_last_exit_status(static_cast<i32>(ret));
        return ret;
      }
    }
  }

  cxt.set_last_exit_status(0);
  return 0;
}

void
CaseClause::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  SHIT_UNUSED(is_unconditional);
  for (const CaseItem &item : m_items)
    item.body->analyze(actx, false);
}

BraceGroup::BraceGroup(SourceLocation location, const Expression *body)
    : CompoundCommand(location), m_body(body)
{}

BraceGroup::~BraceGroup() { delete m_body; }

std::string
BraceGroup::to_string() const
{
  return "BraceGroup";
}

std::string
BraceGroup::to_ast_string(usize layer) const
{
  std::string pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

i64
BraceGroup::evaluate_impl(EvalContext &cxt) const
{
  return m_body->evaluate(cxt);
}

void
BraceGroup::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  m_body->analyze(actx, is_unconditional);
}

Subshell::Subshell(SourceLocation location, const Expression *body)
    : CompoundCommand(location), m_body(body)
{}

Subshell::~Subshell() { delete m_body; }

std::string
Subshell::to_string() const
{
  return "Subshell";
}

std::string
Subshell::to_ast_string(usize layer) const
{
  std::string pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

i64
Subshell::evaluate_impl(EvalContext &cxt) const
{
  /* This shell has no process-level subshell, so isolate by snapshot. A cd or
     an assignment inside does not leak, but the exit status propagates. An exit
     inside ends only the subshell. */
  EvalStateSnapshot snapshot = cxt.snapshot_state();
  cxt.enter_subshell();
  i64 ret = 0;
  try {
    ret = m_body->evaluate(cxt);
  } catch (const ShellExit &exited) {
    ret = exited.status;
  } catch (...) {
    cxt.leave_subshell();
    cxt.restore_state(std::move(snapshot));
    throw;
  }
  cxt.leave_subshell();
  cxt.restore_state(std::move(snapshot));
  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

void
Subshell::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  m_body->analyze(actx, is_unconditional);
}

FunctionDefinition::FunctionDefinition(SourceLocation location,
                                       std::string name, const Expression *body)
    : CompoundCommand(location), m_name(std::move(name)), m_body(body)
{}

FunctionDefinition::~FunctionDefinition() { delete m_body; }

const std::string &
FunctionDefinition::name() const
{
  return m_name;
}

const Expression *
FunctionDefinition::body() const
{
  return m_body;
}

std::string
FunctionDefinition::to_string() const
{
  return "FunctionDefinition \"" + m_name + "\"";
}

std::string
FunctionDefinition::to_ast_string(usize layer) const
{
  std::string pad = indent_for_layer(layer);
  return pad + "[" + to_string() + "]\n" + pad + EXPRESSION_AST_INDENT +
         m_body->to_ast_string(layer + 1);
}

i64
FunctionDefinition::evaluate_impl(EvalContext &cxt) const
{
  cxt.register_function(m_name, m_body);
  cxt.set_last_exit_status(0);
  return 0;
}

void
FunctionDefinition::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  SHIT_UNUSED(is_unconditional);
  actx.defined_functions.insert(m_name);
  m_body->analyze(actx, false);
}

UnaryExpression::UnaryExpression(SourceLocation location, const Expression *rhs)
    : Expression(location), m_rhs(rhs)
{}

UnaryExpression::~UnaryExpression() { delete m_rhs; }

std::string
UnaryExpression::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
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

BinaryExpression::~BinaryExpression()
{
  delete m_lhs;
  delete m_rhs;
}

std::string
BinaryExpression::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};

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

i64
ConstantNumber::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  return m_value;
}

std::string
ConstantNumber::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  s += pad + "[Number " + to_string() + "]";
  return s;
}

std::string
ConstantNumber::to_string() const
{
  return std::to_string(m_value);
}

ConstantString::ConstantString(SourceLocation location,
                               const std::string &value)
    : Expression(location), m_value(value)
{}

ConstantString::~ConstantString() = default;

i64
ConstantString::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  SHIT_UNREACHABLE();
}

std::string
ConstantString::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  s += pad + "[String \"" + to_string() + "\"]";
  return s;
}

std::string
ConstantString::to_string() const
{
  return m_value;
}

#define UNARY_EXPRESSION_DECLS(e, expr)                                        \
  e::e(SourceLocation location, const Expression *rhs)                         \
      : UnaryExpression(location, rhs)                                         \
  {}                                                                           \
  std::string e::to_string() const { return #expr; }                           \
  i64 e::evaluate_impl(EvalContext &cxt) const                                 \
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

std::string
BinaryDummyExpression::to_string() const
{
  return "BinaryDummyExpression";
}

i64
BinaryDummyExpression::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  return 0;
}

Divide::Divide(SourceLocation location, const Expression *lhs,
               const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Divide::to_string() const
{
  return "/";
}

/* Custom evaluation, since we can't divide by zero. */
i64
Divide::evaluate_impl(EvalContext &cxt) const
{
  i64 denom = m_rhs->evaluate(cxt);
  if (denom == 0)
    throw ErrorWithLocation{m_rhs->source_location(), "Division by 0"};
  return m_lhs->evaluate(cxt) / denom;
}

#define BINARY_EXPRESSION_DECLS(e, expr)                                       \
  e::e(SourceLocation location, const Expression *lhs, const Expression *rhs)  \
      : BinaryExpression(location, lhs, rhs)                                   \
  {}                                                                           \
  std::string e::to_string() const { return #expr; }                           \
  i64 e::evaluate_impl(EvalContext &cxt) const                                 \
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

void
SimpleCommand::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  if (m_args.empty()) return;

  std::optional<std::string> name = static_command_name(m_args[0]);

  /* The literal command text, used for the test recognition. A name like [
     holds a glob metacharacter, so static_command_name rejects it, but the test
     check still needs to see it. */
  std::string command_literal =
      m_args[0]->kind() == Token::Kind::Word
          ? static_cast<const tokens::WordToken *>(m_args[0])
                ->word()
                .to_literal_string()
          : m_args[0]->raw_string();

  /* A dot, source, or eval runs code the prepass cannot see, so any later
     unresolved command must not be a hard failure. */
  if (command_literal == "." || command_literal == "source" ||
      command_literal == "eval")
  {
    actx.saw_runtime_definer = true;
  }

  /* A backtick anywhere in the command is an unsupported substitution. */
  for (const Token *t : m_args) {
    if (t->kind() != Token::Kind::Word) continue;
    const Word &word = static_cast<const tokens::WordToken *>(t)->word();
    if (word_has_backtick(word)) {
      actx.warn(t->source_location(),
                "Backquote command substitution is not supported, use $(...) "
                "instead");
    }
  }

  /* An unquoted variable inside a test silently breaks when it is empty or
     splits into several words. */
  if (command_literal == "[" || command_literal == "test" ||
      command_literal == "[[")
  {
    for (usize i = 1; i < m_args.size(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      const Word &word =
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
  if (m_local_vars) {
    for (usize i = 1; i < m_args.size(); i++) {
      if (m_args[i]->kind() != Token::Kind::Word) continue;
      const Word &word =
          static_cast<const tokens::WordToken *>(m_args[i])->word();
      for (const WordSegment &segment : word.segments) {
        if (segment.kind == WordSegment::Kind::VariableReference &&
            m_local_vars->find(segment.text) != m_local_vars->end())
        {
          actx.warn(m_args[i]->source_location(),
                    "The assignment prefix does not affect this command, '" +
                        segment.text + "' is read before it is set");
          break;
        }
      }
    }
  }

  if (name && !command_resolves(*name) &&
      actx.defined_functions.find(*name) == actx.defined_functions.end())
  {
    std::string message = "Command '" + *name + "' was not found";
    /* Point at the command word, not at the whole command. With an assignment
       prefix the command location is the assignment, not the program name. A
       command after a dot, source, or eval may be defined at runtime, so it is
       only a warning. */
    if (is_unconditional && !actx.saw_runtime_definer)
      actx.fail(m_args[0]->source_location(), message);
    else
      actx.warn(m_args[0]->source_location(), message);
  }
}

void
Pipeline::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  for (const SimpleCommand *command : m_commands)
    command->analyze(actx, is_unconditional);
}

void
CompoundListCondition::analyze(AnalysisContext &actx,
                               bool is_unconditional) const
{
  m_cmd->analyze(actx, is_unconditional);
}

void
CompoundList::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  for (const CompoundListCondition *node : m_nodes) {
    /* A semicolon or newline node runs whenever the list runs. An && or || node
       runs only depending on the previous command, so it is conditional. */
    bool node_unconditional =
        is_unconditional && node->kind() == CompoundListCondition::Kind::None;
    node->analyze(actx, node_unconditional);
  }
}

void
IfStatement::analyze(AnalysisContext &actx, bool is_unconditional) const
{
  /* The condition always runs to decide the branch. The branches do not. */
  m_condition->analyze(actx, is_unconditional);
  m_then->analyze(actx, false);
  if (m_otherwise != nullptr) m_otherwise->analyze(actx, false);
}

} /* namespace expressions */

} /* namespace shit */
