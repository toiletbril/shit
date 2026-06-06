#include "Tokens.hpp"

#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"

namespace shit {

Token::Token(SourceLocation location) : m_location(location) {}

pure fn Token::source_location() const wontthrow -> SourceLocation
{
  return m_location;
}

fn Token::operator delete(void *pointer) wontthrow -> void
{
  if (is_arena_pointer(pointer)) return;
  ::operator delete(pointer);
}

cold fn Token::to_ast_string() const throws -> String { return raw_string(); }

pure fn WordSegment::is_split_eligible() const wontthrow -> bool
{
  return kind == Kind::UnquotedText ||
         (kind == Kind::VariableReference && !is_in_double_quotes);
}

pure fn WordSegment::has_live_glob_chars() const wontthrow -> bool
{
  return kind == Kind::UnquotedText;
}

pure fn WordSegment::is_tilde_candidate() const wontthrow -> bool
{
  return kind == Kind::UnquotedText;
}

pure fn Word::is_empty() const wontthrow -> bool { return segments.is_empty(); }

hot fn Word::to_literal_string() const throws -> String
{
  String result{};
  for (const WordSegment &segment : segments) {
    if (segment.kind == WordSegment::Kind::CommandSubstitution) {
      result += "$(";
      result += segment.text;
      result += ")";
      continue;
    }
    if (segment.kind == WordSegment::Kind::ArithmeticExpansion) {
      result += "$((";
      result += segment.text;
      result += "))";
      continue;
    }
    if (segment.kind == WordSegment::Kind::VariableReference) result += '$';
    result += segment.text;
  }
  return result;
}

cold fn Word::to_pretty_string() const throws -> String
{
  String result{"[Word"};
  for (const WordSegment &segment : segments) {
    result += "\n  ";
    switch (segment.kind) {
    case WordSegment::Kind::LiteralText: result += "Literal"; break;
    case WordSegment::Kind::UnquotedText: result += "Unquoted"; break;
    case WordSegment::Kind::DoubleQuotedText: result += "DoubleQuoted"; break;
    case WordSegment::Kind::VariableReference: result += "Variable"; break;
    case WordSegment::Kind::CommandSubstitution:
      result += "CommandSubstitution";
      break;
    case WordSegment::Kind::ArithmeticExpansion:
      result += "ArithmeticExpansion";
      break;
    }
    result += " \"";
    result += segment.text;
    result += '"';
  }
  result += "\n]";
  return result;
}

hot fn Word::get_assignment_split() const throws -> Maybe<word_assignment_split>
{
  if (segments.is_empty()) return shit::None;

  const WordSegment &first = segments[0];
  if (first.kind != WordSegment::Kind::UnquotedText) return shit::None;

  const let equals_position = first.text.find_character('=');
  if (!equals_position.has_value() || *equals_position == 0) return shit::None;

  ASSERT(*equals_position <= first.text.count());

  if (!lexer::is_variable_name_start(first.text[0])) return shit::None;
  for (usize i = 1; i < *equals_position; i++) {
    if (!lexer::is_variable_name(first.text[i])) return shit::None;
  }

  const let name_view = first.text.substring_of_length(0, *equals_position);
  String name{name_view};

  Word value{};
  /* The value always begins with an unquoted segment, even when empty, so that
     FOO= produces one empty field rather than no field at all. */
  value.segments.push(WordSegment{WordSegment::Kind::UnquotedText,
                                  first.text.substring(*equals_position + 1),
                                  false});
  for (usize i = 1; i < segments.count(); i++)
    value.segments.push(segments[i]);

  return word_assignment_split{steal(name), steal(value)};
}

namespace tokens {

#define KEYWORD_TOKEN_DECLS(t, s)                                              \
  t::t(SourceLocation location) : Token(location) {}                           \
  Token::Kind t::kind() const wontthrow { return Token::Kind::t; }             \
  Token::Flags t::flags() const wontthrow { return Token::Flag::Keyword; }     \
  String t::raw_string() const throws { return s; }

KEYWORD_TOKEN_DECLS(If, "if");
KEYWORD_TOKEN_DECLS(Then, "then");
KEYWORD_TOKEN_DECLS(Else, "else");
KEYWORD_TOKEN_DECLS(Elif, "elif");
KEYWORD_TOKEN_DECLS(Fi, "fi");
KEYWORD_TOKEN_DECLS(For, "for");
KEYWORD_TOKEN_DECLS(While, "while");
KEYWORD_TOKEN_DECLS(Until, "until");
KEYWORD_TOKEN_DECLS(Do, "do");
KEYWORD_TOKEN_DECLS(Done, "done");
KEYWORD_TOKEN_DECLS(Case, "case");
KEYWORD_TOKEN_DECLS(When, "when");
KEYWORD_TOKEN_DECLS(Esac, "esac");
KEYWORD_TOKEN_DECLS(Time, "time");
KEYWORD_TOKEN_DECLS(Function, "function");

/* The raw string is the literal symbol, so an error shows ')' rather than the
   internal token name. */
#define SENTINEL_TOKEN_DECLS_COMPOUND(t, s)                                    \
  t::t(SourceLocation location) : Token(location) {}                           \
  Token::Kind t::kind() const wontthrow { return Token::Kind::t; }             \
  Token::Flags t::flags() const wontthrow                                      \
  {                                                                            \
    return Token::Flag::Sentinel | Token::Flag::CompoundList;                  \
  }                                                                            \
  String t::raw_string() const throws { return s; }

SENTINEL_TOKEN_DECLS_COMPOUND(Newline, "newline");
SENTINEL_TOKEN_DECLS_COMPOUND(Semicolon, ";");

#define SENTINEL_TOKEN_DECLS(t, s)                                             \
  t::t(SourceLocation location) : Token(location) {}                           \
  Token::Kind t::kind() const wontthrow { return Token::Kind::t; }             \
  Token::Flags t::flags() const wontthrow { return Token::Flag::Sentinel; }    \
  String t::raw_string() const throws { return s; }

SENTINEL_TOKEN_DECLS(EndOfFile, "end of input");
SENTINEL_TOKEN_DECLS(DoubleSemicolon, ";;");
SENTINEL_TOKEN_DECLS(Dot, ".");

SENTINEL_TOKEN_DECLS(LeftParen, "(");
SENTINEL_TOKEN_DECLS(RightParen, ")");
SENTINEL_TOKEN_DECLS(LeftSquareBracket, "[");
SENTINEL_TOKEN_DECLS(DoubleLeftSquareBracket, "[[");
SENTINEL_TOKEN_DECLS(RightSquareBracket, "]");
SENTINEL_TOKEN_DECLS(DoubleRightSquareBracket, "]]");
SENTINEL_TOKEN_DECLS(LeftBracket, "{");
SENTINEL_TOKEN_DECLS(RightBracket, "}");

Value::Value(SourceLocation location, StringView sv)
    : Token(location), m_value(sv)
{}

fn Value::raw_string() const throws -> String { return m_value; }

Number::Number(SourceLocation location, StringView sv) : Value(location, sv) {}

fn Number::kind() const wontthrow -> Token::Kind { return Token::Kind::Number; }

fn Number::flags() const wontthrow -> Token::Flags
{
  return Token::Flag::Value;
}

Assignment::Assignment(SourceLocation location, StringView key, Word value)
    : Token(location), m_key(key), m_value(steal(value))
{}

fn Assignment::kind() const wontthrow -> Token::Kind
{
  return Token::Kind::Assignment;
}

fn Assignment::flags() const wontthrow -> Token::Flags
{
  return Token::Flag::Special;
}

fn Assignment::raw_string() const throws -> String
{
  String result{m_key};
  result += "=";
  result += m_value.to_literal_string();
  return result;
}

pure fn Assignment::key() const wontthrow -> const String & { return m_key; }

pure fn Assignment::value_word() const wontthrow -> const Word &
{
  return m_value;
}

WordToken::WordToken(SourceLocation location, Word word)
    : Value(location, ""), m_word(steal(word))
{
  m_value = m_word.to_literal_string();
}

fn WordToken::kind() const wontthrow -> Token::Kind
{
  return Token::Kind::Word;
}

fn WordToken::flags() const wontthrow -> Token::Flags
{
  return Token::Flag::Value;
}

pure fn WordToken::word() const wontthrow -> const Word & { return m_word; }

Identifier::Identifier(SourceLocation location, StringView sv)
    : Value(location, sv)
{}

fn Identifier::kind() const wontthrow -> Token::Kind
{
  return Token::Kind::Identifier;
}

fn Identifier::flags() const wontthrow -> Token::Flags
{
  return Token::Flag::Value;
}

Redirection::Redirection(SourceLocation location, StringView what_fd,
                         StringView to_file)
    : Token(location), m_from_fd(what_fd), m_to_file(to_file)
{}

fn Redirection::kind() const wontthrow -> Token::Kind
{
  return Token::Kind::Redirection;
}

fn Redirection::flags() const wontthrow -> Token::Flags
{
  return Token::Flag::Special;
}

pure fn Redirection::from_fd() const wontthrow -> const String &
{
  return m_from_fd;
}

pure fn Redirection::to_file() const wontthrow -> const String &
{
  return m_to_file;
}

Operator::Operator(SourceLocation location) : Token(location) {}

fn Operator::left_precedence() const wontthrow -> u8 { return 0; }

fn Operator::unary_precedence() const wontthrow -> u8 { return 0; }

fn Operator::binary_left_associative() const wontthrow -> bool { return true; }

fn Operator::construct_binary_expression(const Expression *lhs,
                                         const Expression *rhs) const throws
    -> Expression *
{
  unused(lhs);
  unused(rhs);
  unreachable("Invalid binary operator construction of type %d", ENUM(kind()));
}

fn Operator::construct_unary_expression(const Expression *rhs) const throws
    -> Expression *
{
  unused(rhs);
  unreachable("Invalid unary operator construction of type %d", ENUM(kind()));
}

#define BINARY_UNARY_OPERATOR_TOKEN_DECLS(t, s, up, bp, uexpr, bexpr)          \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind t::kind() const wontthrow { return Token::Kind::t; }             \
  Token::Flags t::flags() const wontthrow                                      \
  {                                                                            \
    return Token::Flag::BinaryOperator | Token::Flag::UnaryOperator;           \
  }                                                                            \
  String t::raw_string() const throws { return s; }                            \
  u8 t::left_precedence() const wontthrow { return bp; }                       \
  u8 t::unary_precedence() const wontthrow { return up; }                      \
  Expression *t::construct_binary_expression(                                  \
      const Expression *lhs, const Expression *rhs) const throws               \
  {                                                                            \
    ASSERT(AST_ARENA != nullptr);                                              \
    return AST_ARENA->create<expressions::bexpr>(source_location(), lhs, rhs); \
  }                                                                            \
  Expression *t::construct_unary_expression(const Expression *rhs)             \
      const throws                                                             \
  {                                                                            \
    ASSERT(AST_ARENA != nullptr);                                              \
    return AST_ARENA->create<expressions::uexpr>(source_location(), rhs);      \
  }

BINARY_UNARY_OPERATOR_TOKEN_DECLS(Plus, "+", 13, 11, Unnegate, Add);
BINARY_UNARY_OPERATOR_TOKEN_DECLS(Minus, "-", 13, 11, Negate, Subtract);

#define BINARY_OPERATOR_TOKEN_DECLS_COMPOUND(t, s, bp, bexpr)                  \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind t::kind() const wontthrow { return Token::Kind::t; }             \
  Token::Flags t::flags() const wontthrow                                      \
  {                                                                            \
    return Token::Flag::BinaryOperator | Token::Flag::CompoundList;            \
  }                                                                            \
  String t::raw_string() const throws { return s; }                            \
  u8 t::left_precedence() const wontthrow { return bp; }                       \
  Expression *t::construct_binary_expression(                                  \
      const Expression *lhs, const Expression *rhs) const throws               \
  {                                                                            \
    ASSERT(AST_ARENA != nullptr);                                              \
    return AST_ARENA->create<expressions::bexpr>(source_location(), lhs, rhs); \
  }

#define BINARY_OPERATOR_TOKEN_DECLS(t, s, bp, bexpr)                           \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind t::kind() const wontthrow { return Token::Kind::t; }             \
  Token::Flags t::flags() const wontthrow                                      \
  {                                                                            \
    return Token::Flag::BinaryOperator;                                        \
  }                                                                            \
  String t::raw_string() const throws { return s; }                            \
  u8 t::left_precedence() const wontthrow { return bp; }                       \
  Expression *t::construct_binary_expression(                                  \
      const Expression *lhs, const Expression *rhs) const throws               \
  {                                                                            \
    ASSERT(AST_ARENA != nullptr);                                              \
    return AST_ARENA->create<expressions::bexpr>(source_location(), lhs, rhs); \
  }

BINARY_OPERATOR_TOKEN_DECLS_COMPOUND(DoublePipe, "||", 4, LogicalOr);
BINARY_OPERATOR_TOKEN_DECLS_COMPOUND(Ampersand, "&", 7, BinaryAnd);
BINARY_OPERATOR_TOKEN_DECLS_COMPOUND(DoubleAmpersand, "&&", 4, LogicalAnd);

BINARY_OPERATOR_TOKEN_DECLS(Slash, "/", 12, Divide);
BINARY_OPERATOR_TOKEN_DECLS(Asterisk, "*", 12, Multiply);
BINARY_OPERATOR_TOKEN_DECLS(Percent, "%", 12, Module);
BINARY_OPERATOR_TOKEN_DECLS(Greater, ">", 8, GreaterThan);
BINARY_OPERATOR_TOKEN_DECLS(DoubleGreater, ">>", 8, RightShift);
BINARY_OPERATOR_TOKEN_DECLS(GreaterEquals, ">=", 8, GreaterOrEqual);
BINARY_OPERATOR_TOKEN_DECLS(Less, "<", 8, LessThan);
BINARY_OPERATOR_TOKEN_DECLS(DoubleLess, "<<", 8, LeftShift);
BINARY_OPERATOR_TOKEN_DECLS(LessEquals, "<=", 8, LessOrEqual);
BINARY_OPERATOR_TOKEN_DECLS(Pipe, "|", 5, BinaryOr);
BINARY_OPERATOR_TOKEN_DECLS(Cap, "^", 9, Xor);
BINARY_OPERATOR_TOKEN_DECLS(Equals, "=", 3, BinaryDummyExpression);
BINARY_OPERATOR_TOKEN_DECLS(DoubleEquals, "==", 3, Equal);
BINARY_OPERATOR_TOKEN_DECLS(ExclamationEquals, "!=", 3, NotEqual);

#define UNARY_OPERATOR_TOKEN_DECLS(t, s, up, uexpr)                            \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind t::kind() const wontthrow { return Token::Kind::t; }             \
  Token::Flags t::flags() const wontthrow                                      \
  {                                                                            \
    return Token::Flag::UnaryOperator;                                         \
  }                                                                            \
  String t::raw_string() const throws { return s; }                            \
  u8 t::unary_precedence() const wontthrow { return up; }                      \
  Expression *t::construct_unary_expression(const Expression *rhs)             \
      const throws                                                             \
  {                                                                            \
    ASSERT(AST_ARENA != nullptr);                                              \
    return AST_ARENA->create<expressions::uexpr>(source_location(), rhs);      \
  }

UNARY_OPERATOR_TOKEN_DECLS(ExclamationMark, "!", 13, LogicalNot);
UNARY_OPERATOR_TOKEN_DECLS(Tilde, "~", 13, BinaryComplement);

} /* namespace tokens */

} /* namespace shit */
