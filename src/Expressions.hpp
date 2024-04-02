#pragma once

#include "Common.hpp"
#include "Errors.hpp"

#include <string>

#define EXPRESSION_AST_INDENT " "

struct Expression
{
  Expression(usize location);
  virtual ~Expression();

  virtual usize location() const;

  virtual i64         evaluate() const = 0;
  virtual std::string to_string() const = 0;
  virtual std::string to_ast_string(usize layer = 0) const = 0;

protected:
  usize m_location;
};

struct UnaryExpression : public Expression
{
  UnaryExpression(usize location, const Expression *rhs);
  virtual ~UnaryExpression();

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_rhs;
};

struct BinaryExpression : public Expression
{
  BinaryExpression(usize location, const Expression *lhs,
                   const Expression *rhs);
  virtual ~BinaryExpression();

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_lhs;
  const Expression *m_rhs;
};

struct Constant : public Expression
{
  Constant(usize location, i64 value);
  ~Constant();

  i64         evaluate() const override;
  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  const i64 m_value;
};

struct Negate : public UnaryExpression
{
  Negate(usize location, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Unnegate : public UnaryExpression
{
  Unnegate(usize location, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LogicalNot : public UnaryExpression
{
  LogicalNot(usize location, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct BinaryComplement : public UnaryExpression
{
  BinaryComplement(usize location, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Add : public BinaryExpression
{
  Add(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Subtract : public BinaryExpression
{
  Subtract(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Multiply : public BinaryExpression
{
  Multiply(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Divide : public BinaryExpression
{
  Divide(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Module : public BinaryExpression
{
  Module(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct BinaryAnd : public BinaryExpression
{
  BinaryAnd(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LogicalAnd : public BinaryExpression
{
  LogicalAnd(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct GreaterThan : public BinaryExpression
{
  GreaterThan(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct GreaterOrEqual : public BinaryExpression
{
  GreaterOrEqual(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct RightShift : public BinaryExpression
{
  RightShift(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LessThan : public BinaryExpression
{
  LessThan(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LessOrEqual : public BinaryExpression
{
  LessOrEqual(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LeftShift : public BinaryExpression
{
  LeftShift(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct BinaryOr : public BinaryExpression
{
  BinaryOr(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LogicalOr : public BinaryExpression
{
  LogicalOr(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Xor : public BinaryExpression
{
  Xor(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Equal : public BinaryExpression
{
  Equal(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct NotEqual : public BinaryExpression
{
  NotEqual(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};