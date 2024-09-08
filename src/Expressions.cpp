#include "Expressions.hpp"

#include "Builtin.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <filesystem>

namespace shit {

static constexpr const char *EXPRESSION_AST_INDENT = " ";
static constexpr const char *EXPRESSION_DOUBLE_AST_INDENT = "  ";

/**
 * class: EvalContext
 */
EvalContext::EvalContext(bool should_disable_path_expansion)
    : m_enable_path_expansion(!should_disable_path_expansion)
{}

void
EvalContext::add_evaluated_expression()
{
  m_expressions_executed_last++;
}

void
EvalContext::add_expansion()
{
  m_expansions_last++;
}

void
EvalContext::end_command()
{
  m_expansions_total += m_expansions_last;
  m_expansions_last = 0;

  m_expressions_executed_total += m_expressions_executed_last;
  m_expressions_executed_last = 0;
}

std::string
EvalContext::make_stats_string() const
{
  std::string s{};

  s += "[Statistics:\n";

  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Expansions: " + std::to_string(last_expansion_count());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Nodes evaluated: " + std::to_string(last_expressions_executed());
  s += '\n';
  s += EXPRESSION_DOUBLE_AST_INDENT;
  s += "Total expansions: " + std::to_string(total_expansion_count());
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

usize
EvalContext::last_expansion_count() const
{
  return m_expansions_last;
}

usize
EvalContext::total_expansion_count() const
{
  return m_expansions_total + m_expansions_last;
}

/* TODO: Test symlinks. */
/* TODO: What the fuck is happening. */
std::vector<std::string>
EvalContext::expand_path_once(std::string_view path, bool should_expand_files)
{
  std::vector<std::string> expanded_paths{};

  usize last_slash = path.rfind('/');
  bool  has_slashes = (last_slash != std::string::npos);

  /* Prefix is the parent directory. */
  std::string parent_dir{};

  if (has_slashes) {
    if (last_slash != 0) {
      parent_dir = path.substr(0, last_slash);
    } else {
      parent_dir = path.substr(0, 1);
    }
  } else {
    parent_dir = ".";
  }

  /* Stem of the glob after the last slash. */
  std::optional<std::string_view> glob{};

  if (has_slashes) {
    if (last_slash + 1 < path.length()) {
      glob = path;
      glob->remove_prefix(last_slash + 1);
    } else {
      /* glob is empty. */
    }
  } else {
    glob = path;
  }

  std::filesystem::directory_iterator d{};

  try {
    d = std::filesystem::directory_iterator{parent_dir};
  } catch (std::filesystem::filesystem_error &e) {
    throw Error{"Could not descend into '" + std::string{parent_dir} +
                "': " + os::last_system_error_message()};
  }

  if (glob) {
    for (const std::filesystem::directory_entry &e : d) {
      if (!should_expand_files && !e.is_directory()) {
        continue;
      }
      std::string f = e.path().filename().string();
      /* TODO: Figure the rules of hidden file expansion. */
      if ((*glob)[0] != '.' && f[0] == '.') {
        continue;
      }
      if (utils::glob_matches(*glob, f)) {
        std::string expanded_path{};
        if (parent_dir != ".") {
          expanded_path += parent_dir;
          if (parent_dir != "/") {
            expanded_path += '/';
          }
        }
        expanded_path += f;
        add_expansion();
        expanded_paths.emplace_back(expanded_path);
      }
    }
  } else {
    expanded_paths.emplace_back(path);
  }

  return expanded_paths;
}

std::vector<std::string>
EvalContext::expand_path_recurse(const std::vector<std::string> &paths)
{
  std::vector<std::string> resulting_expanded_paths{};
  std::optional<usize>     expand_ch{};

  for (std::string_view original_path : paths) {
    std::string_view path = original_path;

    for (usize i = 0; i < path.length(); i++) {
      if (lexer::is_expandable_char(path[i])) {
        /* Is it escaped? */
        if (!(i > 0 && path[i - 1] == '\\')) {
          expand_ch = i;
          break;
        }
      }
    }
    if (expand_ch) {
      std::optional<usize> slash_after{};

      for (usize i = *expand_ch; i < path.length(); i++) {
        if (path[i] == '/') {
          slash_after = i;
          break;
        }
      }
      if (slash_after) {
        path.remove_suffix(path.length() - *slash_after);
      }

      std::vector<std::string> expanded_paths =
          expand_path_once(path, !slash_after);
      if (slash_after) {
        /* Bring back the removed suffix. */
        std::string_view removed_suffix = original_path;
        removed_suffix.remove_prefix(*slash_after);

        for (std::string &expanded_path : expanded_paths) {
          expanded_path += removed_suffix;
        }

        /* Call this function recursively on expanded entries. */
        std::vector<std::string> twice_expanded_paths =
            expand_path_recurse(expanded_paths);
        for (const std::string &twice_expanded_path : twice_expanded_paths) {
          resulting_expanded_paths.emplace_back(twice_expanded_path);
        }
      } else {
        for (const std::string &resulting_path : expanded_paths) {
          resulting_expanded_paths.emplace_back(resulting_path);
        }
      }
    } else {
      resulting_expanded_paths.emplace_back(path);
    }
  }

  return resulting_expanded_paths;
}

void
EvalContext::expand_tilde(std::string &p)
{
  if (p[0] == '~') {
    /* NOTE: escapes are not processed here. It works because 0-position is
     * hardcoded. */
    /* TODO: There may be several separators supported. */
    if (p.length() > 1 && p[1] != '/') {
      /* TODO: Expand different users. */
      return;
    }

    /* Remove the tilde. */
    p.erase(0, 1);

    std::optional<std::filesystem::path> u = os::get_home_directory();
    if (!u) {
      throw Error{"Could not figure out home directory"};
    }

    std::string s{u->string()};

#if PLATFORM_IS(WIN32)
    /* FIXME: Double-escape the bullshit path delimiters. */
    utils::string_replace(s, "\\", "\\\\");
#endif

    p.insert(0, s);
  }
}

void
EvalContext::erase_escapes(std::string &r)
{
  for (usize i = 0; i < r.size(); i++) {
    if (r[i] == '\\') {
      r.erase(i, 1);
      if (i == r.size()) {
        throw Error{"Nothing to escape"};
      }
    }
  }
}

std::vector<std::string>
EvalContext::expand_path(std::string &&r)
{
  std::vector<std::string> values{};

  if (m_enable_path_expansion) {
    values = expand_path_recurse({r});
  } else {
    values.emplace_back(r);
  }

  /* Sort expansion in lexicographical order. Ignore punctuation to be somewhat
   * compatible with bash. */
  std::stable_sort(
      values.begin(), values.end(), [](const auto &lhs, const auto &rhs) {
        const auto x =
            mismatch(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend(),
                     [](const auto &lhs, const auto &rhs) {
                       return tolower(lhs) == tolower(rhs) ||
                              (ispunct(lhs) && ispunct(rhs));
                     });
        return x.second != rhs.cend() &&
               (x.first == lhs.cend() ||
                tolower(*x.first) < tolower(*x.second));
      });

  if (values.empty()) {
    throw Error{"No expansions found for '" + r + "'"};
  }

  return values;
}

/* TODO: Command substitution. */
std::vector<std::string>
EvalContext::process_args(const std::vector<const Token *> &args)
{
  std::vector<std::string> expanded_args{};
  expanded_args.reserve(args.size());

  for (const Token *t : args) {
    try {
      if (t->flags() & Token::Flag::Expandable) {
        std::string r = t->raw_string();
        expand_tilde(r);
        std::vector<std::string> e = expand_path(std::move(r));
        for (std::string &a : e) {
          erase_escapes(a);
          expanded_args.emplace_back(a);
        }
      } else {
        std::string a = t->raw_string();
        expand_tilde(a);
        erase_escapes(a);
        expanded_args.emplace_back(a);
      }
    } catch (Error &e) {
      throw ErrorWithLocation{t->source_location(),
                              "Could not expand path: " + e.message()};
    }
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

namespace expressions {

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
 * class: Command
 */
Command::Command(usize location) : Expression(location) {}

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
 * class: SimpleCommand
 */
SimpleCommand::SimpleCommand(usize                              location,
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
      utils::ExecContext::make(source_location(), cxt.process_args(m_args)),
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
CompoundList::CompoundList(usize location) : Expression(location), m_nodes() {}

CompoundList::CompoundList(
    usize location, const std::vector<const CompoundListCondition *> &nodes)
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
CompoundListCondition::CompoundListCondition(usize location, Kind kind,
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
Pipeline::Pipeline(usize                                     location,
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
    ecs.emplace_back(utils::ExecContext::make(e->source_location(),
                                              cxt.process_args(e->args())));
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

} /* namespace expressions */

} /* namespace shit */
