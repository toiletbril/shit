#include "Expressions.hpp"

#include "Builtin.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Utils.hpp"

namespace shit {

static constexpr const char *EXPRESSION_AST_INDENT = " ";

/**
 * class: Expression
 */
Expression::Expression() = default;

Expression::Expression(usize location) : m_location(location) {}

Expression::~Expression() = default;

usize
Expression::location() const
{
  return m_location;
}

std::string
Expression::to_ast_string(usize layer) const
{
  std::string pad;
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

/**
 * class: If
 */
If::If(usize location, const Expression *condition, const Expression *then,
       const Expression *otherwise)
    : Expression(location), m_condition(condition), m_then(then),
      m_otherwise(otherwise)
{}

If::~If()
{
  delete m_condition;
  delete m_then;

  if (m_otherwise != nullptr) {
    delete m_otherwise;
  }
}

i64
If::evaluate() const
{
  if (m_condition->evaluate())
    return m_then->evaluate();
  else if (m_otherwise != nullptr)
    return m_otherwise->evaluate();

  return 0;
}

std::string
If::to_string() const
{
  return "If";
}

std::string
If::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;

  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }

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

/**
 * class: DummyExpression
 */
DummyExpression::DummyExpression(usize location) : Expression(location) {}

i64
DummyExpression::evaluate() const
{
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
  std::string pad;
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

/**
 * class: Exec
 */
Exec::Exec(usize location, const std::string &path,
           const std::vector<std::string> &args)
    : Expression(location), m_program(path), m_args(args)
{}

std::string
Exec::program() const
{
  return m_program;
}

std::vector<std::string>
Exec::args() const
{
  return m_args;
}

i64
Exec::evaluate() const
{
  std::optional<std::filesystem::path> program_path;

  /* This isn't a path? */
  if (m_program.find('/') == std::string::npos) {
    Builtin::Kind bk = search_builtin(m_program);

    /* Is this a builtin? */
    if (bk != Builtin::Kind::Invalid) {
      try {
        return execute_builtin(bk, utils::simple_shell_expand_args(m_args));
      } catch (Error &err) {
        throw ErrorWithLocation{location(), err.message()};
      }
    }

    /* Not a builtin, try to search PATH. */
    program_path = utils::search_program_path(m_program);
  } else {
    /* This is a path. */
    /* TODO: Sanitize extensions here too. */
    program_path = utils::canonicalize_path(m_program);
  }

  if (!program_path) {
    throw ErrorWithLocation{location(), "Command not found"};
  }

  try {
    return utils::execute_program({program_path.value(), m_program,
                                   utils::simple_shell_expand_args(m_args)});
  } catch (Error &err) {
    throw ErrorWithLocation{location(), err.message()};
  }

  SHIT_UNREACHABLE();
}

std::string
Exec::to_string() const
{
  std::string args;
  std::string s = "Exec \"" + m_program;
  if (!m_args.empty()) {
    for (std::string_view arg : m_args) {
      args += " ";
      args += arg;
    }
    s += args;
  }
  s += "\"";
  return s;
}

std::string
Exec::to_ast_string(usize layer) const
{
  SHIT_UNUSED(layer);
  std::string pad;
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

/**
 * class: Sequence
 */
Sequence::Sequence(usize                                    location,
                   const std::vector<const SequenceNode *> &nodes)
    : Expression(location), m_nodes(nodes)
{}

Sequence::~Sequence()
{
  for (const SequenceNode *e : m_nodes) {
    delete e;
  }
}

std::string
Sequence::to_string() const
{
  return "Sequence";
}

std::string
Sequence::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;

  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  s += pad + "[Sequence]";
  for (const SequenceNode *n : m_nodes) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + n->to_ast_string(layer + 1);
  }

  return s;
}

i64
Sequence::evaluate() const
{
  SHIT_ASSERT(m_nodes.size() > 0);

  static constexpr i64 nothing_was_executed = -256;
  i64                  ret = nothing_was_executed;

  for (const SequenceNode *n : m_nodes) {
    switch (n->kind()) {
    case SequenceNode::Kind::Simple: {
      ret = n->evaluate();
    } break;

    case SequenceNode::Kind::Or:
      if (ret != 0) {
        ret = n->evaluate();
      }
      break;

    case SequenceNode::Kind::And:
      if (ret == 0) {
        ret = n->evaluate();
      }
      break;
    }
  }

  SHIT_ASSERT(ret != nothing_was_executed);

  return ret;
}

/**
 * class: SequenceNode
 */
SequenceNode::SequenceNode(usize location, Kind kind, const Expression *expr)
    : Expression(location), m_kind(kind), m_expr(expr)
{}

SequenceNode::~SequenceNode() { delete m_expr; }

SequenceNode::Kind
SequenceNode::kind() const
{
  return m_kind;
}

std::string
SequenceNode::to_string() const
{
  std::string k;
  switch (kind()) {
  case Kind::Simple: k = "Simple"; break;
  case Kind::And: k = "And"; break;
  case Kind::Or: k = "Or"; break;
  default: SHIT_UNREACHABLE();
  }
  return "SequenceNode " + k;
}

std::string
SequenceNode::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;

  s += pad + "[" + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_expr->to_ast_string(layer + 1);

  return s;
}

i64
SequenceNode::evaluate() const
{
  return m_expr->evaluate();
}

ExecPipeSequence::ExecPipeSequence(usize                            location,
                                   const std::vector<const Exec *> &commands)
    : Expression(location), m_commands(commands)
{}

ExecPipeSequence::~ExecPipeSequence()
{
  for (const Exec *e : m_commands) {
    delete e;
  }
}

std::string
ExecPipeSequence::to_string() const
{
  return "ExecPipeSequence";
}

std::string
ExecPipeSequence::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;

  s += pad + "[ExecPipeSequence]";
  for (const Exec *e : m_commands) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + e->to_ast_string(layer + 1);
  }

  return s;
}

i64
ExecPipeSequence::evaluate() const
{
  SHIT_ASSERT(m_commands.size() > 1);

  std::vector<utils::ExecContext> ecs;

  for (const Exec *e : m_commands) {
    std::optional<std::filesystem::path> program_path = e->program();

    /* TODO: Support builtins for pipes. */
    if (e->program().find('/') == std::string::npos) {
      program_path = utils::search_program_path(e->program());
    } else {
      /* TODO: Sanitize extensions here too. */
      program_path = utils::canonicalize_path(e->program());
    }

    if (!program_path) {
      throw ErrorWithLocation{e->location(), "Command not found"};
    }

    ecs.push_back({*program_path, e->program(), e->args()});
  }

  i64 ret = utils::execute_program_sequence_with_pipes(ecs);

  return ret;
}

/**
 * class: UnaryExpression
 */
UnaryExpression::UnaryExpression(usize location, const Expression *rhs)
    : Expression(location), m_rhs(rhs)
{}

UnaryExpression::~UnaryExpression() { delete m_rhs; }

std::string
UnaryExpression::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  s += pad + "[Unary " + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_rhs->to_ast_string(layer + 1);
  return s;
}

/**
 * class: BinaryExpression
 */
BinaryExpression::BinaryExpression(usize location, const Expression *lhs,
                                   const Expression *rhs)
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
  std::string s;
  std::string pad;
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  s += pad + "[Binary " + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_lhs->to_ast_string(layer + 1) + "\n";
  s += pad + EXPRESSION_AST_INDENT + m_rhs->to_ast_string(layer + 1);
  return s;
}

/**
 * class: ConstantNumber
 */
ConstantNumber::ConstantNumber(usize location, i64 value)
    : Expression(location), m_value(value)
{}

ConstantNumber::~ConstantNumber() = default;

i64
ConstantNumber::evaluate() const
{
  return m_value;
}

std::string
ConstantNumber::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;
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

/**
 * class: ConstantString
 */
ConstantString::ConstantString(usize location, const std::string &value)
    : Expression(location), m_value(value)
{}

ConstantString::~ConstantString() = default;

i64
ConstantString::evaluate() const
{
  return 0;
}

std::string
ConstantString::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;
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

/**
 * class: Negate
 */
Negate::Negate(usize location, const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
Negate::to_string() const
{
  return "-";
}

i64
Negate::evaluate() const
{
  return -m_rhs->evaluate();
}

/**
 * class: Unnegate
 */
Unnegate::Unnegate(usize location, const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
Unnegate::to_string() const
{
  return "+";
}

i64
Unnegate::evaluate() const
{
  return +m_rhs->evaluate();
}

/**
 * class: LogicalNot
 */
LogicalNot::LogicalNot(usize location, const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
LogicalNot::to_string() const
{
  return "!";
}

i64
LogicalNot::evaluate() const
{
  return !m_rhs->evaluate();
}

/**
 * class: BinaryComplement
 */
BinaryComplement::BinaryComplement(usize location, const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
BinaryComplement::to_string() const
{
  return "~";
}

i64
BinaryComplement::evaluate() const
{
  return ~m_rhs->evaluate();
}

/**
 * class: Add
 */
Add::Add(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Add::to_string() const
{
  return "+";
}

i64
Add::evaluate() const
{
  return m_lhs->evaluate() + m_rhs->evaluate();
}

/**
 * class: Subtract
 */
Subtract::Subtract(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Subtract::to_string() const
{
  return "-";
}

i64
Subtract::evaluate() const
{
  return m_lhs->evaluate() - m_rhs->evaluate();
}

/**
 * class: Multiply
 */
Multiply::Multiply(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Multiply::to_string() const
{
  return "*";
}

i64
Multiply::evaluate() const
{
  return m_lhs->evaluate() * m_rhs->evaluate();
}

/**
 * class: Divide
 */
Divide::Divide(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Divide::to_string() const
{
  return "/";
}

i64
Divide::evaluate() const
{
  i64 denom = m_rhs->evaluate();
  if (denom == 0)
    throw ErrorWithLocation{m_rhs->location(), "Division by 0"};
  return m_lhs->evaluate() / denom;
}

/**
 * class: Module
 */
Module::Module(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Module::to_string() const
{
  return "%";
}

i64
Module::evaluate() const
{
  return m_lhs->evaluate() % m_rhs->evaluate();
}

/**
 * class: BinaryAnd
 */
BinaryAnd::BinaryAnd(usize location, const Expression *lhs,
                     const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
BinaryAnd::to_string() const
{
  return "&";
}

i64
BinaryAnd::evaluate() const
{
  return m_lhs->evaluate() & m_rhs->evaluate();
}

/**
 * class: LogicalAnd
 */
LogicalAnd::LogicalAnd(usize location, const Expression *lhs,
                       const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LogicalAnd::to_string() const
{
  return "&&";
}

i64
LogicalAnd::evaluate() const
{
  return m_lhs->evaluate() && m_rhs->evaluate();
}

/**
 * class: GreaterThan
 */
GreaterThan::GreaterThan(usize location, const Expression *lhs,
                         const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
GreaterThan::to_string() const
{
  return ">";
}

i64
GreaterThan::evaluate() const
{
  return m_lhs->evaluate() > m_rhs->evaluate();
}

/**
 * class: GreaterOrEqual
 */
GreaterOrEqual::GreaterOrEqual(usize location, const Expression *lhs,
                               const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
GreaterOrEqual::to_string() const
{
  return ">=";
}

i64
GreaterOrEqual::evaluate() const
{
  return m_lhs->evaluate() >= m_rhs->evaluate();
}

/**
 * class: RightShift
 */
RightShift::RightShift(usize location, const Expression *lhs,
                       const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
RightShift::to_string() const
{
  return ">>";
}

i64
RightShift::evaluate() const
{
  return m_lhs->evaluate() >> m_rhs->evaluate();
}

/**
 * class: LessThan
 */
LessThan::LessThan(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LessThan::to_string() const
{
  return "<";
}

i64
LessThan::evaluate() const
{
  return m_lhs->evaluate() < m_rhs->evaluate();
}

/**
 * class: LessOrEqual
 */
LessOrEqual::LessOrEqual(usize location, const Expression *lhs,
                         const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LessOrEqual::to_string() const
{
  return "<=";
}

i64
LessOrEqual::evaluate() const
{
  return m_lhs->evaluate() <= m_rhs->evaluate();
}

/**
 * class: LeftShift
 */
LeftShift::LeftShift(usize location, const Expression *lhs,
                     const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LeftShift::to_string() const
{
  return "<<";
}

i64
LeftShift::evaluate() const
{
  return m_lhs->evaluate() << m_rhs->evaluate();
}

/**
 * class: BinaryOr
 */
BinaryOr::BinaryOr(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
BinaryOr::to_string() const
{
  return "|";
}

i64
BinaryOr::evaluate() const
{
  return m_lhs->evaluate() | m_rhs->evaluate();
}

/**
 * class: LogicalOr
 */
LogicalOr::LogicalOr(usize location, const Expression *lhs,
                     const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LogicalOr::to_string() const
{
  return "||";
}

i64
LogicalOr::evaluate() const
{
  return m_lhs->evaluate() || m_rhs->evaluate();
}

/**
 * class: Xor
 */
Xor::Xor(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Xor::to_string() const
{
  return "^";
}

i64
Xor::evaluate() const
{
  return m_lhs->evaluate() ^ m_rhs->evaluate();
}

/**
 * class: Equality
 */
Equal::Equal(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Equal::to_string() const
{
  return "==";
}

i64
Equal::evaluate() const
{
  return m_lhs->evaluate() == m_rhs->evaluate();
}

/**
 * class: NotEqual
 */
NotEqual::NotEqual(usize location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
NotEqual::to_string() const
{
  return "!=";
}

i64
NotEqual::evaluate() const
{
  return m_lhs->evaluate() != m_rhs->evaluate();
}

} /* namespace shit */
