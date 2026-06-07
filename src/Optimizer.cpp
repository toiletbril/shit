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

/* The fully literal value of a word, the concatenation of its text when every
   segment is literal or quoted text with no expansion. None when any segment is
   a variable, a command substitution, or an arithmetic expansion, since the
   value is then only known at run time. Unlike static_command_name this keeps a
   glob metacharacter, since a test operand compares bytes and does not glob. */
fn literal_word_value(const Token *token) throws -> Maybe<String>
{
  if (token == nullptr) return None;
  if (token->kind() != Token::Kind::Word) return None;

  let const &word = static_cast<const tokens::WordToken *>(token)->word();

  String value{};
  for (const WordSegment &segment : word.segments) {
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText:
    case WordSegment::Kind::DoubleQuotedText:
      value.append(segment.text.view());
      break;
    case WordSegment::Kind::UnquotedText:
      /* An unquoted segment may hold a glob or a tilde, which expand. The test
         operands the fold accepts are plain bytes, so a live glob char makes
         the word non-constant and the fold declines it. */
      for (usize i = 0; i < segment.text.count(); i++) {
        if (lexer::is_expandable_char(segment.text[i])) return None;
      }
      if (segment.has_live_glob_chars()) return None;
      value.append(segment.text.view());
      break;
    default: return None;
    }
  }
  return value;
}

/* The constant verdict of a literal test command, the arguments after the test
   or [ word with the trailing ] of the bracket form already removed. Some(true)
   or Some(false) for the simplest forms the fold can prove, None otherwise. */
fn constant_test_verdict(const ArrayList<const Token *> &operands) throws
    -> Maybe<bool>
{
  /* test with no operand is false, test X is true when X is non-empty. */
  if (operands.is_empty()) return Maybe<bool>{false};

  if (operands.count() == 1) {
    let const value = literal_word_value(operands[0]);
    if (!value.has_value()) return None;
    return Maybe<bool>{!value->is_empty()};
  }

  if (operands.count() == 2) {
    let const op = literal_word_value(operands[0]);
    let const arg = literal_word_value(operands[1]);
    if (!op.has_value() || !arg.has_value()) return None;
    if (*op == "-n") return Maybe<bool>{!arg->is_empty()};
    if (*op == "-z") return Maybe<bool>{arg->is_empty()};
    return None;
  }

  if (operands.count() == 3) {
    let const lhs = literal_word_value(operands[0]);
    let const op = literal_word_value(operands[1]);
    let const rhs = literal_word_value(operands[2]);
    if (!lhs.has_value() || !op.has_value() || !rhs.has_value()) return None;
    if (*op == "=") return Maybe<bool>{*lhs == *rhs};
    if (*op == "!=") return Maybe<bool>{!(*lhs == *rhs)};
    return None;
  }

  return None;
}

} /* namespace */

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

fn simple_command_static_verdict(const ArrayList<const Token *> &args,
                                 const AnalysisContext &actx) throws
    -> Maybe<bool>
{
  if (args.is_empty()) return None;

  let const name = literal_word_value(args[0]);
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
      return constant_test_verdict(operands);
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

} /* namespace optimizer */

} /* namespace shit */
