#pragma once

#include "Common.hpp"

#include <string>
#include <vector>

namespace shit {

struct Token;

namespace tokens {
struct ExpandableIdentifier;
}

struct EvalContext
{
  EvalContext(bool should_disable_path_expansion);

  void add_expansion();
  void add_evaluated_expression();

  void end_command();

  /* Path-expand, tilde-expand and escape. */
  std::vector<std::string> process_args(const std::vector<const Token *> &args);

  std::string make_stats_string() const;

  usize last_expressions_executed() const;
  usize total_expressions_executed() const;

  usize last_expansion_count() const;
  usize total_expansion_count() const;

protected:
  bool m_enable_path_expansion;

  std::vector<std::string> expand_path_once(std::string_view r,
                                            bool should_count_files);
  std::vector<std::string>
  expand_path_recurse(const std::vector<std::string> &vs);
  std::vector<std::string> expand_path(std::string &&r);

  void expand_tilde(std::string &r);
  void erase_escapes(std::string &r);

  usize m_expressions_executed_last{0};
  usize m_expressions_executed_total{0};
  usize m_expansions_last{0};
  usize m_expansions_total{0};
};

struct Expression
{
  Expression() = default;
  Expression(usize location);

  virtual ~Expression() = default;

  usize source_location() const;
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

  usize m_location{std::string::npos};
};

namespace expressions {

struct If : public Expression
{
  If(usize location, const Expression *condition, const Expression *then,
     const Expression *otherwise);

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
  DummyExpression(usize location);

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Command : public Expression
{
  Command(usize location);

  void make_async();
  bool is_async() const;

  virtual void append_to(usize d, std::string &f, bool duplicate) = 0;
  virtual void redirect_to(usize d, std::string &f, bool duplicate) = 0;

protected:
  bool m_is_async{false};
};

struct SimpleCommand : public Command
{
  SimpleCommand(usize location, const std::vector<const Token *> &&args);
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

  CompoundListCondition(usize location, Kind kind, const Command *expr);
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
  CompoundList(usize location);
  CompoundList(usize                                             location,
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
  Pipeline(usize location, const std::vector<const SimpleCommand *> &commands);
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

  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const i64 m_value;
};

struct ConstantString : public Expression
{
  ConstantString(usize location, const std::string &value);
  ~ConstantString() override;

  std::string to_ast_string(usize layer = 0) const override;
  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const std::string m_value;
};

struct Negate : public UnaryExpression
{
  Negate(usize location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Unnegate : public UnaryExpression
{
  Unnegate(usize location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LogicalNot : public UnaryExpression
{
  LogicalNot(usize location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct BinaryComplement : public UnaryExpression
{
  BinaryComplement(usize location, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Add : public BinaryExpression
{
  Add(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Subtract : public BinaryExpression
{
  Subtract(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Multiply : public BinaryExpression
{
  Multiply(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Divide : public BinaryExpression
{
  Divide(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Module : public BinaryExpression
{
  Module(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct BinaryAnd : public BinaryExpression
{
  BinaryAnd(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LogicalAnd : public BinaryExpression
{
  LogicalAnd(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct GreaterThan : public BinaryExpression
{
  GreaterThan(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct GreaterOrEqual : public BinaryExpression
{
  GreaterOrEqual(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct RightShift : public BinaryExpression
{
  RightShift(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LessThan : public BinaryExpression
{
  LessThan(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LessOrEqual : public BinaryExpression
{
  LessOrEqual(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LeftShift : public BinaryExpression
{
  LeftShift(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct BinaryOr : public BinaryExpression
{
  BinaryOr(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct LogicalOr : public BinaryExpression
{
  LogicalOr(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Xor : public BinaryExpression
{
  Xor(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct Equal : public BinaryExpression
{
  Equal(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

struct NotEqual : public BinaryExpression
{
  NotEqual(usize location, const Expression *lhs, const Expression *rhs);

  std::string to_string() const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;
};

} /* namespace expressions */

} /* namespace shit */
