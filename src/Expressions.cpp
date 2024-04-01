#include "Expressions.hpp"

/* UnaryExpression */
UnaryExpression::UnaryExpression(const Expression *rhs) : m_rhs(rhs) {}

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

/* BinaryExpression */
BinaryExpression::BinaryExpression(const Expression *lhs, const Expression *rhs)
    : m_lhs(lhs), m_rhs(rhs)
{
}

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

/* Constant */
Constant::Constant(i64 value) : m_value(value) {}

Constant::~Constant() {}

i64
Constant::evaluate() const
{
  return m_value;
}

std::string
Constant::to_ast_string(usize layer) const
{
  std::string s;
  std::string pad;
  for (usize i = 0; i < layer; i++)
    pad += EXPRESSION_AST_INDENT;
  s += pad + "[Const " + to_string() + "]";
  return s;
}

std::string
Constant::to_string() const
{
  return std::to_string(m_value);
}

/* Negate */
Negate::Negate(const Expression *rhs) : UnaryExpression(rhs) {}

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

/* Unnegate */
Unnegate::Unnegate(const Expression *rhs) : UnaryExpression(rhs) {}

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

/* BinaryComplement */
BinaryComplement::BinaryComplement(const Expression *rhs) : UnaryExpression(rhs)
{
}

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

/* Add */
Add::Add(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* Subtract */
Subtract::Subtract(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* Multiply */
Multiply::Multiply(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* Divide */
Divide::Divide(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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
    throw std::runtime_error("division by 0");
  return m_lhs->evaluate() / denom;
}

/* Module */
Module::Module(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* BinaryAnd */
BinaryAnd::BinaryAnd(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* LogicalAnd */
LogicalAnd::LogicalAnd(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* GreaterThan */
GreaterThan::GreaterThan(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* GreaterOrEqual */
GreaterOrEqual::GreaterOrEqual(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* RightShift */
RightShift::RightShift(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* LessThan */
LessThan::LessThan(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* LessOrEqual */
LessOrEqual::LessOrEqual(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* LeftShift */
LeftShift::LeftShift(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* BinaryOr */
BinaryOr::BinaryOr(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* LogicalOr */
LogicalOr::LogicalOr(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* Xor */
Xor::Xor(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

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

/* Equality */
Equality::Equality(const Expression *lhs, const Expression *rhs)
    : BinaryExpression(lhs, rhs)
{
}

std::string
Equality::to_string() const
{
  return "==";
}

i64
Equality::evaluate() const
{
  return m_lhs->evaluate() == m_rhs->evaluate();
}
