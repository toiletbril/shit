#pragma once

#include "Common.hpp"

#include <string>
#include <vector>

namespace shit {

struct Expression
{
  Expression() = default;
  Expression(usize location);

  virtual ~Expression() = default;

  virtual usize location() const;

  /* Each expression should provide it's own way to copy it. */
  Expression(const Expression &) = delete;
  Expression(Expression &&) noexcept = delete;
  Expression &operator=(const Expression &) = delete;
  Expression &operator=(Expression &&) noexcept = delete;

  virtual i64         evaluate() const = 0;
  virtual std::string to_string() const = 0;
  virtual std::string to_ast_string(usize layer = 0) const;

protected:
  usize m_location{std::string::npos};
};

struct If : public Expression
{
  If(usize location, const Expression *condition, const Expression *then,
     const Expression *otherwise);

  ~If() override;

  i64         evaluate() const override;
  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_condition;
  const Expression *m_then;
  const Expression *m_otherwise;
};

struct DummyExpression : public Expression
{
  DummyExpression(usize location);

  i64         evaluate() const override;
  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
};

struct Exec : public Expression
{
  Exec(usize location, const std::string &path,
       const std::vector<std::string> &args);

  std::string              program() const;
  std::vector<std::string> args() const;

  i64         evaluate() const override;
  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

protected:
  std::string              m_program;
  std::vector<std::string> m_args;
};

struct SequenceNode : public Expression
{
  /* Does this sequence node need evaluation? */
  enum class Kind : uint8_t
  {
    Simple,
    And,
    Or,
  };

  SequenceNode(usize location, Kind kind, const Expression *expr);
  ~SequenceNode() override;

  Kind kind() const;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  i64         evaluate() const override;

protected:
  Kind              m_kind;
  const Expression *m_expr;
};

struct Sequence : public Expression
{
  Sequence(usize location);
  Sequence(usize location, const std::vector<const SequenceNode *> &nodes);

  ~Sequence() override;

  bool empty() const;
  void append_node(const SequenceNode *node);

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  i64         evaluate() const override;

protected:
  std::vector<const SequenceNode *> m_nodes;
};

struct ExecPipeSequence : public Expression
{
  ExecPipeSequence(usize location, const std::vector<const Exec *> &commands);
  ~ExecPipeSequence() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  i64         evaluate() const override;

protected:
  std::vector<const Exec *> m_commands;
};

struct UnaryExpression : public Expression
{
  UnaryExpression(usize location, const Expression *rhs);
  ~UnaryExpression() override;

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_rhs;
};

struct BinaryExpression : public Expression
{
  BinaryExpression(usize location, const Expression *lhs,
                   const Expression *rhs);
  ~BinaryExpression() override;

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_lhs;
  const Expression *m_rhs;
};

struct ConstantNumber : public Expression
{
  ConstantNumber(usize location, i64 value);
  ~ConstantNumber() override;

  i64         evaluate() const override;
  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  const i64 m_value;
};

struct ConstantString : public Expression
{
  ConstantString(usize location, const std::string &value);
  ~ConstantString() override;

  i64         evaluate() const override;
  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  const std::string m_value;
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

} /* namespace shit */
