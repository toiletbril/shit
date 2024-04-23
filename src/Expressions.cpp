#include "Expressions.hpp"

#include "Builtin.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Utils.hpp"

#include <cstring>

namespace shit {

static constexpr const char *EXPRESSION_AST_INDENT = " ";

/**
 * class: Expression
 */
Expression::Expression(usize location) : m_location(location) {}

Expression::~Expression() = default;

usize
Expression::location() const
{
  return m_location;
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
  if (m_otherwise != nullptr)
    delete m_otherwise;
}

i64
If::evaluate() const
{
  bool condition_value = m_condition->evaluate();

  if (condition_value)
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

/**
 * class: DummyExpression
 */
DummyExpression::DummyExpression() : Expression(0) {}

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
  SHIT_UNUSED(layer);
  std::string pad;
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
}

/**
 * class: Exec
 */
Exec::Exec(usize location, std::string path, std::vector<std::string> args)
    : Expression(location), m_path(path), m_args(args)
{}

i64
Exec::evaluate() const
{
  std::optional<std::filesystem::path> program_path;

  /* This isn't a path? */
  if (m_path.find('/') == std::string::npos) {
    /* Is this a builtin? */
    Builtin::Kind bk = search_builtin(m_path);
    if (bk != Builtin::Kind::Invalid) {
      try {
        return execute_builtin(bk, m_args);
      } catch (Error &err) {
        throw ErrorWithLocation{location(), err.message()};
      }
    }
    /* Not a builtin, try to search PATH. */
    program_path = utils::search_program_path(m_path);
  } else {
    /* This is a path. */
    program_path = utils::canonicalize_path(m_path);
  }

  if (!program_path)
    throw ErrorWithLocation{location(), "Command not found"};

  i32 ret = 256;
  try {
    ret = utils::execute_program_by_path(program_path.value(), m_args);
  } catch (Error &err) {
    throw ErrorWithLocation{location(), err.message()};
  }

  return ret;
}

std::string
Exec::to_string() const
{
  std::string args;
  std::string s = "Exec \"" + m_path;
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
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  return pad + "[" + to_string() + "]";
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
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
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
ConstantString::ConstantString(usize location, std::string value)
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
 * class: Inequality
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
