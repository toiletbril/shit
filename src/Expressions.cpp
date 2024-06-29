#include "Expressions.hpp"

#include "Builtin.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

namespace shit {

static constexpr const char *EXPRESSION_AST_INDENT = " ";
static constexpr const char *EXPRESSION_DOUBLE_AST_INDENT = "  ";

/**
 * class: EvalContext
 */
EvalContext::EvalContext() = default;

void
EvalContext::add_evaluated_expression()
{
  m_expressions_executed_last++;
}

void
EvalContext::end_command()
{
  m_expressions_executed_total += m_expressions_executed_last;
  m_expressions_executed_last = 0;
}

std::string
EvalContext::make_stats_string() const
{
  std::string s{};
  s += "[Statistics:\n";

  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Nodes evaluated: " + std::to_string(last_expressions_executed());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total nodes evaluated: " + std::to_string(total_expressions_executed());
  s += '\n';

  s += "]";
  return s;
}

usize
EvalContext::last_expressions_executed() const
{
  return m_expressions_executed_last;
}

usize
EvalContext::total_expressions_executed() const
{
  return m_expressions_executed_total + m_expressions_executed_last;
}

std::vector<std::string>
EvalContext::expand_args(const std::vector<const Token *> &args) const
{
  std::vector<std::string> expanded_args{};
  expanded_args.reserve(args.size());
  for (const Token *t : args) {
    expanded_args.emplace_back(t->value());
  }
  return expanded_args;
}

/**
 * class: Expression
 */
Expression::Expression(usize location) : m_location(location) {}

usize
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
 * class: DummyExpression
 */
DummyExpression::DummyExpression(usize location) : Expression(location) {}

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
 * class: Exec
 */
Exec::Exec(usize location, const std::vector<const Token *> &&args)
    : Expression(location), m_args(args)
{}

Exec::~Exec()
{
  for (const Token *t : m_args) {
    delete t;
  }
}

const std::vector<const Token *> &
Exec::args() const
{
  return m_args;
}

i64
Exec::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_args.size() > 0);

  return utils::execute_context(
      utils::ExecContext::make(source_location(), cxt.expand_args(m_args)));

  SHIT_UNREACHABLE();
}

std::string
Exec::to_string() const
{
  std::string args{};
  std::string s = "Exec \"" + m_args[0]->value();
  if (!m_args.empty()) {
    for (usize i = 1; i < m_args.size(); i++) {
      args += " ";
      args += m_args[i]->value();
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
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  return pad + "[" + to_string() + "]";
}

/**
 * class: Sequence
 */
Sequence::Sequence(usize location) : Expression(location), m_nodes() {}

Sequence::Sequence(usize                                    location,
                   const std::vector<const SequenceNode *> &nodes)
    : Expression(location), m_nodes(nodes)
{}

Sequence::~Sequence()
{
  for (const SequenceNode *e : m_nodes) {
    delete e;
  }
}

bool
Sequence::empty() const
{
  return m_nodes.empty();
}

void
Sequence::append_node(const SequenceNode *node)
{
  m_nodes.emplace_back(node);
}

std::string
Sequence::to_string() const
{
  return "Sequence";
}

std::string
Sequence::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};

  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }
  s += pad + "[Sequence]";
  for (const SequenceNode *n : m_nodes) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + n->to_ast_string(layer + 1);
  }

  return s;
}

i64
Sequence::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_nodes.size() > 0);

  static constexpr i64 nothing_was_executed = -256;
  i64                  ret = nothing_was_executed;

  for (const SequenceNode *n : m_nodes) {
    switch (n->kind()) {
    case SequenceNode::Kind::Simple: {
      ret = n->evaluate(cxt);
    } break;

    case SequenceNode::Kind::Or:
      if (ret != 0) {
        ret = n->evaluate(cxt);
      }
      break;

    case SequenceNode::Kind::And:
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
 * class: SequenceNode
 */
SequenceNode::SequenceNode(usize location, Kind kind, const Expression *expr)
    : Expression(location), m_kind(kind), m_expr(expr)
{}

SequenceNode::~SequenceNode() { delete m_expr; }

SequenceNode::Kind
SequenceNode::kind() const
{
  return m_kind;
}

std::string
SequenceNode::to_string() const
{
  std::string k;
  switch (kind()) {
  case Kind::Simple: k = "Simple"; break;
  case Kind::And: k = "And"; break;
  case Kind::Or: k = "Or"; break;
  default: SHIT_UNREACHABLE();
  }
  return "SequenceNode " + k;
}

std::string
SequenceNode::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }

  s += pad + "[" + to_string() + "]\n";
  s += pad + EXPRESSION_AST_INDENT + m_expr->to_ast_string(layer + 1);

  return s;
}

i64
SequenceNode::evaluate_impl(EvalContext &cxt) const
{
  return m_expr->evaluate(cxt);
}

ExecPipeSequence::ExecPipeSequence(usize                            location,
                                   const std::vector<const Exec *> &commands)
    : Expression(location), m_commands(commands)
{}

ExecPipeSequence::~ExecPipeSequence()
{
  for (const Exec *e : m_commands) {
    delete e;
  }
}

std::string
ExecPipeSequence::to_string() const
{
  return "ExecPipeSequence";
}

std::string
ExecPipeSequence::to_ast_string(usize layer) const
{
  std::string s{};
  std::string pad{};
  for (usize i = 0; i < layer; i++) {
    pad += EXPRESSION_AST_INDENT;
  }

  s += pad + "[ExecPipeSequence]";
  for (const Exec *e : m_commands) {
    s += '\n';
    s += pad + EXPRESSION_AST_INDENT + e->to_ast_string(layer + 1);
  }

  return s;
}

i64
ExecPipeSequence::evaluate_impl(EvalContext &cxt) const
{
  SHIT_ASSERT(m_commands.size() > 1);

  std::vector<utils::ExecContext> ecs;
  ecs.reserve(m_commands.size());

  for (const Exec *e : m_commands) {
    cxt.add_evaluated_expression();
    ecs.emplace_back(utils::ExecContext::make(e->source_location(),
                                              cxt.expand_args(e->args())));
  }

  return utils::execute_contexts_with_pipes(std::move(ecs));
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
ConstantNumber::ConstantNumber(usize location, i64 value)
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
ConstantString::ConstantString(usize location, const std::string &value)
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
Negate::Negate(usize location, const Expression *rhs)
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
Unnegate::Unnegate(usize location, const Expression *rhs)
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
LogicalNot::LogicalNot(usize location, const Expression *rhs)
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
BinaryComplement::BinaryComplement(usize location, const Expression *rhs)
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
Add::Add(usize location, const Expression *lhs, const Expression *rhs)
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
Subtract::Subtract(usize location, const Expression *lhs, const Expression *rhs)
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
Multiply::Multiply(usize location, const Expression *lhs, const Expression *rhs)
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
Divide::Divide(usize location, const Expression *lhs, const Expression *rhs)
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
Module::Module(usize location, const Expression *lhs, const Expression *rhs)
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
BinaryAnd::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) & m_rhs->evaluate(cxt);
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
LogicalAnd::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) && m_rhs->evaluate(cxt);
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
GreaterThan::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) > m_rhs->evaluate(cxt);
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
GreaterOrEqual::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) >= m_rhs->evaluate(cxt);
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
RightShift::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) >> m_rhs->evaluate(cxt);
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
LessThan::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) < m_rhs->evaluate(cxt);
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
LessOrEqual::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) <= m_rhs->evaluate(cxt);
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
LeftShift::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) << m_rhs->evaluate(cxt);
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
BinaryOr::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) | m_rhs->evaluate(cxt);
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
LogicalOr::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) || m_rhs->evaluate(cxt);
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
Xor::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) ^ m_rhs->evaluate(cxt);
}

/**
 * class: Equal
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
Equal::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) == m_rhs->evaluate(cxt);
}

/**
 * class: NotEqual
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
NotEqual::evaluate_impl(EvalContext &cxt) const
{
  return m_lhs->evaluate(cxt) != m_rhs->evaluate(cxt);
}

} /* namespace shit */
