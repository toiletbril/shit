#pragma once

#include "types.hpp"

#include <string>

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
  evaluate() const
  {
    return m_value;
  }

  std::string
  to_ast_string([[maybe_unused]] usize layer = 0) const
  {
    return "[constant " + to_string() + "]";
  }

  std::string
  to_string() const
  {
    return std::to_string(m_value);
  }

protected:
  const i64 m_value;
};

struct Operator : public Expression
{
  Operator(Expression *lhs, Expression *rhs) : m_lhs(lhs), m_rhs(rhs) {}

  std::string
  to_ast_string(usize layer = 0) const
  {
    std::string s;
    std::string pad;
    for (usize i = 0; i < layer; i++)
      pad += " ";
    s += pad + "[operator " + to_string() + "] -- " +
         m_lhs->to_ast_string(layer + 1) + "\n" + pad + "  \\\n" +
         m_rhs->to_ast_string(layer + 1);
    return s;
  }

  virtual ~Operator()
  {
    delete m_lhs;
    delete m_rhs;
  };

protected:
  const Expression *m_lhs{};
  const Expression *m_rhs{};
};

struct Add : public Operator
{
  Add(Expression *lhs, Expression *rhs) : Operator(lhs, rhs) {}

  std::string
  to_string() const
  {
    return "+";
  }

  i64
  evaluate() const
  {
    return this->m_lhs->evaluate() + this->m_rhs->evaluate();
  }
};

struct Subtract : public Operator
{
  Subtract(Expression *lhs, Expression *rhs) : Operator(lhs, rhs) {}

  std::string
  to_string() const
  {
    return "-";
  }

  i64
  evaluate() const
  {
    return this->m_lhs->evaluate() - this->m_rhs->evaluate();
  }
};

struct Multiply : public Operator
{
  Multiply(Expression *lhs, Expression *rhs) : Operator(lhs, rhs) {}

  std::string
  to_string() const
  {
    return "*";
  }

  i64
  evaluate() const
  {
    return this->m_lhs->evaluate() * this->m_rhs->evaluate();
  }
};

struct Divide : public Operator
{
  Divide(Expression *lhs, Expression *rhs) : Operator(lhs, rhs) {}

  std::string
  to_string() const
  {
    return "/";
  }

  i64
  evaluate() const
  {
    return this->m_lhs->evaluate() / this->m_rhs->evaluate();
  }
};

struct Module : public Operator
{
  Module(Expression *lhs, Expression *rhs) : Operator(lhs, rhs) {}

  std::string
  to_string() const
  {
    return "%";
  }

  i64
  evaluate() const
  {
    return this->m_lhs->evaluate() % this->m_rhs->evaluate();
  }
};
