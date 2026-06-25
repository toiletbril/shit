#include "Optimizer.hpp"

#include "Builtin.hpp"
#include "Common.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"

namespace shit {

namespace optimizer {

namespace {

/* A byte that may appear in a provably-constant arithmetic expression. It
   excludes every letter and underscore, so no variable name and no hex prefix
   is folded, which keeps the fold to plain decimal constants. */
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

/* The first byte of a C-style identifier, a letter or an underscore. */
pure fn is_identifier_start(char byte) wontthrow -> bool
{
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         byte == '_';
}

pure fn is_identifier_continuation(char byte) wontthrow -> bool
{
  return is_identifier_start(byte) || (byte >= '0' && byte <= '9');
}

/* True when the text is a plain decimal integer, an optional leading minus then
   digits. A recorded constant is only substituted into arithmetic when its
   value has this shape, so it cannot inject an operator or another name. */
pure fn is_plain_integer_literal(StringView text) wontthrow -> bool
{
  if (text.length == 0) return false;
  usize start_position = 0;
  if (text[0] == '-') {
    if (text.length == 1) return false;
    start_position = 1;
  }
  for (usize i = start_position; i < text.length; i++) {
    if (!lexer::is_number(text[i])) return false;
  }
  return true;
}

/* True when the text is a C-style identifier with no parameter expansion
   modifier. A name carrying a modifier such as x:-y is left alone. */
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
   time. Its recorded value is not the single test argument the run sees, so the
   verdict must not fold from it. A quoted "$name" still folds. */
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

fn propagated_test_operand_value(const Token *token,
                                 const AnalysisContext &actx) throws
    -> Maybe<String>
{
  if (is_split_eligible_variable_operand(token)) {
    LOG(All, "declining the test operand fold, the unquoted "
             "variable splits at run time");
    return None;
  }
  return propagated_literal_word_value(token, actx);
}

/* The constant verdict of a literal test command, the arguments after the test
   or [ word. None when not provable. */
fn constant_test_verdict(const ArrayList<const Token *> &operands,
                         const AnalysisContext &actx) throws -> Maybe<bool>
{
  if (operands.is_empty()) return Maybe<bool>{false};

  if (operands.count() == 1) {
    let const value = propagated_test_operand_value(operands[0], actx);
    if (!value.has_value()) return None;
    return Maybe<bool>{!value->is_empty()};
  }

  if (operands.count() == 2) {
    let const op = propagated_test_operand_value(operands[0], actx);
    let const arg = propagated_test_operand_value(operands[1], actx);
    if (!op.has_value() || !arg.has_value()) {
      return None;
    }
    if (*op == "-n") return Maybe<bool>{!arg->is_empty()};
    if (*op == "-z") return Maybe<bool>{arg->is_empty()};
    return None;
  }

  if (operands.count() == 3) {
    let const lhs = propagated_test_operand_value(operands[0], actx);
    let const op = propagated_test_operand_value(operands[1], actx);
    let const rhs = propagated_test_operand_value(operands[2], actx);
    if (!lhs.has_value() || !op.has_value() || !rhs.has_value()) {
      return None;
    }
    if (*op == "=") return Maybe<bool>{*lhs == *rhs};
    if (*op == "!=") return Maybe<bool>{*lhs != *rhs};
    return None;
  }

  return None;
}

/* The names of commands proven not to mutate the shell environment. A command
   outside the table is assumed to write a variable, so the constant table is
   forgotten across it. The env-mutating builtins, read, and printf -v fall
   through to the conservative clear. */
inline constexpr StaticStringMap<bool>::entry ENVIRONMENT_NEUTRAL_ENTRIES[] = {
    {SSK("echo"),     true},
    {SSK("true"),     true},
    {SSK("false"),    true},
    {SSK(":"),        true},
    {SSK("test"),     true},
    {SSK("["),        true},
    {SSK("pwd"),      true},
    {SSK("which"),    true},
    {SSK("whoami"),   true},
    {SSK("basename"), true},
    {SSK("dirname"),  true},
    {SSK("seq"),      true},
    {SSK("expr"),     true},
    {SSK("id"),       true},
    {SSK("hostname"), true},
    {SSK("uname"),    true},
    {SSK("date"),     true},
    {SSK("arch"),     true},
    {SSK("tty"),      true},
};

inline constexpr StaticStringMap<bool> ENVIRONMENT_NEUTRAL_NAMES{
    ENVIRONMENT_NEUTRAL_ENTRIES, sizeof(ENVIRONMENT_NEUTRAL_ENTRIES) /
                                     sizeof(ENVIRONMENT_NEUTRAL_ENTRIES[0])};

} // namespace

fn command_is_environment_neutral(StringView name) throws -> bool
{
  return ENVIRONMENT_NEUTRAL_NAMES.find(name).has_value();
}

fn literal_word_value(const Word &word) throws -> Maybe<String>
{
  let value = String{};
  for (let const &segment : word.segments) {
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      value.append(segment.text.view());
      break;
    case WordSegment::Kind::UnquotedText:
      /* An unquoted segment expands a glob metacharacter and a leading tilde,
         so its bytes are only a plain constant when it holds neither. */
      for (usize i = 0; i < segment.text.count(); i++) {
        if (lexer::is_expandable_char(segment.text[i])) return None;
      }
      if (!segment.text.is_empty() && segment.text[0] == '~') {
        return None;
      }
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

/* The literal program text of a command word. Unlike the operand extractor this
   keeps a glob metacharacter, so the bracket word [ reads as its two-byte name.
   None when a segment holds an expansion. */
fn command_word_literal(const Token *token) throws -> Maybe<String>
{
  if (token == nullptr) return None;
  if (token->kind() != Token::Kind::Word) return None;

  let const &word = static_cast<const tokens::WordToken *>(token)->word();
  let name = String{};
  for (let const &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::LiteralText &&
        segment.kind != WordSegment::Kind::DoubleQuotedText &&
        segment.kind != WordSegment::Kind::UnquotedText)
      return None;
    name.append(segment.text.view());
  }
  return name;
}

} // namespace

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
  let const literal = literal_word_value(token);
  if (literal.has_value()) return literal;

  let const name = plain_variable_reference_name(token);
  if (!name.has_value()) return None;
  if (const String *recorded = actx.constant_variables.find(*name);
      recorded != nullptr)
  {
    LOG(All, "reading the recorded constant '%.*s' = '%s'",
        static_cast<int>(name->length), name->data, recorded->c_str());
    return recorded->clone();
  }
  return None;
}

fn try_fold_constant_arithmetic(StringView expression) wontthrow -> Maybe<i64>
{
  if (expression.length == 0) return None;

  for (usize i = 0; i < expression.length; i++) {
    if (!is_constant_arithmetic_byte(expression[i])) return None;
  }

  /* The expression holds no variable, so the constant evaluator runs without a
     context. A malformed constant such as a division by zero throws and leaves
     the segment for the runtime path. */
  try {
    return evaluate_constant_arithmetic(expression);
  } catch (...) {
    LOG(All,
        "swallowed an arithmetic error while folding '%.*s', leaving the "
        "segment for the runtime path",
        static_cast<int>(expression.length), expression.data);
    return None;
  }
}

fn try_fold_arithmetic_with_constants(StringView expression,
                                      const AnalysisContext &actx) wontthrow
    -> Maybe<i64>
{
  if (expression.length == 0) return None;
  if (actx.constant_variables.count() == 0) return None;

  try {
    let rewritten = String{};
    usize i = 0;
    while (i < expression.length) {
      const char byte = expression[i];
      if (!is_identifier_start(byte)) {
        rewritten.append(StringView{&expression.data[i], 1});
        i++;
        continue;
      }

      usize start_position = i;
      while (i < expression.length && is_identifier_continuation(expression[i]))
        i++;
      let const name =
          StringView{&expression.data[start_position], i - start_position};

      let const recorded = actx.constant_variables.find(name);
      if (recorded == nullptr) {
        LOG(All,
            "skipping the arithmetic fold, '%.*s' is not a recorded constant",
            static_cast<int>(name.length), name.data);
        return None;
      }
      if (!is_plain_integer_literal(recorded->view())) {
        LOG(All,
            "skipping the arithmetic fold, the value of '%.*s' is not a plain "
            "integer",
            static_cast<int>(name.length), name.data);
        return None;
      }
      rewritten.append(recorded->view());
    }

    for (usize j = 0; j < rewritten.count(); j++) {
      if (!is_constant_arithmetic_byte(rewritten[j])) return None;
    }
    return evaluate_constant_arithmetic(rewritten.view());
  } catch (...) {
    LOG(All,
        "swallowed an arithmetic error while folding '%.*s' with constants",
        static_cast<int>(expression.length), expression.data);
    return None;
  }
}

/* Strip the leading and trailing arithmetic whitespace from a span. The span is
   returned narrowed, never widened. */
pure fn trim_arithmetic_whitespace(StringView text) wontthrow -> StringView
{
  usize start_position = 0;
  while (start_position < text.length &&
         (text[start_position] == ' ' || text[start_position] == '\t'))
    start_position++;
  usize end_position = text.length;
  while (end_position > start_position &&
         (text[end_position - 1] == ' ' || text[end_position - 1] == '\t'))
    end_position--;
  return text.substring(start_position)
      .substring_of_length(0, end_position - start_position);
}

/* The constant result of an algebraic identity on a single binary operator. The
   folding rule caches an integer, so only x*0, 0*x, and x-x qualify. The split
   fires only on a parenthesis-free expression, so a nested term cannot hide a
   different operator. A bare variable operand folds only when nounset is
   provably off in the live shell, since reading an unset name under set -u is
   an error this fold would swallow. */
fn try_algebraic_simplify(StringView expression,
                          const AnalysisContext &actx) wontthrow -> Maybe<i64>
{
  let const expr = trim_arithmetic_whitespace(expression);
  if (expr.length == 0) return None;

  /* A parenthesis or a second operator means the operands are not the two plain
     terms this matcher assumes, so it declines rather than guess the grouping.
   */
  usize operator_position = 0;
  usize operator_count = 0;
  bool has_grouping = false;
  for (usize i = 0; i < expr.length; i++) {
    const char byte = expr[i];
    if (byte == '(' || byte == ')') {
      has_grouping = true;
      break;
    }
    /* A leading sign on the whole expression binds as a unary minus. The scan
       starts past position zero to keep this matcher from splitting on it as a
       binary operator. */
    if (i > 0 && (byte == '*' || byte == '-')) {
      operator_position = i;
      operator_count++;
    }
  }

  if (has_grouping || operator_count != 1) return None;

  const char op = expr[operator_position];
  let const lhs = trim_arithmetic_whitespace(
      expr.substring_of_length(0, operator_position));
  let const rhs =
      trim_arithmetic_whitespace(expr.substring(operator_position + 1));
  if (lhs.length == 0 || rhs.length == 0) return None;

  /* Each operand must read as a plain name or a plain integer. A side-effecting
     operand such as x++ would make x - x non-zero, so it is declined. */
  let const operand_is_plain = [](StringView operand) wontthrow -> bool {
    for (usize i = 0; i < operand.length; i++) {
      if (!is_identifier_continuation(operand[i])) return false;
    }
    return true;
  };
  if (!operand_is_plain(lhs) || !operand_is_plain(rhs)) return None;

  /* Under nounset an unset name is an error this fold would hide, so the fold
     proceeds only when a live shell proves nounset off. */
  let const nounset_is_off =
      actx.eval_context != nullptr && !actx.eval_context->error_unset();
  let const operand_reads_variable = [](StringView operand) wontthrow -> bool {
    return operand.length > 0 && is_identifier_start(operand[0]);
  };
  if (!nounset_is_off &&
      (operand_reads_variable(lhs) || operand_reads_variable(rhs)))
  {
    LOG(All, "the algebraic simplify declines a variable operand, nounset may "
             "be on");
    return None;
  }

  if (op == '*') {
    /* A multiply by a literal zero on either side is zero, whatever the other
       operand reads, since the shell's arithmetic has no NaN. */
    if (lhs == StringView{"0"} || rhs == StringView{"0"}) {
      LOG(All, "algebraic simplify '%.*s' to 0 through a zero multiply",
          static_cast<int>(expr.length), expr.data);
      return Maybe<i64>{0};
    }
    return None;
  }

  /* A subtraction of a term from the same term is zero. The two sides match
     byte for byte after the whitespace trim, so a different spelling of the
     same value is left alone. */
  if (op == '-' && lhs == rhs) {
    LOG(All, "algebraic simplify '%.*s' to 0 through x - x",
        static_cast<int>(expr.length), expr.data);
    return Maybe<i64>{0};
  }

  return None;
}

fn simple_command_static_verdict(const ArrayList<const Token *> &args,
                                 const AnalysisContext &actx) throws
    -> Maybe<bool>
{
  if (args.is_empty()) return None;

  /* The bracket word [ holds a glob metacharacter that the operand extractor
     rejects, so the name uses the word's literal text directly, which keeps the
     [ for the test recognition. */
  let const name = command_word_literal(args[0]);
  if (!name.has_value()) return None;

  /* A function or alias of the same name shadows the builtin. */
  if (actx.defined_functions.contains(name->view())) {
    LOG(All, "declining the static verdict, a function shadows '%s'",
        name->c_str());
    return None;
  }
  if (actx.known_aliases.contains(name->view())) {
    LOG(All, "declining the static verdict, an alias shadows '%s'",
        name->c_str());
    return None;
  }

  if (*name == "true" || *name == ":") {
    if (args.count() != 1) return None;
    LOG(All, "the builtin '%s' always succeeds, verdict is true",
        name->c_str());
    return Maybe<bool>{true};
  }
  if (*name == "false") {
    if (args.count() != 1) return None;
    LOG(All, "the builtin 'false' always fails, verdict is false");
    return Maybe<bool>{false};
  }

  /* The bracket form must close with a literal ], dropped before the operands
     are judged. */
  if (*name == "test" || *name == "[") {
    ArrayList<const Token *> operands{heap_allocator()};
    usize last_index = args.count();
    if (*name == "[") {
      let const closing = literal_word_value(args[args.count() - 1]);
      if (!closing.has_value() || *closing != "]") {
        return None;
      }
      last_index -= 1;
    }
    for (usize i = 1; i < last_index; i++)
      operands.push(args[i]);
    try {
      return constant_test_verdict(operands, actx);
    } catch (...) {
      LOG(All, "swallowed an error while judging the literal "
               "test, leaving it unfolded");
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
    if (c == '*' || c == '?' || c == '[') {
      return true;
    }
    /* An extended-glob opener such as @( still globs against names, so it keeps
       the word off the literal fast path. The matcher leaves it literal when
       extglob is off, so this only costs a rare word a directory scan. */
    if ((c == '+' || c == '@' || c == '!') && i + 1 < segment.text.count() &&
        segment.text[i + 1] == '(')
    {
      return true;
    }
  }
  return false;
}

pure fn classify_plain_literal(const Word &word) wontthrow -> Word::PlainLiteral
{
  if (word.segments.is_empty()) return Word::PlainLiteral::NotPlain;

  /* A single unquoted segment splits and globs, so it qualifies only without a
     glob metacharacter and without a leading tilde. The IFS question is left to
     the evaluator. */
  if (word.segments.count() == 1 &&
      word.segments[0].kind == WordSegment::Kind::UnquotedText)
  {
    const WordSegment &only = word.segments[0];
    if (word_segment_has_glob_metacharacter(only))
      return Word::PlainLiteral::NotPlain;
    if (!only.text.is_empty() && only.text[0] == '~') {
      return Word::PlainLiteral::NotPlain;
    }
    return Word::PlainLiteral::PlainUnquotedOneSegment;
  }

  /* Literal and double-quoted text never splits, globs, or expands. */
  for (let const &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::LiteralText &&
        segment.kind != WordSegment::Kind::DoubleQuotedText)
      return Word::PlainLiteral::NotPlain;
  }
  return Word::PlainLiteral::PlainNoSplit;
}

namespace {

/* Fold every constant arithmetic expansion in a word once. A segment is folded
   when its bytes are already constant, or when constant propagation replaces
   each identifier with a recorded integer constant. The evaluator then reads
   the cached value instead of re-parsing the arithmetic on every expansion. */
fn fold_constant_arithmetic_in_word(const Word &word,
                                    AnalysisContext &actx) throws -> bool
{
  bool did_fold = false;
  for (let const &segment : word.segments) {
    if (segment.kind != WordSegment::Kind::ArithmeticExpansion) continue;
    if (segment.folded_arithmetic_result.has_value()) continue;

    let result = try_fold_constant_arithmetic(segment.text.view());
    if (!result.has_value())
      result = try_fold_arithmetic_with_constants(segment.text.view(), actx);
    /* An identity such as x*0 or x-x folds even when the operand reads a
       run-time variable, so it runs after the numeric folds decline. */
    if (!result.has_value())
      result = try_algebraic_simplify(segment.text.view(), actx);
    if (result.has_value()) {
      LOG(All, "folded the constant arithmetic '%.*s' to %lld",
          static_cast<int>(segment.text.view().length),
          segment.text.view().data, static_cast<long long>(*result));
      segment.folded_arithmetic_result = result;
      did_fold = true;
      actx.optimizer_folded_arithmetic++;
      if (actx.should_trace_optimizer)
        actx.trace_optimizer_line(String{"folded constant arithmetic: "} +
                                  String{segment.text.view()} + " = " +
                                  utils::int_to_text(*result));
    }
  }
  return did_fold;
}

fn fold_constant_arithmetic_in_token(const Token *token,
                                     AnalysisContext &actx) throws -> bool
{
  if (token == nullptr) return false;
  if (token->kind() != Token::Kind::Word) return false;
  return fold_constant_arithmetic_in_word(
      static_cast<const tokens::WordToken *>(token)->word(), actx);
}

/* RULE constant-arithmetic folding. A constant $((...)) in a command word, an
   assignment value, or a prefix assignment is folded to its decimal result
   once. The rule also folds an expression whose identifiers are all recorded
   integer constants. */
fn rule_fold_constant_arithmetic(const Expression *node,
                                 AnalysisContext &actx) throws -> bool
{
  if (const expressions::AssignCommand *assign = node->as_assign_command()) {
    return fold_constant_arithmetic_in_word(assign->assignment()->value_word(),
                                            actx);
  }

  if (const expressions::SimpleCommand *cmd = node->as_simple_command()) {
    bool did_fold = false;
    for (let const t : cmd->args()) {
      if (fold_constant_arithmetic_in_token(t, actx)) did_fold = true;
    }
    for (let const &var : cmd->local_vars()) {
      if (fold_constant_arithmetic_in_word(var.value, actx)) did_fold = true;
    }
    return did_fold;
  }

  return false;
}

/* RULE dead-branch elimination. When every condition of an if up to the chosen
   one has a statically-decidable verdict, the branch the if takes is recorded,
   so the evaluator runs that body without testing any condition. The first
   undecidable condition stops the fold. */
fn rule_dead_branch_elimination(const Expression *node,
                                AnalysisContext &actx) throws -> bool
{
  const expressions::IfClause *clause = node->as_if_clause();
  if (clause == nullptr) return false;
  if (clause->has_folded_branch()) return false;

  for (usize i = 0; i < clause->branches().count(); i++) {
    let const verdict =
        clause->branches()[i].condition->try_static_condition_verdict(actx);
    if (!verdict.has_value()) {
      LOG(All,
          "the dead-branch fold stops, condition %zu is not statically "
          "decidable",
          i);
      return false;
    }
    if (*verdict) {
      LOG(All, "dead-branch elimination chose branch %zu", i);
      clause->set_folded_branch(i);
      actx.optimizer_folded_branches++;
      if (actx.should_trace_optimizer)
        actx.trace_optimizer_line(String{"folded if to branch "} +
                                  utils::int_to_text(static_cast<i64>(i)));
      return true;
    }
  }
  /* An index past the last branch names the else body, which runs when every
     condition failed. */
  LOG(All, "every if condition is statically false, folding to the else body");
  clause->set_folded_branch(clause->branches().count());
  actx.optimizer_folded_branches++;
  if (actx.should_trace_optimizer)
    actx.trace_optimizer_line(String{"folded if to the else body"});
  return true;
}

/* RULE loop elimination. A while whose condition is statically false or an
   until whose condition is statically true never lets the body run, so the
   whole loop is recorded as skipped and the evaluator yields 0. A while true or
   an until false is infinite and stays unfolded. */
fn rule_loop_elimination(const Expression *node, AnalysisContext &actx) throws
    -> bool
{
  const expressions::WhileLoop *loop_node = node->as_while_loop();
  if (loop_node == nullptr) return false;
  if (loop_node->is_folded_to_skip()) return false;

  let const verdict =
      loop_node->condition()->try_static_condition_verdict(actx);
  if (!verdict.has_value()) {
    LOG(All,
        "the loop fold declines, the condition is not statically decidable");
    return false;
  }

  let const body_would_run = loop_node->is_until() ? !*verdict : *verdict;
  if (body_would_run) {
    LOG(All,
        "the loop fold declines, the body would run under the static verdict");
    return false;
  }

  LOG(All, "loop elimination folded the %s loop to a skip",
      loop_node->is_until() ? "until" : "while");
  loop_node->set_folded_to_skip();
  actx.optimizer_folded_loops++;
  if (actx.should_trace_optimizer)
    actx.trace_optimizer_line(loop_node->is_until()
                                  ? String{"folded until loop to a skip"}
                                  : String{"folded while loop to a skip"});
  return true;
}

/* RULE compound-body elimination. An if every reachable path of which does
   nothing observable folds to a no-op the evaluator skips whole. The rule fires
   only on the outcome the dead-branch rule already proved, an if folded to the
   else body when there is no else body. */
fn rule_eliminate_compound_body(const Expression *node,
                                AnalysisContext &actx) throws -> bool
{
  const expressions::IfClause *clause = node->as_if_clause();
  if (clause == nullptr) return false;
  if (clause->is_fully_eliminated()) return false;
  if (!clause->has_folded_branch()) return false;

  /* An index at the branch count names the else body, and a null else body
     means the chosen path runs nothing, so the whole if is a proven no-op. */
  if (clause->folded_branch_index() != clause->branches().count()) return false;
  if (clause->otherwise() != nullptr) return false;

  LOG(All, "compound-body elimination folded an if with no taken branch to a "
           "no-op");
  clause->set_fully_eliminated();
  actx.optimizer_eliminated_compounds++;
  if (actx.should_trace_optimizer)
    actx.trace_optimizer_line(String{"eliminated if to a no-op"});
  actx.trace_eliminated_node(node->source_location(),
                             "Eliminated if with no reachable body");
  return true;
}

/* RULE empty for-loop elimination. A for with an explicit in clause and no
   words never iterates, so the whole loop is a no-op whatever the body holds.
 */
fn rule_eliminate_empty_for(const Expression *node,
                            AnalysisContext &actx) throws -> bool
{
  const expressions::ForLoop *loop_node = node->as_for_loop();
  if (loop_node == nullptr) return false;
  if (loop_node->is_fully_eliminated()) return false;

  /* A for without an in clause walks the positional parameters, whose count is
     only known at run time, so an empty static word list there proves nothing.
   */
  if (!loop_node->has_in_clause()) return false;
  if (!loop_node->words().is_empty()) return false;

  LOG(All, "empty for-loop elimination folded a for with an empty in clause to "
           "a no-op");
  loop_node->set_fully_eliminated();
  actx.optimizer_eliminated_compounds++;
  if (actx.should_trace_optimizer)
    actx.trace_optimizer_line(String{"eliminated empty for loop"});
  actx.trace_eliminated_node(node->source_location(),
                             "Eliminated for over an empty list");
  return true;
}

/* RULE C-style for folding. A constant condition clause with no run-time
   variable is folded to its value. A constant non-zero condition runs as an
   infinite loop the way bash reads it, while a constant zero condition folds
   the whole loop to a no-op. */
fn rule_fold_cstyle_for(const Expression *node, AnalysisContext &actx) throws
    -> bool
{
  const expressions::CStyleForLoop *loop_node = node->as_cstyle_for_loop();
  if (loop_node == nullptr) return false;
  if (loop_node->has_folded_condition()) return false;

  /* A blank condition is the for ((;;)) infinite form, which has no value to
     fold. */
  let const condition = loop_node->condition_clause();
  let const trimmed = trim_arithmetic_whitespace(condition);
  if (trimmed.length == 0) return false;

  /* Only a condition with no identifier is folded. The counter the header
     mutates is read in the condition, so inlining a recorded constant would
     freeze the loop at its first verdict. */
  for (usize i = 0; i < trimmed.length; i++) {
    if (is_identifier_start(trimmed[i])) {
      LOG(All, "the c-style-for fold declines, the condition reads a variable");
      return false;
    }
  }

  let const value = try_fold_constant_arithmetic(trimmed);
  if (!value.has_value()) return false;

  loop_node->set_folded_condition(*value);
  LOG(All, "folded the c-style for condition '%.*s' to %lld",
      static_cast<int>(trimmed.length), trimmed.data,
      static_cast<long long>(*value));
  actx.optimizer_folded_arithmetic++;
  if (actx.should_trace_optimizer)
    actx.trace_optimizer_line(String{"folded c-style for condition: "} +
                              String{trimmed} + " = " +
                              utils::int_to_text(*value));

  /* A constant zero condition means the body never runs. The init clause still
     runs once before the condition the way C semantics require, so a non-blank
     init keeps the folded zero condition and the evaluator runs the init then
     skips the body. Only a blank-init loop is a proven no-op. */
  let const init_is_blank =
      trim_arithmetic_whitespace(loop_node->init_clause()).length == 0;
  if (*value == 0 && init_is_blank) {
    loop_node->set_fully_eliminated();
    actx.optimizer_eliminated_compounds++;
    if (actx.should_trace_optimizer)
      actx.trace_optimizer_line(String{"eliminated c-style for loop"});
    actx.trace_eliminated_node(
        node->source_location(),
        "Eliminated c-style for whose condition is zero");
  }
  return true;
}

/* The transformation rules, applied to each node in order on every pass. A rule
   matches its own node kind and does nothing on a node it does not own. */
using OptimizationRule = fn(const Expression *, AnalysisContext &) throws->bool;

OptimizationRule *const OPTIMIZATION_RULES[] = {
    rule_fold_constant_arithmetic, rule_dead_branch_elimination,
    rule_loop_elimination,         rule_eliminate_compound_body,
    rule_eliminate_empty_for,      rule_fold_cstyle_for,
};

/* The pass cap that bounds the fixpoint loop. It guards against a rule that
   reports a change without making progress, so a buggy rule cannot loop the
   driver forever. */
constexpr usize MAX_OPTIMIZATION_PASSES = 8;

} // namespace

fn optimize_node(const Expression *node, AnalysisContext &actx) throws -> void
{
  ASSERT(node != nullptr);

  for (usize pass = 0; pass < MAX_OPTIMIZATION_PASSES; pass++) {
    bool did_any_rule_fire = false;
    for (let rule : OPTIMIZATION_RULES) {
      if (rule(node, actx)) did_any_rule_fire = true;
    }
    if (!did_any_rule_fire) return;
    LOG(All,
        "optimization pass %zu fired a rule, running another "
        "pass over the node",
        pass);
  }
  LOG(All, "the optimizer hit the pass cap of %zu on one node",
      MAX_OPTIMIZATION_PASSES);
}

} // namespace optimizer

} // namespace shit
