#pragma once

#include "types.hpp"

#include <string>

#define INDENT " "

struct Constant;

struct Expression
{
  virtual i64 evaluate() const = 0;
  virtual ~Expression(){};
  virtual std::string to_string() const                    = 0;
  virtual std::string to_ast_string(usize layer = 0) const = 0;
};

struct Constant : public Expression
{
  Constant(i64 value) : m_value(value) {}
  ~Constant(){};

  i64
  evaluate() const override
  {
    return m_value;
  }

  std::string
  to_ast_string(usize layer = 0) const override
  {
    std::string s;
    std::string pad;
    for (usize i = 0; i < layer; i++)
      pad += INDENT;
    s += pad + "[Const " + to_string() + "]";
    return s;
  }

  std::string
  to_string() const override
  {
    return std::to_string(m_value);
  }

protected:
  const i64 m_value;
};

struct UnaryExpression : public Expression
{
  UnaryExpression(const Expression *rhs) : m_rhs(rhs) {}
  virtual ~UnaryExpression() { delete m_rhs; };

  std::string
  to_ast_string(usize layer = 0) const override
  {
    std::string s;
    std::string pad;
    for (usize i = 0; i < layer; i++)
      pad += INDENT;
    s += pad + "[Unary " + to_string() + "]\n";
    s += pad + INDENT + m_rhs->to_ast_string(layer + 1);
    return s;
  }

protected:
  const Expression *m_rhs{};
};

struct Negate : public UnaryExpression
{
  Negate(const Expression *rhs) : UnaryExpression(rhs) {}

  std::string
  to_string() const override
  {
    return "-";
  }

  i64
  evaluate() const override
  {
    return -this->m_rhs->evaluate();
  }
};

struct Unnegate : public UnaryExpression
{
  Unnegate(const Expression *rhs) : UnaryExpression(rhs) {}

  std::string
  to_string() const override
  {
    return "+";
  }

  i64
  evaluate() const override
  {
    return +this->m_rhs->evaluate();
  }
};

struct BinaryComplement : public UnaryExpression
{
  BinaryComplement(const Expression *rhs) : UnaryExpression(rhs) {}

  std::string
  to_string() const override
  {
    return "~";
  }

  i64
  evaluate() const override
  {
    return ~this->m_rhs->evaluate();
  }
};

struct BinaryExpression : public Expression
{
  BinaryExpression(const Expression *lhs, const Expression *rhs)
      : m_lhs(lhs), m_rhs(rhs)
  {
  }
  virtual ~BinaryExpression()
  {
    delete m_lhs;
    delete m_rhs;
  };

  std::string
  to_ast_string(usize layer = 0) const override
  {
    std::string s;
    std::string pad;
    for (usize i = 0; i < layer; i++)
      pad += INDENT;
    s += pad + "[Binary " + to_string() + "]\n";
    s += pad + INDENT + m_lhs->to_ast_string(layer + 1) + "\n";
    s += pad + INDENT + m_rhs->to_ast_string(layer + 1);
    return s;
  }

protected:
  const Expression *m_lhs{};
  const Expression *m_rhs{};
};

struct Add : public BinaryExpression
{
  Add(const Expression *lhs, const Expression *rhs) : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "+";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() + this->m_rhs->evaluate();
  }
};

struct Subtract : public BinaryExpression
{
  Subtract(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "-";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() - this->m_rhs->evaluate();
  }
};

struct Multiply : public BinaryExpression
{
  Multiply(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "*";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() * this->m_rhs->evaluate();
  }
};

struct Divide : public BinaryExpression
{
  Divide(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "/";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() / this->m_rhs->evaluate();
  }
};

struct Module : public BinaryExpression
{
  Module(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "%";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() % this->m_rhs->evaluate();
  }
};

struct BinaryAnd : public BinaryExpression
{
  BinaryAnd(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "&";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() & this->m_rhs->evaluate();
  }
};

struct LogicalAnd : public BinaryExpression
{
  LogicalAnd(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "&&";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() && this->m_rhs->evaluate();
  }
};

struct GreaterThan : public BinaryExpression
{
  GreaterThan(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return ">";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() > this->m_rhs->evaluate();
  }
};

struct GreaterOrEqualTo : public BinaryExpression
{
  GreaterOrEqualTo(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return ">=";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() >= this->m_rhs->evaluate();
  }
};

struct RightShift : public BinaryExpression
{
  RightShift(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return ">>";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() >> this->m_rhs->evaluate();
  }
};

struct LessThan : public BinaryExpression
{
  LessThan(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "<";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() < this->m_rhs->evaluate();
  }
};

struct LessOrEqualTo : public BinaryExpression
{
  LessOrEqualTo(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "<=";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() <= this->m_rhs->evaluate();
  }
};

struct LeftShift : public BinaryExpression
{
  LeftShift(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "<<";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() << this->m_rhs->evaluate();
  }
};

struct BinaryOr : public BinaryExpression
{
  BinaryOr(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "|";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() | this->m_rhs->evaluate();
  }
};

struct LogicalOr : public BinaryExpression
{
  LogicalOr(const Expression *lhs, const Expression *rhs)
      : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "||";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() || this->m_rhs->evaluate();
  }
};

struct Xor : public BinaryExpression
{
  Xor(const Expression *lhs, const Expression *rhs) : BinaryExpression(lhs, rhs)
  {
  }

  std::string
  to_string() const override
  {
    return "^";
  }

  i64
  evaluate() const override
  {
    return this->m_lhs->evaluate() ^ this->m_rhs->evaluate();
  }
};
