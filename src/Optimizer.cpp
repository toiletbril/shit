#include "Optimizer.hpp"

#include "Builtin.hpp"
#include "Common.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"

namespace shit {

namespace optimizer {

namespace {

/* A byte that may appear in a provably-constant arithmetic expression. The set
   is digits, whitespace, parentheses, and the operator characters. It excludes
   every letter and underscore, so no variable name and no hex prefix is folded,
   which keeps the fold to plain decimal constants the analyze pass can prove.
 */
pure fn is_constant_arithmetic_byte(char byte) wontthrow -> bool
{
  if (lexer::is_number(byte)) return true;
  switch (byte) {
  case ' ':
  case '\t':
  case '\n':
  case '\r':
  case '(':
  case ')':
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case '&':
  case '|':
  case '^':
  case '~':
  case '!':
  case '<':
  case '>':
  case '=':
  case '?':
  case ':': return true;
  default: return false;
  }
}

/* The first byte of a C-style identifier, a letter or an underscore. The
   arithmetic constant-propagation scanner uses this to find a variable name
   inside an arithmetic expression. */
pure fn is_identifier_start(char byte) wontthrow -> bool
{
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         byte == '_';
}

/* A non-leading byte of a C-style identifier, a letter, a digit, or an
   underscore. */
pure fn is_identifier_continuation(char byte) wontthrow -> bool
{
  return is_identifier_start(byte) || (byte >= '0' && byte <= '9');
}

/* True when the text is a non-empty run of bytes that all form a plain decimal
   integer, an optional leading minus then digits. A recorded constant variable
   is only substituted into arithmetic when its value has this shape, so the
   substitution cannot inject an operator or another name. */
pure fn is_plain_integer_literal(StringView text) wontthrow -> bool
{
  if (text.length == 0) return false;
  usize start = 0;
  if (text[0] == '-') {
    if (text.length == 1) return false;
    start = 1;
  }
  for (usize i = start; i < text.length; i++) {
    if (!lexer::is_number(text[i])) return false;
  }
  return true;
}

/* True when the text is a valid plain variable name, a C-style identifier with
   no parameter expansion modifier. A VariableReference segment whose text is
   such a name is a bare $name reference the constant-propagation rule may
   replace, while a name carrying a modifier such as x:-y is left alone. */
pure fn is_plain_variable_name(StringView name) wontthrow -> bool
{
  if (name.length == 0) return false;
  if (!is_identifier_start(name[0])) return false;
  for (usize i = 1; i < name.length; i++) {
    if (!is_identifier_continuation(name[i])) return false;
  }
  return true;
}

/* True when a token is a bare unquoted $name reference that field-splits at run
   time. An unquoted operand splits on IFS, so the recorded value of $name is
   not the single test argument the run actually sees, and the test verdict must
   not fold from it. A quoted "$name" carries the same VariableReference segment
   with is_in_double_quotes set, which is_split_eligible reports as not split,
   so it still folds. The check mirrors the unquoted-variable test warning. */
fn is_split_eligible_variable_operand(const Token *token) wontthrow -> bool
{
  if (token == nullptr) return false;
  if (token->kind() != Token::Kind::Word) return false;

  let const &word = static_cast<const tokens::WordToken *>(token)->word();
  if (word.segments.count() != 1) return false;

  const WordSegment &segment = word.segments[0];
  return segment.kind == WordSegment::Kind::VariableReference &&
         segment.is_split_eligible();
}

/* The constant value of a test operand, declining an unquoted $name reference
   that field-splits at run time. The fold judges a quoted or literal operand by
   its propagated value, while an unquoted variable operand keeps its run-time
   splitting and is left unfolded. */
fn propagated_test_operand_value(const Token *token,
                                 const AnalysisContext &actx) throws
    -> Maybe<String>
{
  if (is_split_eligible_variable_operand(token)) return None;
  return propagated_literal_word_value(token, actx);
}

/* The constant verdict of a literal test command, the arguments after the test
   or [ word with the trailing ] of the bracket form already removed. Some(true)
   or Some(false) for the simplest forms the fold can prove, None otherwise. The
   operand value comes through propagated_test_operand_value, so a $name operand
   recorded as a constant is judged by its recorded value unless it
   field-splits.
 */
fn constant_test_verdict(const ArrayList<const Token *> &operands,
                         const AnalysisContext &actx) throws -> Maybe<bool>
{
  /* test with no operand is false, test X is true when X is non-empty. */
  if (operands.is_empty()) return Maybe<bool>{false};

  if (operands.count() == 1) {
    let const value = propagated_test_operand_value(operands[0], actx);
    if (!value.has_value()) return None;
    return Maybe<bool>{!value->is_empty()};
  }

  if (operands.count() == 2) {
    let const op = propagated_test_operand_value(operands[0], actx);
    let const arg = propagated_test_operand_value(operands[1], actx);
    if (!op.has_value() || !arg.has_value()) return None;
    if (*op == "-n") return Maybe<bool>{!arg->is_empty()};
    if (*op == "-z") return Maybe<bool>{arg->is_empty()};
    return None;
  }

  if (operands.count() == 3) {
    let const lhs = propagated_test_operand_value(operands[0], actx);
    let const op = propagated_test_operand_value(operands[1], actx);
    let const rhs = propagated_test_operand_value(operands[2], actx);
    if (!lhs.has_value() || !op.has_value() || !rhs.has_value()) return None;
    if (*op == "=") return Maybe<bool>{*lhs == *rhs};
    if (*op == "!=") return Maybe<bool>{!(*lhs == *rhs)};
    return None;
  }

  return None;
}

} /* namespace */

fn literal_word_value(const Word &word) throws -> Maybe<String>
{
  String value{};
  for (const WordSegment &segment : word.segments) {
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      value.append(segment.text.view());
      break;
    case WordSegment::Kind::UnquotedText:
      /* An unquoted segment expands a glob metacharacter and a leading tilde,
         so its bytes are only a plain constant when it holds neither. A glob
         char is one of '*', '?', or '[', which is_expandable_char reports, and
         a leading '~' opens tilde expansion. The fold accepts the remaining
         literal bytes, which a test operand compares directly. */
      for (usize i = 0; i < segment.text.count(); i++) {
        if (lexer::is_expandable_char(segment.text[i])) return None;
      }
      if (!segment.text.is_empty() && segment.text[0] == '~') return None;
      value.append(segment.text.view());
      break;
    default: return None;
    }
  }
  return value;
}

fn literal_word_value(const Token *token) throws -> Maybe<String>
{
  if (token == nullptr) return None;
  if (token->kind() != Token::Kind::Word) return None;
  return literal_word_value(
      static_cast<const tokens::WordToken *>(token)->word());
}

namespace {

/* The literal program text of a command word, the concatenation of its bytes
   when every segment is literal, quoted, or unquoted text. Unlike the operand
   extractor this keeps a glob metacharacter, so the bracket word [ reads as the
   two-byte name the test recognition compares. None when a segment holds a
   variable, a command substitution, or an arithmetic expansion, since the name
   is then only known at run time. */
fn command_word_literal(const Token *token) throws -> Maybe<String>
{
  if (token == nullptr) return None;
  if (token->kind() != Token::Kind::Word) return None;

  let const &word = static_cast<const tokens::WordToken *>(token)->word();
  String name{};
  for (const WordSegment &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::LiteralText &&
        segment.kind != WordSegment::Kind::DoubleQuotedText &&
        segment.kind != WordSegment::Kind::UnquotedText)
      return None;
    name.append(segment.text.view());
  }
  return name;
}

} /* namespace */

fn plain_variable_reference_name(const Token *token) wontthrow
    -> Maybe<StringView>
{
  if (token == nullptr) return None;
  if (token->kind() != Token::Kind::Word) return None;

  let const &word = static_cast<const tokens::WordToken *>(token)->word();
  if (word.segments.count() != 1) return None;

  const WordSegment &segment = word.segments[0];
  if (segment.kind != WordSegment::Kind::VariableReference) return None;
  let const name = segment.text.view();
  if (!is_plain_variable_name(name)) return None;
  return name;
}

fn propagated_literal_word_value(const Token *token,
                                 const AnalysisContext &actx) throws
    -> Maybe<String>
{
  /* A bare $name reference reads its recorded constant. The literal form is
     tried first, since a word that is already literal carries no variable. */
  let const literal = literal_word_value(token);
  if (literal.has_value()) return literal;

  let const name = plain_variable_reference_name(token);
  if (!name.has_value()) return None;
  if (const String *recorded = actx.constant_variables.find(*name))
    return String{*recorded};
  return None;
}

fn try_fold_constant_arithmetic(StringView expression) wontthrow -> Maybe<i64>
{
  if (expression.length == 0) return None;

  for (usize i = 0; i < expression.length; i++) {
    if (!is_constant_arithmetic_byte(expression[i])) return None;
  }

  /* The expression holds no variable and no assignment, so the constant
     evaluator runs without a context. A malformed constant, such as a division
     by zero, throws and leaves the segment unfolded for the runtime path to
     report at the caret. */
  try {
    return evaluate_constant_arithmetic(expression);
  } catch (...) {
    return None;
  }
}

fn try_fold_arithmetic_with_constants(StringView expression,
                                      const AnalysisContext &actx) wontthrow
    -> Maybe<i64>
{
  if (expression.length == 0) return None;
  if (actx.constant_variables.count() == 0) return None;

  /* Rebuild the expression with each identifier replaced by its recorded
     integer constant. An identifier that is not a recorded plain-integer
     constant aborts the fold, since its value is only known at run time. The
     rewritten text holds only constant bytes and feeds the constant evaluator.
   */
  try {
    String rewritten{};
    usize i = 0;
    while (i < expression.length) {
      const char byte = expression[i];
      if (!is_identifier_start(byte)) {
        rewritten.append(StringView{&expression.data[i], 1});
        i++;
        continue;
      }

      usize start = i;
      while (i < expression.length && is_identifier_continuation(expression[i]))
        i++;
      let const name = StringView{&expression.data[start], i - start};

      const String *recorded = actx.constant_variables.find(name);
      if (recorded == nullptr) return None;
      if (!is_plain_integer_literal(recorded->view())) return None;
      rewritten.append(recorded->view());
    }

    for (usize j = 0; j < rewritten.count(); j++) {
      if (!is_constant_arithmetic_byte(rewritten[j])) return None;
    }
    return evaluate_constant_arithmetic(rewritten.view());
  } catch (...) {
    return None;
  }
}

fn simple_command_static_verdict(const ArrayList<const Token *> &args,
                                 const AnalysisContext &actx) throws
    -> Maybe<bool>
{
  if (args.is_empty()) return None;

  /* The program name is matched against the fixed builtin names true, false,
     test, and the bracket word [. The bracket holds a glob metacharacter that
     the operand extractor rejects, so the name uses the word's literal text
     directly, which keeps the [ for the test recognition. */
  let const name = command_word_literal(args[0]);
  if (!name.has_value()) return None;

  /* A function or an alias of the same name shadows the builtin, so the program
     word no longer names the constant builtin and the fold declines it. */
  if (actx.defined_functions.contains(name->view())) return None;
  if (actx.known_aliases.contains(name->view())) return None;

  /* true and : always succeed, false always fails, each with no side effect. */
  if (*name == "true" || *name == ":") {
    if (args.count() != 1) return None;
    return Maybe<bool>{true};
  }
  if (*name == "false") {
    if (args.count() != 1) return None;
    return Maybe<bool>{false};
  }

  /* A literal test or [ with only plain operands decides at analyze time. The
     bracket form must close with a literal ], which is dropped before the
     operands are judged. */
  if (*name == "test" || *name == "[") {
    ArrayList<const Token *> operands{heap_allocator()};
    usize last = args.count();
    if (*name == "[") {
      let const closing = literal_word_value(args[args.count() - 1]);
      if (!closing.has_value() || *closing != "]") return None;
      last -= 1;
    }
    for (usize i = 1; i < last; i++)
      operands.push(args[i]);
    try {
      return constant_test_verdict(operands, actx);
    } catch (...) {
      return None;
    }
  }

  return None;
}

pure fn word_segment_has_glob_metacharacter(
    const WordSegment &segment) wontthrow -> bool
{
  for (usize i = 0; i < segment.text.count(); i++) {
    const char c = segment.text[i];
    if (c == '*' || c == '?' || c == '[') return true;
    /* An extended-glob opener such as @( carries no plain * or ? but still
       globs against names, so it keeps the word off the literal fast path. The
       matcher leaves it literal when extglob is off, so this only costs a rare
       word a directory scan. */
    if ((c == '+' || c == '@' || c == '!') && i + 1 < segment.text.count() &&
        segment.text[i + 1] == '(')
      return true;
  }
  return false;
}

pure fn classify_plain_literal(const Word &word) wontthrow -> Word::PlainLiteral
{
  if (word.segments.is_empty()) return Word::PlainLiteral::NotPlain;

  /* A single unquoted segment splits and globs, so it qualifies only without a
     glob metacharacter and without a leading tilde that tilde expansion would
     rewrite. The IFS question is left to the evaluator. */
  if (word.segments.count() == 1 &&
      word.segments[0].kind == WordSegment::Kind::UnquotedText)
  {
    const WordSegment &only = word.segments[0];
    if (word_segment_has_glob_metacharacter(only))
      return Word::PlainLiteral::NotPlain;
    if (!only.text.is_empty() && only.text[0] == '~')
      return Word::PlainLiteral::NotPlain;
    return Word::PlainLiteral::PlainUnquotedOneSegment;
  }

  /* Literal and double-quoted text never splits, globs, or expands, so a word
     built only from those is one field of its concatenated text. */
  for (const WordSegment &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::LiteralText &&
        segment.kind != WordSegment::Kind::DoubleQuotedText)
      return Word::PlainLiteral::NotPlain;
  }
  return Word::PlainLiteral::PlainNoSplit;
}

namespace {

/* Fold every constant arithmetic expansion in a word once. A segment is folded
   when its bytes are already constant, or when constant propagation replaces
   each identifier with a recorded integer constant and the result is provably
   constant. The evaluator then reads the cached value instead of re-parsing the
   arithmetic on every expansion. Returns true when this call newly folds a
   segment, so the driver counts the rule as having fired. */
fn fold_constant_arithmetic_in_word(const Word &word,
                                    const AnalysisContext &actx) wontthrow
    -> bool
{
  bool did_fold = false;
  for (const WordSegment &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::ArithmeticExpansion) continue;
    if (segment.folded_arithmetic_result.has_value()) continue;

    let result = try_fold_constant_arithmetic(segment.text.view());
    if (!result.has_value())
      result = try_fold_arithmetic_with_constants(segment.text.view(), actx);
    if (result.has_value()) {
      segment.folded_arithmetic_result = result;
      did_fold = true;
    }
  }
  return did_fold;
}

fn fold_constant_arithmetic_in_token(const Token *token,
                                     const AnalysisContext &actx) wontthrow
    -> bool
{
  if (token == nullptr) return false;
  if (token->kind() != Token::Kind::Word) return false;
  return fold_constant_arithmetic_in_word(
      static_cast<const tokens::WordToken *>(token)->word(), actx);
}

/* RULE constant-arithmetic folding. A constant $((...)) in a command word, an
   assignment value, or a prefix assignment is folded to its decimal result
   once. The rule also folds an expression whose identifiers are all recorded
   integer constants, so x=2; echo $((x+x)) yields 4. */
fn rule_fold_constant_arithmetic(const Expression *node,
                                 AnalysisContext &actx) throws -> bool
{
  if (const expressions::AssignCommand *assign = node->as_assign_command()) {
    return fold_constant_arithmetic_in_word(assign->assignment()->value_word(),
                                            actx);
  }

  if (const expressions::SimpleCommand *cmd = node->as_simple_command()) {
    bool did_fold = false;
    for (const Token *t : cmd->args()) {
      if (fold_constant_arithmetic_in_token(t, actx)) did_fold = true;
    }
    for (const expressions::prefix_assignment &var : cmd->local_vars()) {
      if (fold_constant_arithmetic_in_word(var.value, actx)) did_fold = true;
    }
    return did_fold;
  }

  return false;
}

/* RULE dead-branch elimination. When every condition of an if up to the chosen
   one has a statically-decidable verdict, the branch the if takes is recorded,
   so the evaluator runs that body without testing any condition. A condition
   that always succeeds selects its body, one that always fails moves on, and
   the first undecidable condition stops the fold. */
fn rule_dead_branch_elimination(const Expression *node,
                                AnalysisContext &actx) throws -> bool
{
  const expressions::IfClause *clause = node->as_if_clause();
  if (clause == nullptr) return false;
  if (clause->has_folded_branch()) return false;

  for (usize i = 0; i < clause->branches().count(); i++) {
    let const verdict =
        clause->branches()[i].condition->try_static_condition_verdict(actx);
    if (!verdict.has_value()) return false;
    if (*verdict) {
      clause->set_folded_branch(i);
      return true;
    }
  }
  /* Every condition failed, so the else body runs, or nothing when there is
     none. An index past the last branch names that outcome. */
  clause->set_folded_branch(clause->branches().count());
  return true;
}

/* RULE loop elimination. A while whose condition is statically false or an
   until whose condition is statically true never lets the body run, so the
   whole loop is recorded as skipped and the evaluator yields 0 without testing
   the condition. A while true or an until false is infinite and stays
   unfolded. */
fn rule_loop_elimination(const Expression *node, AnalysisContext &actx) throws
    -> bool
{
  const expressions::WhileLoop *loop = node->as_while_loop();
  if (loop == nullptr) return false;
  if (loop->is_folded_to_skip()) return false;

  let const verdict = loop->condition()->try_static_condition_verdict(actx);
  if (!verdict.has_value()) return false;

  let const body_would_run =
      loop->is_until() ? (*verdict == false) : (*verdict == true);
  if (body_would_run) return false;

  loop->set_folded_to_skip();
  return true;
}

/* The transformation rules, applied to each node in order on every pass. A rule
   matches its own node kind and does nothing on a node it does not own, so the
   driver runs the whole list against every node. */
using OptimizationRule = fn(const Expression *, AnalysisContext &) throws->bool;

OptimizationRule *const OPTIMIZATION_RULES[] = {
    rule_fold_constant_arithmetic,
    rule_dead_branch_elimination,
    rule_loop_elimination,
};

/* The pass cap that bounds the fixpoint loop. A correct rule set reaches a
   fixpoint in one pass over a node, since a second pass only re-confirms the
   first. The cap is a guard against a rule that reports a change without making
   progress, so a buggy rule cannot loop the driver forever. */
constexpr usize MAX_OPTIMIZATION_PASSES = 8;

} /* namespace */

fn optimize_node(const Expression *node, AnalysisContext &actx) throws -> void
{
  ASSERT(node != nullptr);

  for (usize pass = 0; pass < MAX_OPTIMIZATION_PASSES; pass++) {
    bool any_rule_fired = false;
    for (OptimizationRule *rule : OPTIMIZATION_RULES) {
      if (rule(node, actx)) any_rule_fired = true;
    }
    if (!any_rule_fired) return;
  }
}

} /* namespace optimizer */

} /* namespace shit */
