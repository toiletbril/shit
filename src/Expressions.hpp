#pragma once

#include "Common.hpp"
#include "Eval.hpp"
#include "Tokens.hpp"

#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace shit {

using namespace tokens;

struct Token;

/* The prepass walks the whole tree once before any command runs. It carries the
   source for the caret, and a fatal flag that stops execution. A warning is a
   located message that does not stop execution, a failure does. */
struct AnalysisContext
{
  std::string_view source;
  bool has_fatal{false};
  /* Set once a dot, source, or eval is seen. Those run code the prepass cannot
     see, so a later unresolved command is a warning rather than a failure. */
  bool saw_runtime_definer{false};
  /* Names of functions seen so far. A call to one of these resolves, so a
     function defined before its use is not reported as a missing command. */
  std::unordered_set<std::string> defined_functions{};

  explicit AnalysisContext(std::string_view source_view) : source(source_view)
  {}

  void warn(SourceLocation location, const std::string &message);
  void fail(SourceLocation location, const std::string &message);
};

/* Walk the tree and report. Returns true when execution may proceed, false when
   an unconditional command failed to resolve. */
bool analyze_ast(const Expression *root, std::string_view source,
                 const std::unordered_set<std::string> &known_functions = {});

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

  /* Lightweight kind tags so a caller can recognize a node without RTTI or a
     string compare. The base returns false, the concrete node overrides. */
  virtual bool is_simple_command() const;
  virtual bool is_dummy() const;

  /* A node lives in the parse arena, so its storage is reclaimed in bulk. This
     no-ops for arena storage and frees an ordinary heap node otherwise. The
     destructor still runs through the normal delete. */
  static void operator delete(void *pointer);

  /* The prepass entry per node. The base does nothing, the command and the
     control flow nodes override it. is_unconditional says whether this node is
     reached on every run, which decides a failure from a warning. */
  virtual void analyze(AnalysisContext &actx, bool is_unconditional) const;

protected:
  virtual i64 evaluate_impl(EvalContext &cxt) const = 0;

  SourceLocation m_location;
};

namespace expressions {

struct IfStatement : public Expression
{
  IfStatement(SourceLocation location, const Expression *condition,
              const Expression *then, const Expression *otherwise);

  ~IfStatement() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const Expression *m_condition;
  const Expression *m_then;
  const Expression *m_otherwise;
};

struct DummyExpression : public Expression
{
  DummyExpression(SourceLocation location);

  bool is_dummy() const override;

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
  void set_local_vars(std::unordered_map<std::string, Word> &&vars);

  /* The ! reserved word in front of a pipeline inverts its exit status. */
  void set_negated();
  bool is_negated() const;

  virtual bool is_assignment() const;

  virtual void append_to(usize d, std::string &f, bool duplicate) = 0;
  virtual void redirect_to(usize d, std::string &f, bool duplicate) = 0;

protected:
  bool m_is_async{false};
  bool m_is_negated{false};
  Maybe<std::unordered_map<std::string, Word>> m_local_vars;
};

struct AssignCommand : public Command
{
  AssignCommand(SourceLocation location, const Assignment *a);
  ~AssignCommand() override;

  const Assignment *assignment() const;

  bool is_assignment() const override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

  void append_to(usize d, std::string &f, bool duplicate) override;
  void redirect_to(usize d, std::string &f, bool duplicate) override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const Assignment *m_assignment;
};

/* One redirection attached to a simple command. The target word is expanded to
   a filename at evaluation, or for a duplication it names a file descriptor. */
struct Redirection
{
  enum class Kind : u8
  {
    TruncateOutput,  /* >    */
    AppendOutput,    /* >>   */
    ReadInput,       /* <    */
    DuplicateOutput, /* >&   */
    Heredoc          /* <<   */
  };

  i32 fd;
  Kind kind;
  /* The filename word for a file redirection, or null otherwise. */
  const Token *target;
  /* For a duplication, the descriptor to copy from, as in 2>&1. */
  i32 dup_fd;
  /* For a heredoc, the lexer-owned body and whether it is expanded. */
  const std::string *heredoc_body;
  bool heredoc_expand;
};

struct SimpleCommand : public Command
{
  SimpleCommand(SourceLocation location,
                const std::vector<const Token *> &&args);
  ~SimpleCommand() override;

  void set_redirections(std::vector<Redirection> &&redirections);

  /* Open this command's redirections into an exec context, for a pipeline stage
     that does not go through evaluate_impl. */
  void redirect_exec_context(ExecContext &ec, EvalContext &cxt) const;

  bool is_simple_command() const override;

  const std::vector<const Token *> &args() const;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

  void append_to(usize d, std::string &f, bool duplicate) override;
  void redirect_to(usize d, std::string &f, bool duplicate) override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  std::vector<const Token *> m_args;

  /* The resolution of the command word, memoized so a command run repeatedly in
     a loop body does not search PATH on every iteration. The name guards the
     cache, since an expanded name from a variable may differ between runs. */
  mutable std::string m_resolved_name{};
  mutable Maybe<std::variant<Builtin::Kind, std::filesystem::path>>
      m_resolved_kind{};

  ArrayList<Redirection> m_redirections{heap_allocator()};
};

struct CompoundListCondition : public Expression
{
  enum class Kind : u8
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

  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  Kind m_kind;
  const Command *m_cmd;
};

struct CompoundList : public Expression
{
  CompoundList();

  ~CompoundList() override;

  bool is_empty() const;
  void append_node(const CompoundListCondition *node);

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  ArrayList<const CompoundListCondition *> m_nodes{heap_allocator()};
};

struct Pipeline : public Command
{
  Pipeline(SourceLocation location);

  ~Pipeline() override;

  bool is_empty() const;
  void append_command(const SimpleCommand *node);

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;

  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

  void append_to(usize d, std::string &f, bool duplicate) override;
  void redirect_to(usize d, std::string &f, bool duplicate) override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  ArrayList<const SimpleCommand *> m_commands{heap_allocator()};
};

/* A compound command groups one or more command lists, like a loop body or an
   if branch. It slots into a CompoundListCondition as an ordinary command.
   Redirections on a compound command are not supported yet. */
struct CompoundCommand : public Command
{
  CompoundCommand(SourceLocation location);

  void append_to(usize d, std::string &f, bool duplicate) override;
  void redirect_to(usize d, std::string &f, bool duplicate) override;
};

struct IfClause : public CompoundCommand
{
  /* Each branch pairs a condition list with the body to run when it succeeds.
     The plain if and every elif share this list. The else body has no
     condition and is held separately. */
  IfClause(
      SourceLocation location,
      std::vector<std::pair<const Expression *, const Expression *>> &&branches,
      const Expression *otherwise);
  ~IfClause() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  std::vector<std::pair<const Expression *, const Expression *>> m_branches;
  const Expression *m_otherwise;
};

struct WhileLoop : public CompoundCommand
{
  WhileLoop(SourceLocation location, const Expression *condition,
            const Expression *body, bool is_until);
  ~WhileLoop() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const Expression *m_condition;
  const Expression *m_body;
  /* An until loop runs the body while the condition is non-zero. */
  bool m_is_until;
};

struct ForLoop : public CompoundCommand
{
  ForLoop(SourceLocation location, std::string variable_name,
          std::vector<const Token *> &&words, bool has_in_clause,
          const Expression *body);
  ~ForLoop() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  std::string m_variable_name;
  std::vector<const Token *> m_words;
  /* Without an in clause, a for loop iterates the positional parameters. */
  bool m_has_in_clause;
  const Expression *m_body;
};

/* One arm of a case, a set of patterns and the body that runs on a match. */
struct CaseItem
{
  std::vector<const Token *> patterns;
  const Expression *body;
};

struct CaseClause : public CompoundCommand
{
  CaseClause(SourceLocation location, const Token *word,
             std::vector<CaseItem> &&items);
  ~CaseClause() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const Token *m_word;
  std::vector<CaseItem> m_items;
};

struct BraceGroup : public CompoundCommand
{
  BraceGroup(SourceLocation location, const Expression *body);
  ~BraceGroup() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const Expression *m_body;
};

struct Subshell : public CompoundCommand
{
  Subshell(SourceLocation location, const Expression *body);
  ~Subshell() override;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  const Expression *m_body;
};

struct FunctionDefinition : public CompoundCommand
{
  FunctionDefinition(SourceLocation location, std::string name,
                     const Expression *body);
  ~FunctionDefinition() override;

  const std::string &name() const;
  const Expression *body() const;

  std::string to_string() const override;
  std::string to_ast_string(usize layer = 0) const override;
  void analyze(AnalysisContext &actx, bool is_unconditional) const override;

protected:
  i64 evaluate_impl(EvalContext &cxt) const override;

  std::string m_name;
  const Expression *m_body;
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

struct UnaryExpression : public Expression
{
  UnaryExpression(SourceLocation location, const Expression *rhs);
  ~UnaryExpression() override;

  std::string to_ast_string(usize layer = 0) const override;

protected:
  const Expression *m_rhs;
};

#define UNARY_EXPRESSION_STRUCT(e)                                             \
  struct e : public UnaryExpression                                            \
  {                                                                            \
    e(SourceLocation location, const Expression *rhs);                         \
    std::string to_string() const override;                                    \
                                                                               \
  protected:                                                                   \
    i64 evaluate_impl(EvalContext &cxt) const override;                        \
  }

UNARY_EXPRESSION_STRUCT(Negate);
UNARY_EXPRESSION_STRUCT(Unnegate);
UNARY_EXPRESSION_STRUCT(LogicalNot);
UNARY_EXPRESSION_STRUCT(BinaryComplement);

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

#define BINARY_EXPRESSION_STRUCT(e)                                            \
  struct e : public BinaryExpression                                           \
  {                                                                            \
    e(SourceLocation location, const Expression *lhs, const Expression *rhs);  \
    std::string to_string() const override;                                    \
                                                                               \
  protected:                                                                   \
    i64 evaluate_impl(EvalContext &cxt) const override;                        \
  }

BINARY_EXPRESSION_STRUCT(BinaryDummyExpression);
BINARY_EXPRESSION_STRUCT(Add);
BINARY_EXPRESSION_STRUCT(Subtract);
BINARY_EXPRESSION_STRUCT(Multiply);
BINARY_EXPRESSION_STRUCT(Divide);
BINARY_EXPRESSION_STRUCT(Module);
BINARY_EXPRESSION_STRUCT(BinaryAnd);
BINARY_EXPRESSION_STRUCT(LogicalAnd);
BINARY_EXPRESSION_STRUCT(GreaterThan);
BINARY_EXPRESSION_STRUCT(GreaterOrEqual);
BINARY_EXPRESSION_STRUCT(RightShift);
BINARY_EXPRESSION_STRUCT(LeftShift);
BINARY_EXPRESSION_STRUCT(LessThan);
BINARY_EXPRESSION_STRUCT(LessOrEqual);
BINARY_EXPRESSION_STRUCT(BinaryOr);
BINARY_EXPRESSION_STRUCT(LogicalOr);
BINARY_EXPRESSION_STRUCT(Xor);
BINARY_EXPRESSION_STRUCT(Equal);
BINARY_EXPRESSION_STRUCT(NotEqual);

} /* namespace expressions */

} /* namespace shit */
