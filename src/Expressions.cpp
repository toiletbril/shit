#include "Expressions.hpp"

#include "Builtin.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

namespace shit {

/**
 * class: Expression
 */
Expression::Expression(SourceLocation location) : m_location(location) {}

SourceLocation
Expression::source_location() const
{
  return m_location;
}

std::string
Expression::to_ast_string(usize layer) const
{
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

i64
Expression::evaluate(EvalContext &cxt) const
{
  cxt.add_evaluated_expression();
  return evaluate_impl(cxt);
}

namespace expressions {

/**
 * class: If
 */
If::If(SourceLocation location, const Expression *condition,
       const Expression *then, const Expression *otherwise)
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
If::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  if (m_condition->evaluate(cxt)) {
    return m_then->evaluate(cxt);
  } else if (m_otherwise != nullptr) {
    return m_otherwise->evaluate(cxt);
  }

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
  std::string s{};
  std::string pad{};

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
 * class: Command
 */
Command::Command(SourceLocation location) : Expression(location) {}

void
Command::make_async()
{
  m_is_async = true;
}

bool
Command::is_async() const
{
  return m_is_async;
}

/**
 * class: DummyExpression
 */
DummyExpression::DummyExpression(SourceLocation location) : Expression(location)
{}

i64
DummyExpression::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
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
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

/**
 * class: SimpleCommand
 */
SimpleCommand::SimpleCommand(SourceLocation                     location,
                             const std::vector<const Token *> &&args)
    : Command(location), m_args(args)
{}

SimpleCommand::~SimpleCommand()
{
  for (const Token *t : m_args) {
    delete t;
  }
}

const std::vector<const Token *> &
SimpleCommand::args() const
{
  return m_args;
}

i64
SimpleCommand::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_args.size() > 0);

  return utils::execute_context(
      utils::ExecContext::make_from(source_location(),
                                    cxt.process_args(m_args)),
      is_async());

  SHIT_UNREACHABLE();
}

std::string
SimpleCommand::to_string() const
{
  std::string args{};
  std::string s = "SimpleCommand \"" + m_args[0]->raw_string();
  if (!m_args.empty()) {
    for (usize i = 1; i < m_args.size(); i++) {
      args += " ";
      args += m_args[i]->raw_string();
    }
    s += args;
  }
  s += "\"";
  if (is_async()) {
    s += ", Async";
  }
  return s;
}

std::string
SimpleCommand::to_ast_string(usize layer) const
{
  SHIT_UNUSED(layer);
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

void
SimpleCommand::append_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

void
SimpleCommand::redirect_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

/**
 * class: Sequence
 */
CompoundList::CompoundList() : Expression({0, 0}), m_nodes() {}

CompoundList::CompoundList(
    SourceLocation                                    location,
    const std::vector<const CompoundListCondition *> &nodes)
    : Expression(location), m_nodes(nodes)
{}

CompoundList::~CompoundList()
{
  for (const CompoundListCondition *e : m_nodes) {
    delete e;
  }
}

bool
CompoundList::empty() const
{
  return m_nodes.empty();
}

void
CompoundList::append_node(const CompoundListCondition *node)
{
  m_nodes.emplace_back(node);
}

std::string
CompoundList::to_string() const
{
  return "CompoundList";
}

std::string
CompoundList::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};

  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  s += pad + "[" + to_string() + "]";
  for (const CompoundListCondition *n : m_nodes) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + n->to_ast_string(layer + 1);
  }

  return s;
}

i64
CompoundList::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_nodes.size() > 0);

  static constexpr i64 nothing_was_executed = -256;
  i64                  ret = nothing_was_executed;

  for (const CompoundListCondition *n : m_nodes) {
    switch (n->kind()) {
    case CompoundListCondition::Kind::None: {
      ret = n->evaluate(cxt);
    } break;

    case CompoundListCondition::Kind::Or:
      if (ret != 0) {
        ret = n->evaluate(cxt);
      }
      break;

    case CompoundListCondition::Kind::And:
      if (ret == 0) {
        ret = n->evaluate(cxt);
      }
      break;
    }
  }

  SHIT_ASSERT(ret != nothing_was_executed);

  return ret;
}

/**
 * class: CompoundListCondition
 */
CompoundListCondition::CompoundListCondition(SourceLocation location, Kind kind,
                                             const Command *expr)
    : Expression(location), m_kind(kind), m_cmd(expr)
{}

CompoundListCondition::~CompoundListCondition() { delete m_cmd; }

CompoundListCondition::Kind
CompoundListCondition::kind() const
{
  return m_kind;
}

std::string
CompoundListCondition::to_string() const
{
  std::string k;
  switch (kind()) {
  case Kind::None: k = "None"; break;
  case Kind::And: k = "&&"; break;
  case Kind::Or: k = "||"; break;
  default: SHIT_UNREACHABLE();
  }
  return "CompoundListCondition, " + k;
}

std::string
CompoundListCondition::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }

  s += pad + "[" + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_cmd->to_ast_string(layer + 1);

  return s;
}

i64
CompoundListCondition::evaluate_impl(EvalContext &cxt) const
{
  return m_cmd->evaluate(cxt);
}

/**
 * class: Pipeline
 */
Pipeline::Pipeline(SourceLocation                            location,
                   const std::vector<const SimpleCommand *> &commands)
    : Command(location), m_commands(commands)
{}

Pipeline::~Pipeline()
{
  for (const SimpleCommand *e : m_commands) {
    delete e;
  }
}

std::string
Pipeline::to_string() const
{
  return "Pipeline";
}

std::string
Pipeline::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }

  s += pad + "[" + to_string();
  if (is_async()) {
    s += ", Async";
  }
  s += "]";
  for (const SimpleCommand *e : m_commands) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + e->to_ast_string(layer + 1);
  }

  return s;
}

i64
Pipeline::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_commands.size() > 1);

  std::vector<utils::ExecContext> ecs;
  ecs.reserve(m_commands.size());

  for (const SimpleCommand *e : m_commands) {
    cxt.add_evaluated_expression();
    ecs.emplace_back(utils::ExecContext::make_from(
        e->source_location(), cxt.process_args(e->args())));
  }

  return utils::execute_contexts_with_pipes(std::move(ecs), is_async());
}

void
Pipeline::append_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

void
Pipeline::redirect_to(usize d, std::string &f, bool duplicate)
{
  SHIT_UNUSED(d);
  SHIT_UNUSED(f);
  SHIT_UNUSED(duplicate);
  throw ErrorWithLocation{source_location(), "Not implemented (Expressions)"};
}

/**
 * class: UnaryExpression
 */
UnaryExpression::UnaryExpression(SourceLocation location, const Expression *rhs)
    : Expression(location), m_rhs(rhs)
{}

UnaryExpression::~UnaryExpression() { delete m_rhs; }

std::string
UnaryExpression::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
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
BinaryExpression::BinaryExpression(SourceLocation    location,
                                   const Expression *lhs, const Expression *rhs)
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
  std::string s{};
  std::string pad{};

  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  s += pad + "[Binary " + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_lhs->to_ast_string(layer + 1) + "\n";
  s += pad + EXPRESSION_AST_INDENT + m_rhs->to_ast_string(layer + 1);

  return s;
}

/**
 * class: ConstantNumber
 */
ConstantNumber::ConstantNumber(SourceLocation location, i64 value)
    : Expression(location), m_value(value)
{}

ConstantNumber::~ConstantNumber() = default;

i64
ConstantNumber::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  return m_value;
}

std::string
ConstantNumber::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
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
ConstantString::ConstantString(SourceLocation     location,
                               const std::string &value)
    : Expression(location), m_value(value)
{}

ConstantString::~ConstantString() = default;

i64
ConstantString::evaluate_impl(EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  SHIT_UNREACHABLE();
}

std::string
ConstantString::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
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
Negate::Negate(SourceLocation location, const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
Negate::to_string() const
{
  return "-";
}

i64
Negate::evaluate_impl(EvalContext &cxt) const
{
  return -m_rhs->evaluate(cxt);
}

/**
 * class: Unnegate
 */
Unnegate::Unnegate(SourceLocation location, const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
Unnegate::to_string() const
{
  return "+";
}

i64
Unnegate::evaluate_impl(EvalContext &cxt) const
{
  return +m_rhs->evaluate(cxt);
}

/**
 * class: LogicalNot
 */
LogicalNot::LogicalNot(SourceLocation location, const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
LogicalNot::to_string() const
{
  return "!";
}

i64
LogicalNot::evaluate_impl(EvalContext &cxt) const
{
  return !m_rhs->evaluate(cxt);
}

/**
 * class: BinaryComplement
 */
BinaryComplement::BinaryComplement(SourceLocation    location,
                                   const Expression *rhs)
    : UnaryExpression(location, rhs)
{}

std::string
BinaryComplement::to_string() const
{
  return "~";
}

i64
BinaryComplement::evaluate_impl(EvalContext &cxt) const
{
  return ~m_rhs->evaluate(cxt);
}

/**
 * class: Add
 */
Add::Add(SourceLocation location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Add::to_string() const
{
  return "+";
}

i64
Add::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) + m_rhs->evaluate(cxt);
}

/**
 * class: Subtract
 */
Subtract::Subtract(SourceLocation location, const Expression *lhs,
                   const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Subtract::to_string() const
{
  return "-";
}

i64
Subtract::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) - m_rhs->evaluate(cxt);
}

/**
 * class: Multiply
 */
Multiply::Multiply(SourceLocation location, const Expression *lhs,
                   const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Multiply::to_string() const
{
  return "*";
}

i64
Multiply::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) * m_rhs->evaluate(cxt);
}

/**
 * class: Divide
 */
Divide::Divide(SourceLocation location, const Expression *lhs,
               const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Divide::to_string() const
{
  return "/";
}

i64
Divide::evaluate_impl(EvalContext &cxt) const
{
  i64 denom = m_rhs->evaluate(cxt);
  if (denom == 0)
    throw ErrorWithLocation{m_rhs->source_location(), "Division by 0"};
  return m_lhs->evaluate(cxt) / denom;
}

/**
 * class: Module
 */
Module::Module(SourceLocation location, const Expression *lhs,
               const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Module::to_string() const
{
  return "%";
}

i64
Module::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) % m_rhs->evaluate(cxt);
}

/**
 * class: BinaryAnd
 */
BinaryAnd::BinaryAnd(SourceLocation location, const Expression *lhs,
                     const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
BinaryAnd::to_string() const
{
  return "&";
}

i64
BinaryAnd::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) & m_rhs->evaluate(cxt);
}

/**
 * class: LogicalAnd
 */
LogicalAnd::LogicalAnd(SourceLocation location, const Expression *lhs,
                       const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LogicalAnd::to_string() const
{
  return "&&";
}

i64
LogicalAnd::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) && m_rhs->evaluate(cxt);
}

/**
 * class: GreaterThan
 */
GreaterThan::GreaterThan(SourceLocation location, const Expression *lhs,
                         const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
GreaterThan::to_string() const
{
  return ">";
}

i64
GreaterThan::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) > m_rhs->evaluate(cxt);
}

/**
 * class: GreaterOrEqual
 */
GreaterOrEqual::GreaterOrEqual(SourceLocation location, const Expression *lhs,
                               const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
GreaterOrEqual::to_string() const
{
  return ">=";
}

i64
GreaterOrEqual::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) >= m_rhs->evaluate(cxt);
}

/**
 * class: RightShift
 */
RightShift::RightShift(SourceLocation location, const Expression *lhs,
                       const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
RightShift::to_string() const
{
  return ">>";
}

i64
RightShift::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) >> m_rhs->evaluate(cxt);
}

/**
 * class: LessThan
 */
LessThan::LessThan(SourceLocation location, const Expression *lhs,
                   const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LessThan::to_string() const
{
  return "<";
}

i64
LessThan::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) < m_rhs->evaluate(cxt);
}

/**
 * class: LessOrEqual
 */
LessOrEqual::LessOrEqual(SourceLocation location, const Expression *lhs,
                         const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LessOrEqual::to_string() const
{
  return "<=";
}

i64
LessOrEqual::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) <= m_rhs->evaluate(cxt);
}

/**
 * class: LeftShift
 */
LeftShift::LeftShift(SourceLocation location, const Expression *lhs,
                     const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LeftShift::to_string() const
{
  return "<<";
}

i64
LeftShift::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) << m_rhs->evaluate(cxt);
}

/**
 * class: BinaryOr
 */
BinaryOr::BinaryOr(SourceLocation location, const Expression *lhs,
                   const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
BinaryOr::to_string() const
{
  return "|";
}

i64
BinaryOr::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) | m_rhs->evaluate(cxt);
}

/**
 * class: LogicalOr
 */
LogicalOr::LogicalOr(SourceLocation location, const Expression *lhs,
                     const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
LogicalOr::to_string() const
{
  return "||";
}

i64
LogicalOr::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) || m_rhs->evaluate(cxt);
}

/**
 * class: Xor
 */
Xor::Xor(SourceLocation location, const Expression *lhs, const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Xor::to_string() const
{
  return "^";
}

i64
Xor::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) ^ m_rhs->evaluate(cxt);
}

/**
 * class: Equal
 */
Equal::Equal(SourceLocation location, const Expression *lhs,
             const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
Equal::to_string() const
{
  return "==";
}

i64
Equal::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) == m_rhs->evaluate(cxt);
}

/**
 * class: NotEqual
 */
NotEqual::NotEqual(SourceLocation location, const Expression *lhs,
                   const Expression *rhs)
    : BinaryExpression(location, lhs, rhs)
{}

std::string
NotEqual::to_string() const
{
  return "!=";
}

i64
NotEqual::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) != m_rhs->evaluate(cxt);
}

} /* namespace expressions */

} /* namespace shit */
