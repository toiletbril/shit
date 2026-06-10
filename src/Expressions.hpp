#pragma once

#include "Common.hpp"
#include "Eval.hpp"
#include "Tokens.hpp"

namespace shit {

using namespace tokens;

class Token;

namespace expressions {
class IfClause;
class WhileLoop;
class AssignCommand;
class SimpleCommand;
} /* namespace expressions */

/* The prepass walks the whole tree once before any command runs. It carries the
   source for the caret, and a fatal flag that stops execution. A warning is a
   located message that does not stop execution, a failure does. */
class AnalysisContext
{
public:
  StringView source;
  bool has_fatal{false};
  /* The analysis runs by default and a found error is fatal, so a script with a
     command that cannot resolve does not run. -W keeps the analysis but reports
     every error as a warning and lets the run proceed, which this flag carries
     into fail(). POSIX mode and bash mode skip the whole stage, so nothing here
     runs at all and the file executes the way dash or bash does. */
  bool errors_are_warnings{false};
  /* Set once a dot, source, or eval is seen. Those run code the prepass cannot
     see, so a later unresolved command is a warning rather than a failure. */
  bool saw_runtime_definer{false};
  /* Names of functions seen so far. A call to one of these resolves, so a
     function defined before its use is not reported as a missing command. */
  HashSet defined_functions{heap_allocator()};
  /* Names already defined as aliases. A call to one resolves at runtime through
     the alias expansion, so it is not a missing command. */
  HashSet known_aliases{heap_allocator()};
  /* Caches whether a command name resolved against the builtins and PATH during
     this pass. A name run many times across the file then hits the filesystem
     at most once. */
  HashMap<bool> command_resolution_cache{heap_allocator()};
  /* Names assigned a plain literal value in the current straight-line block,
     mapped to that value. The constant-propagation rule records a name here on
     an unconditional literal assignment and reads it to fold a $name reference
     in a later static condition or constant arithmetic. The table is cleared at
     a conditional branch, a loop body, a function body, a subshell, and on any
     runtime definer, since a value recorded before such a boundary is no longer
     proven to hold past it. */
  HashMap<String> constant_variables{heap_allocator()};

  /* The depth of function bodies the prepass is inside, raised on entry to a
     function body and lowered on exit. A plain scalar assignment at a depth above
     zero leaks to the global scope, which a warning flags. */
  usize function_scope_depth{0};

  /* The names the current function body declared with local, declare, or
     typeset, so an assignment to one of them does not warn about a leak. The set
     is saved and cleared on entry to a function body and restored on exit. */
  HashSet function_local_names{heap_allocator()};

  explicit AnalysisContext(StringView source_view) : source(source_view) {}

  fn warn(SourceLocation location, StringView message) throws -> void;
  fn fail(SourceLocation location, StringView message) throws -> void;
};

/* Walk the tree and report. Returns true when execution may proceed, false when
   an unconditional command failed to resolve. */
fn analyze_ast(const Expression *root, StringView source,
               const HashSet &known_functions, const HashSet &known_aliases,
               bool errors_are_warnings) throws -> bool;

class Expression
{
public:
  Expression() = delete;
  Expression(SourceLocation location);

  virtual ~Expression() = default;

  pure fn source_location() const wontthrow -> SourceLocation;
  /* The byte just past this node's source text. It defaults to the end of the
     opening token that source_location names, and a compound node whose source
     runs to a closing token, such as a brace group's '}', sets it to that
     token's end so the whole node's source span can be recovered without
     widening source_location, which the error caret reads. */
  pure fn source_end_position() const wontthrow -> usize;
  fn set_source_end_position(usize position) wontthrow -> void;
  /* Expressions should override evaluate_impl() instead. This method is used
   * mainly for initialization before the actual evaluation. */
  fn evaluate(EvalContext &cxt) const throws -> i64;

  /* Each expression should provide it's own way to copy it. */
  Expression(const Expression &) = delete;
  Expression(Expression &&) noexcept = delete;
  Expression &operator=(const Expression &) = delete;
  Expression &operator=(Expression &&) noexcept = delete;

  virtual fn to_string() const throws -> String = 0;
  virtual fn to_ast_string(usize layer = 0) const throws -> String;

  virtual fn is_simple_command() const wontthrow -> bool;
  virtual fn is_dummy() const wontthrow -> bool;

  /* The typed-node downcasts the optimizer rules use to match a node without
     RTTI, since the build links with -fno-rtti. The base returns nullptr and a
     node of the matching kind overrides its own hook to return this, so a rule
     reads a typed pointer or skips the node. */
  virtual fn as_if_clause() const wontthrow -> const expressions::IfClause *;
  virtual fn as_while_loop() const wontthrow -> const expressions::WhileLoop *;
  virtual fn as_assign_command() const wontthrow
      -> const expressions::AssignCommand *;
  virtual fn as_simple_command() const wontthrow
      -> const expressions::SimpleCommand *;

  /* A node lives in the parse arena, so its storage is reclaimed in bulk. This
     no-ops for arena storage and frees an ordinary heap node otherwise. The
     destructor still runs through the normal delete. */
  static fn operator delete(void *pointer) wontthrow->void;

  /* The prepass entry per node. The base does None, the command and the
     control flow nodes override it. is_unconditional says whether this node is
     reached on every run, which decides a failure from a warning. */
  virtual fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void;

  /* Register the function names this node defines at the top level, before the
     ordered analyze walk. A call to a sibling function defined later in the
     file then resolves without a PATH scan or a missing-command warning. The
     base does None, the list nodes forward to their children, and a function
     definition adds its own name. */
  virtual fn register_defined_functions(AnalysisContext &actx) const throws
      -> void;

  /* The statically-decidable success of this node used as a loop or if
     condition. Some(true) means the condition always succeeds with status 0 and
     no side effect, Some(false) means it always fails, and None means the
     result is only known at run time. The base returns None, the list wrappers
     forward through a single unconditional command, and a simple command
     decides from a constant builtin such as true, false, or a literal test. */
  virtual fn
  try_static_condition_verdict(const AnalysisContext &actx) const wontthrow
      -> Maybe<bool>;

protected:
  virtual fn evaluate_impl(EvalContext &cxt) const throws -> i64 = 0;

  SourceLocation m_location;
  usize m_source_end_position;
};

namespace expressions {

class IfStatement : public Expression
{
public:
  IfStatement(SourceLocation location, const Expression *condition,
              const Expression *then, const Expression *otherwise);

  ~IfStatement() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const Expression *m_condition;
  const Expression *m_then;
  const Expression *m_otherwise;
};

class DummyExpression : public Expression
{
public:
  DummyExpression(SourceLocation location);

  fn is_dummy() const wontthrow -> bool override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;
};

/* One prefix assignment on a simple command, the name, the right hand side, and
   whether the source spelled it as the appending NAME+=VALUE form. The prefix
   assignments are kept as an ordered list rather than a map, so they preserve
   left-to-right order and a repeated name accumulates rather than overwrites.
 */
struct prefix_assignment
{
  String name;
  Word value;
  bool is_append;
};

/* One NAME=(...) array assignment given as an argument to an assignment builtin
   such as local or declare. The element words expand the way a command's
   arguments do, and the command applies them in the scope the builtin selects,
   so local arr=(...) declares a local array while declare and export reach the
   global store. */
struct array_builtin_assignment
{
  String name;
  ArrayList<const Token *> elements;
  bool is_append;
};

class Command : public Expression
{
public:
  Command(SourceLocation location);

  fn make_async() wontthrow -> void;
  pure fn is_async() const wontthrow -> bool;
  fn set_local_vars(ArrayList<prefix_assignment> &&vars) throws -> void;

  /* The prefix assignments on this command, read by the constant-arithmetic
     rule to fold a constant $((...)) in a prefix value. */
  pure fn local_vars() const wontthrow -> const ArrayList<prefix_assignment> &;

  /* The ! reserved word in front of a pipeline inverts its exit status. */
  fn set_negated() wontthrow -> void;
  pure fn is_negated() const wontthrow -> bool;

  /* The time reserved word in front of a command, including a compound command,
     reports how long it took on stderr after it runs. The pretty report is the
     default, and time -p or time --posix selects the plain POSIX three-line
     form. */
  fn set_timed(bool posix_format) wontthrow -> void;
  pure fn is_timed() const wontthrow -> bool;
  pure fn time_uses_posix_format() const wontthrow -> bool;

  virtual fn is_assignment() const wontthrow -> bool;

  virtual fn append_to(usize d, String &f, bool duplicate) throws -> void = 0;
  virtual fn redirect_to(usize d, String &f, bool duplicate) throws -> void = 0;

protected:
  bool m_is_async{false};
  bool m_is_negated{false};
  bool m_is_timed{false};
  bool m_time_uses_posix_format{false};
  ArrayList<prefix_assignment> m_local_vars{heap_allocator()};
};

class AssignCommand : public Command
{
public:
  AssignCommand(SourceLocation location, const Assignment *a);
  ~AssignCommand() override;

  pure fn assignment() const wontthrow -> const Assignment *;

  fn is_assignment() const wontthrow -> bool override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;

  fn as_assign_command() const wontthrow -> const AssignCommand * override;

  fn append_to(usize d, String &f, bool duplicate) throws -> void override;
  fn redirect_to(usize d, String &f, bool duplicate) throws -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const Assignment *m_assignment;
};

/* One Redirection attached to a simple command. The target word is expanded to
   a filename at evaluation, or for a duplication it names a file descriptor. */
class Redirection
{
public:
  enum class Kind : u8
  {
    TruncateOutput,         /* >    */
    TruncateOutputOverride, /* >|   */
    AppendOutput,           /* >>   */
    ReadInput,              /* <    */
    ReadWrite,              /* <>   */
    DuplicateOutput,        /* >&   */
    DuplicateInput,         /* <&   */
    Heredoc,                /* <<   */
    HereString              /* <<<  */
  };

  /* The dup_fd value that marks the close-descriptor form, as in 2>&- and <&-,
     which closes fd rather than copying another descriptor onto it. */
  static constexpr i32 DUP_FD_CLOSE = -2;

  i32 fd;
  Kind kind;
  /* The filename word for a file Redirection. For a duplication it is the word
     after >& or <& when that word is a variable or an expansion such as $4, so
     the descriptor is resolved at evaluation. It is null for a duplication
     whose descriptor was a literal in the source. */
  const Token *target;
  /* For a duplication, the literal descriptor to copy from, as in 2>&1, or
     DUP_FD_CLOSE for the close form, or -1 when the descriptor is a dynamic
     word held in target. */
  i32 dup_fd;
  /* For a heredoc, the lexer-owned body and whether it is expanded. */
  const String *heredoc_body;
  bool heredoc_expand;
};

class SimpleCommand : public Command
{
public:
  SimpleCommand(SourceLocation location, ArrayList<const Token *> &&args);
  ~SimpleCommand() override;

  fn set_redirections(ArrayList<Redirection> &&redirections) throws -> void;

  /* The NAME=(...) array assignments given to an assignment builtin, applied
     after the builtin runs. */
  fn set_array_args(ArrayList<array_builtin_assignment> &&array_args) throws
      -> void;

  /* Open this command's redirections into an exec context, for a pipeline stage
     that does not go through evaluate_impl. */
  fn redirect_exec_context(ExecContext &ec, EvalContext &cxt) const throws
      -> void;

  fn is_simple_command() const wontthrow -> bool override;

  pure fn args() const wontthrow -> const ArrayList<const Token *> &;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;

  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

  fn try_static_condition_verdict(const AnalysisContext &actx) const wontthrow
      -> Maybe<bool> override;

  fn as_simple_command() const wontthrow -> const SimpleCommand * override;

  fn append_to(usize d, String &f, bool duplicate) throws -> void override;
  fn redirect_to(usize d, String &f, bool duplicate) throws -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  ArrayList<const Token *> m_args{heap_allocator()};

  /* The resolution of the command word, memoized so a command run repeatedly in
     a loop body does not search PATH on every iteration. The name guards the
     cache, since an expanded name from a variable may differ between runs. */
  mutable String m_resolved_name{};
  mutable Maybe<ResolvedCommand> m_resolved_kind{};

  ArrayList<Redirection> m_redirections{heap_allocator()};
  ArrayList<array_builtin_assignment> m_array_args{heap_allocator()};
};

class CompoundListCondition : public Expression
{
public:
  enum class Kind : u8
  {
    None,
    And,
    Or,
  };

  CompoundListCondition(SourceLocation location, Kind kind,
                        const Command *expr);
  ~CompoundListCondition() override;

  pure fn kind() const wontthrow -> Kind;

  /* True when the command this node holds carries a leading !, which set -e
     exempts from its exit. */
  pure fn is_negated() const wontthrow -> bool;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;
  fn try_static_condition_verdict(const AnalysisContext &actx) const wontthrow
      -> Maybe<bool> override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  Kind m_kind;
  const Command *m_cmd;
};

class CompoundList : public Expression
{
public:
  CompoundList();

  ~CompoundList() override;

  pure fn is_empty() const wontthrow -> bool;
  fn append_node(const CompoundListCondition *node) throws -> void;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;
  fn try_static_condition_verdict(const AnalysisContext &actx) const wontthrow
      -> Maybe<bool> override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  ArrayList<const CompoundListCondition *> m_nodes{heap_allocator()};
};

class Pipeline : public Command
{
public:
  Pipeline(SourceLocation location);

  ~Pipeline() override;

  pure fn is_empty() const wontthrow -> bool;
  fn append_command(const Command *node) throws -> void;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;

  fn append_to(usize d, String &f, bool duplicate) throws -> void override;
  fn redirect_to(usize d, String &f, bool duplicate) throws -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  /* The fork-per-stage path taken when a stage is a compound command. The
     all-simple path stays in evaluate_impl through execute_contexts_with_pipes.
   */
  fn evaluate_with_compound_stages(EvalContext &cxt) const throws -> i64;

  /* A stage is any command, either a simple command exec'd in a forked child or
     a compound command whose tree is evaluated in a forked child. */
  ArrayList<const Command *> m_commands{heap_allocator()};
};

/* A compound command groups one or more command lists, like a loop body or an
   if branch. It slots into a CompoundListCondition as an ordinary command.
   Redirections on a compound command are not supported yet. */
class CompoundCommand : public Command
{
public:
  CompoundCommand(SourceLocation location);

  fn append_to(usize d, String &f, bool duplicate) throws -> void override;
  fn redirect_to(usize d, String &f, bool duplicate) throws -> void override;
};

/* One branch of an if clause, the condition list and the body to run when it
   succeeds. The plain if and every elif each form one of these. */
struct if_branch
{
  const Expression *condition;
  const Expression *body;
};

class IfClause : public CompoundCommand
{
public:
  /* Each branch pairs a condition list with the body to run when it succeeds.
     The plain if and every elif share this list. The else body has no
     condition and is held separately. */
  IfClause(SourceLocation location, ArrayList<if_branch> &&branches,
           const Expression *otherwise);
  ~IfClause() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

  /* The branch list and the else body, read by the dead-branch-elimination rule
     to walk the conditions and judge each statically. */
  pure fn branches() const wontthrow -> const ArrayList<if_branch> &;
  pure fn otherwise() const wontthrow -> const Expression *;

  /* Record the branch the dead-branch-elimination rule proved this if takes, so
     the evaluator runs that body without testing any condition. The rule passes
     the branch index, or the branch count to select the else body or nothing.
   */
  fn set_folded_branch(usize index) const wontthrow -> void;
  pure fn has_folded_branch() const wontthrow -> bool;

  fn as_if_clause() const wontthrow -> const IfClause * override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  ArrayList<if_branch> m_branches{heap_allocator()};
  const Expression *m_otherwise;

  /* The branch the analyze pass proved this if takes, when every condition up
     to it has a statically-decidable verdict. Some(i) selects branch i's body,
     a value past the last branch selects the else body or nothing. None means
     the branch is only known at run time and evaluate_impl runs the conditions.
   */
  mutable Maybe<usize> m_folded_branch{};
};

class WhileLoop : public CompoundCommand
{
public:
  WhileLoop(SourceLocation location, const Expression *condition,
            const Expression *body, bool is_until);
  ~WhileLoop() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

  /* The condition and the until flag, read by the loop-elimination rule to
     judge whether the body ever runs. */
  pure fn condition() const wontthrow -> const Expression *;
  pure fn is_until() const wontthrow -> bool;

  /* Record that the loop-elimination rule proved the body never runs, so the
     evaluator yields 0 without testing the condition. */
  fn set_folded_to_skip() const wontthrow -> void;
  pure fn is_folded_to_skip() const wontthrow -> bool;

  fn as_while_loop() const wontthrow -> const WhileLoop * override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const Expression *m_condition;
  const Expression *m_body;
  /* An until loop runs the body while the condition is non-zero. */
  bool m_is_until;

  /* Set when the analyze pass proved the condition never lets the body run, so
     the whole loop is skipped. A while false or an until true folds to this. A
     while true stays unfolded, since the loop is infinite and still runs. */
  mutable bool m_folded_to_skip{false};
};

class ForLoop : public CompoundCommand
{
public:
  ForLoop(SourceLocation location, StringView variable_name,
          ArrayList<const Token *> &&words, bool has_in_clause,
          const Expression *body);
  ~ForLoop() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  String m_variable_name;
  ArrayList<const Token *> m_words{heap_allocator()};
  /* Without an in clause, a for loop iterates the positional parameters. */
  bool m_has_in_clause;
  const Expression *m_body;
};

/* How an arm ends. ;; stops the case, ;& falls into the next arm body without
   matching it, and ;;& resumes matching at the following arms. */
enum class case_terminator
{
  Break,
  FallThrough,
  ContinueMatch,
};

/* One arm of a case, a set of patterns, the body that runs on a match, and the
   terminator that decides what happens after the body. */
struct case_item
{
  ArrayList<const Token *> patterns;
  const Expression *body;
  case_terminator terminator;
};

class CaseClause : public CompoundCommand
{
public:
  CaseClause(SourceLocation location, const Token *word,
             ArrayList<case_item> &&items);
  ~CaseClause() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const Token *m_word;
  ArrayList<case_item> m_items{heap_allocator()};
};

class BraceGroup : public CompoundCommand
{
public:
  BraceGroup(SourceLocation location, const Expression *body);
  ~BraceGroup() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const Expression *m_body;
};

class Subshell : public CompoundCommand
{
public:
  Subshell(SourceLocation location, const Expression *body);
  ~Subshell() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const Expression *m_body;
};

/* The bash [[ ... ]] conditional command. It holds the collected elements and
   evaluates them with the double-bracket grammar, which does no field splitting
   on the operands, treats the right side of == and != as a glob pattern, and
   joins primaries with && and || rather than -a and -o. */
class ConditionalCommand : public CompoundCommand
{
public:
  ConditionalCommand(SourceLocation location,
                     ArrayList<conditional_element> elements);
  ~ConditionalCommand() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  ArrayList<conditional_element> m_elements;
};

/* The bash (( expr )) arithmetic command. It evaluates the expression and
   reports success when the value is non-zero, the way bash turns an arithmetic
   value into an exit status. */
class ArithmeticCommand : public CompoundCommand
{
public:
  ArithmeticCommand(SourceLocation location, String expression);
  ~ArithmeticCommand() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  String m_expression;
};

/* The bash C-style for, for (( init; condition; step )); do BODY; done. The
   init runs once, then the body runs while the condition is non-zero, with the
   step after each iteration. The three clauses are arithmetic expressions. */
class CStyleForLoop : public CompoundCommand
{
public:
  CStyleForLoop(SourceLocation location, String init, String condition,
                String step, const Expression *body);
  ~CStyleForLoop() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  String m_init;
  String m_condition;
  String m_step;
  const Expression *m_body;
};

/* The bash select loop, select name in words; do BODY; done. It prints a
   numbered menu of the words, reads a choice from standard input, binds the
   name to the chosen word and REPLY to the raw input, runs the body, and
   repeats until end of input or a break. */
class SelectLoop : public CompoundCommand
{
public:
  SelectLoop(SourceLocation location, StringView variable_name,
             ArrayList<const Token *> &&words, bool has_in_clause,
             const Expression *body);
  ~SelectLoop() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  String m_variable_name;
  ArrayList<const Token *> m_words{heap_allocator()};
  bool m_has_in_clause;
  const Expression *m_body;
};

/* A bash indexed-array assignment, NAME=(words) or the appending NAME+=(words).
   The element words expand with field splitting and globbing at run time, the
   way bash builds the array, then they are stored under the name. */
class ArrayAssignCommand : public Command
{
public:
  ArrayAssignCommand(SourceLocation location, StringView name,
                     ArrayList<const Token *> elements, bool is_append);
  ~ArrayAssignCommand() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;

  fn append_to(usize d, String &f, bool duplicate) throws -> void override;
  fn redirect_to(usize d, String &f, bool duplicate) throws -> void override;

  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  String m_name;
  ArrayList<const Token *> m_elements{heap_allocator()};
  bool m_is_append;
};

/* A compound command with trailing redirections, such as { cmd; } >file or
   (cmd) 2>&1. A compound command runs in the shell process, so the redirections
   are applied to the shell's own descriptors around the child and restored
   afterward, rather than handed to a spawned process. */
class RedirectedCommand : public Command
{
public:
  RedirectedCommand(SourceLocation location, const Command *child,
                    ArrayList<Redirection> &&redirections);
  ~RedirectedCommand() override;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

  fn append_to(usize d, String &f, bool duplicate) throws -> void override;
  fn redirect_to(usize d, String &f, bool duplicate) throws -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const Command *m_child;
  ArrayList<Redirection> m_redirections{heap_allocator()};
};

class FunctionDefinition : public CompoundCommand
{
public:
  FunctionDefinition(SourceLocation location, StringView name,
                     const Expression *body);
  ~FunctionDefinition() override;

  pure fn name() const wontthrow -> const String &;
  pure fn body() const wontthrow -> const Expression *;

  fn to_string() const throws -> String override;
  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn analyze(AnalysisContext &actx, bool is_unconditional) const throws
      -> void override;
  fn register_defined_functions(AnalysisContext &actx) const throws
      -> void override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  String m_name;
  const Expression *m_body;
};

class ConstantNumber : public Expression
{
public:
  ConstantNumber(SourceLocation location, i64 value);
  ~ConstantNumber() override;

  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn to_string() const throws -> String override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const i64 m_value;
};

class ConstantString : public Expression
{
public:
  ConstantString(SourceLocation location, StringView value);
  ~ConstantString() override;

  fn to_ast_string(usize layer = 0) const throws -> String override;
  fn to_string() const throws -> String override;

protected:
  fn evaluate_impl(EvalContext &cxt) const throws -> i64 override;

  const String m_value;
};

class UnaryExpression : public Expression
{
public:
  UnaryExpression(SourceLocation location, const Expression *rhs);
  ~UnaryExpression() override;

  fn to_ast_string(usize layer = 0) const throws -> String override;

protected:
  const Expression *m_rhs;
};

#define UNARY_EXPRESSION_STRUCT(e)                                             \
  class e : public UnaryExpression                                             \
  {                                                                            \
  public:                                                                      \
    e(SourceLocation location, const Expression *rhs);                         \
    String to_string() const throws override;                                  \
                                                                               \
  protected:                                                                   \
    i64 evaluate_impl(EvalContext &cxt) const throws override;                 \
  }

UNARY_EXPRESSION_STRUCT(Negate);
UNARY_EXPRESSION_STRUCT(Unnegate);
UNARY_EXPRESSION_STRUCT(LogicalNot);
UNARY_EXPRESSION_STRUCT(BinaryComplement);

class BinaryExpression : public Expression
{
public:
  BinaryExpression(SourceLocation location, const Expression *lhs,
                   const Expression *rhs);
  ~BinaryExpression() override;

  fn to_ast_string(usize layer = 0) const throws -> String override;

protected:
  const Expression *m_lhs;
  const Expression *m_rhs;
};

#define BINARY_EXPRESSION_STRUCT(e)                                            \
  class e : public BinaryExpression                                            \
  {                                                                            \
  public:                                                                      \
    e(SourceLocation location, const Expression *lhs, const Expression *rhs);  \
    String to_string() const throws override;                                  \
                                                                               \
  protected:                                                                   \
    i64 evaluate_impl(EvalContext &cxt) const throws override;                 \
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
