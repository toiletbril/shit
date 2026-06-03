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
  cxt.set_shell_variable(m_assignment->key(), cxt.expand_word_for_assignment(
                                                  m_assignment->value_word()));
  cxt.set_last_exit_status(0);
  return 0;
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
}

const std::vector<const Token *> &
SimpleCommand::args() const
{
  return m_args;
}

i64
SimpleCommand::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_args.size() > 0);

  if (cxt.should_echo())
    std::cout << utils::merge_tokens_to_string(m_args) << std::endl;

  std::vector<std::string> program_args = cxt.process_args(m_args);

  /* An expansion may drop every word, for example an unset $x used as the whole
     command. There is nothing to run then. */
  if (program_args.empty()) {
    cxt.set_last_exit_status(0);
    return 0;
  }

  if (cxt.shell_is_interactive())
    toiletline::set_title(utils::merge_args_to_string(program_args));

  /* Per-command assignments apply to the environment around this command, so
     the child inherits them. The previous values are restored afterwards. */
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_env{};
  if (m_local_vars) {
    for (const auto &[name, value_word] : *m_local_vars) {
      saved_env.emplace_back(name, os::get_environment_variable(name));
      os::set_environment_variable(name,
                                   cxt.expand_word_for_assignment(value_word));
    }
  }

  i64 ret = utils::execute_context(
      ExecContext::make_from(source_location(), program_args), cxt, is_async());

  for (const auto &[name, old_value] : saved_env) {
    if (old_value)
      os::set_environment_variable(name, *old_value);
    else
      os::unset_environment_variable(name);
  }

  cxt.set_last_exit_status(static_cast<i32>(ret));
  return ret;
}

std::string
SimpleCommand::to_string() const
{
  std::string args{};
  std::string s = "SimpleCommand \"" + m_args[0]->raw_string() + "\"";

  if (!m_args.empty()) {
    for (usize i = 1; i < m_args.size(); i++) {
      args += " \"";
      args += m_args[i]->raw_string();
      args += "\"";
    }
    s += args;
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
  return m_cmd->evaluate(cxt);
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
    ecs.emplace_back(ExecContext::make_from(e->source_location(),
                                            cxt.process_args(e->args())));
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

  if (name && !command_resolves(*name)) {
    std::string message = "Command '" + *name + "' was not found";
    /* Point at the command word, not at the whole command. With an assignment
       prefix the command location is the assignment, not the program name. */
    if (is_unconditional)
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
