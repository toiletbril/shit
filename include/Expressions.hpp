#pragma once

#include "Common.hpp"
#include "Errors.hpp"

#include <string>

#define EXPRESSION_AST_INDENT " "

struct Expression
{
  virtual ~Expression() {}
  virtual i64         evaluate() const                     = 0;
  virtual std::string to_string() const                    = 0;
  virtual std::string to_ast_string(usize layer = 0) const = 0;
};

struct UnaryExpression : public Expression
{
  UnaryExpression(const Expression *rhs);
  virtual ~UnaryExpression();

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_rhs;
};

struct BinaryExpression : public Expression
{
  BinaryExpression(const Expression *lhs, const Expression *rhs);
  virtual ~BinaryExpression();

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_lhs;
  const Expression *m_rhs;
};

struct Constant : public Expression
{
  Constant(i64 value);
  ~Constant();

  i64         evaluate() const override;
  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  const i64 m_value;
};

struct Negate : public UnaryExpression
{
  Negate(const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Unnegate : public UnaryExpression
{
  Unnegate(const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct BinaryComplement : public UnaryExpression
{
  BinaryComplement(const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Add : public BinaryExpression
{
  Add(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Subtract : public BinaryExpression
{
  Subtract(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Multiply : public BinaryExpression
{
  Multiply(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Divide : public BinaryExpression
{
  Divide(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Module : public BinaryExpression
{
  Module(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct BinaryAnd : public BinaryExpression
{
  BinaryAnd(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LogicalAnd : public BinaryExpression
{
  LogicalAnd(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct GreaterThan : public BinaryExpression
{
  GreaterThan(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct GreaterOrEqual : public BinaryExpression
{
  GreaterOrEqual(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct RightShift : public BinaryExpression
{
  RightShift(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LessThan : public BinaryExpression
{
  LessThan(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LessOrEqual : public BinaryExpression
{
  LessOrEqual(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LeftShift : public BinaryExpression
{
  LeftShift(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct BinaryOr : public BinaryExpression
{
  BinaryOr(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct LogicalOr : public BinaryExpression
{
  LogicalOr(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Xor : public BinaryExpression
{
  Xor(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};

struct Equality : public BinaryExpression
{
  Equality(const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;
  i64         evaluate() const override;
};
