#pragma once

#include "Common.hpp"
#include "Eval.hpp"

#include <string>
#include <vector>

namespace shit {

struct Token;

struct Expression
{
  Expression() = delete;
  Expression(SourceLocation location);

  virtual ~Expression() = default;

  SourceLocation source_location() const;
  /* Expressions should override evaluate_impl() instead. This method is used
   * mainly for initialization before the actual evaluation. */
  i64 evaluate(EvalContext &cxt) const;

  /* Each expression should provide it's own way to copy it. */
  Expression(const Expression &) = delete;
  Expression(Expression &&) noexcept = delete;
  Expression &operator=(const Expression &) = delete;
  Expression &operator=(Expression &&) noexcept = delete;

  virtual std::string to_string() const = 0;
  virtual std::string to_ast_string(usize layer = 0) const;

protected:
  virtual i64 evaluate_impl(EvalContext &cxt) const = 0;

  SourceLocation m_location;
};

namespace expressions {

struct If : public Expression
{
  If(SourceLocation location, const Expression *condition,
     const Expression *then, const Expression *otherwise);

  ~If() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const Expression *m_condition;
  const Expression *m_then;
  const Expression *m_otherwise;
};

struct DummyExpression : public Expression
{
  DummyExpression(SourceLocation location);

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Command : public Expression
{
  Command(SourceLocation location);

  void make_async();
  bool is_async() const;

  virtual void append_to(usize d, std::string &f, bool duplicate) = 0;
  virtual void redirect_to(usize d, std::string &f, bool duplicate) = 0;

protected:
  bool m_is_async{false};
};

struct SimpleCommand : public Command
{
  SimpleCommand(SourceLocation                     location,
                const std::vector<const Token *> &&args);
  ~SimpleCommand() override;

  const std::vector<const Token *> &args() const;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

  void append_to(usize d, std::string &f, bool duplicate) override;
  void redirect_to(usize d, std::string &f, bool duplicate) override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  std::vector<const Token *> m_args;
};

struct CompoundListCondition : public Expression
{
  enum class Kind : uint8_t
  {
    None,
    And,
    Or,
  };

  CompoundListCondition(SourceLocation location, Kind kind,
                        const Command *expr);
  ~CompoundListCondition() override;

  Kind kind() const;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  Kind           m_kind;
  const Command *m_cmd;
};

struct CompoundList : public Expression
{
  CompoundList();
  CompoundList(SourceLocation                                    location,
               const std::vector<const CompoundListCondition *> &nodes);

  ~CompoundList() override;

  bool empty() const;
  void append_node(const CompoundListCondition *node);

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  std::vector<const CompoundListCondition *> m_nodes;
};

struct Pipeline : public Command
{
  Pipeline(SourceLocation                            location,
           const std::vector<const SimpleCommand *> &commands);
  ~Pipeline() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

  void append_to(usize d, std::string &f, bool duplicate) override;
  void redirect_to(usize d, std::string &f, bool duplicate) override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  std::vector<const SimpleCommand *> m_commands;
};

struct UnaryExpression : public Expression
{
  UnaryExpression(SourceLocation location, const Expression *rhs);
  ~UnaryExpression() override;

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_rhs;
};

struct BinaryExpression : public Expression
{
  BinaryExpression(SourceLocation location, const Expression *lhs,
                   const Expression *rhs);
  ~BinaryExpression() override;

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_lhs;
  const Expression *m_rhs;
};

struct ConstantNumber : public Expression
{
  ConstantNumber(SourceLocation location, i64 value);
  ~ConstantNumber() override;

  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const i64 m_value;
};

struct ConstantString : public Expression
{
  ConstantString(SourceLocation location, const std::string &value);
  ~ConstantString() override;

  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const std::string m_value;
};

struct Negate : public UnaryExpression
{
  Negate(SourceLocation location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Unnegate : public UnaryExpression
{
  Unnegate(SourceLocation location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LogicalNot : public UnaryExpression
{
  LogicalNot(SourceLocation location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct BinaryComplement : public UnaryExpression
{
  BinaryComplement(SourceLocation location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Add : public BinaryExpression
{
  Add(SourceLocation location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Subtract : public BinaryExpression
{
  Subtract(SourceLocation location, const Expression *lhs,
           const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Multiply : public BinaryExpression
{
  Multiply(SourceLocation location, const Expression *lhs,
           const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Divide : public BinaryExpression
{
  Divide(SourceLocation location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Module : public BinaryExpression
{
  Module(SourceLocation location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct BinaryAnd : public BinaryExpression
{
  BinaryAnd(SourceLocation location, const Expression *lhs,
            const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LogicalAnd : public BinaryExpression
{
  LogicalAnd(SourceLocation location, const Expression *lhs,
             const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct GreaterThan : public BinaryExpression
{
  GreaterThan(SourceLocation location, const Expression *lhs,
              const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct GreaterOrEqual : public BinaryExpression
{
  GreaterOrEqual(SourceLocation location, const Expression *lhs,
                 const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct RightShift : public BinaryExpression
{
  RightShift(SourceLocation location, const Expression *lhs,
             const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LessThan : public BinaryExpression
{
  LessThan(SourceLocation location, const Expression *lhs,
           const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LessOrEqual : public BinaryExpression
{
  LessOrEqual(SourceLocation location, const Expression *lhs,
              const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LeftShift : public BinaryExpression
{
  LeftShift(SourceLocation location, const Expression *lhs,
            const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct BinaryOr : public BinaryExpression
{
  BinaryOr(SourceLocation location, const Expression *lhs,
           const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LogicalOr : public BinaryExpression
{
  LogicalOr(SourceLocation location, const Expression *lhs,
            const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Xor : public BinaryExpression
{
  Xor(SourceLocation location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Equal : public BinaryExpression
{
  Equal(SourceLocation location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct NotEqual : public BinaryExpression
{
  NotEqual(SourceLocation location, const Expression *lhs,
           const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

} /* namespace expressions */

} /* namespace shit */
