#include "Tokens.hpp"

#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"

namespace shit {

Token::Token(SourceLocation location) : m_location(location) {}

SourceLocation
Token::source_location() const
{
  return m_location;
}

void
Token::operator delete(void *pointer)
{
  if (g_ast_arena != nullptr && g_ast_arena->owns(pointer)) return;
  ::operator delete(pointer);
}

std::string
Token::to_ast_string() const
{
  return raw_string();
}

bool
WordSegment::is_split_eligible() const
{
  return kind == Kind::UnquotedText ||
         (kind == Kind::VariableReference && !is_in_double_quotes);
}

bool
WordSegment::has_live_glob_chars() const
{
  return kind == Kind::UnquotedText;
}

bool
WordSegment::is_tilde_candidate() const
{
  return kind == Kind::UnquotedText;
}

bool
Word::is_empty() const
{
  return segments.empty();
}

std::string
Word::to_literal_string() const
{
  std::string result{};
  for (const WordSegment &segment : segments) {
    if (segment.kind == WordSegment::Kind::CommandSubstitution) {
      result += "$(" + segment.text + ")";
      continue;
    }
    if (segment.kind == WordSegment::Kind::VariableReference) result += '$';
    result += segment.text;
  }
  return result;
}

std::string
Word::to_pretty_string() const
{
  std::string result{"[Word"};
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
    }
    result += " \"";
    result += segment.text;
    result += '"';
  }
  result += "\n]";
  return result;
}

std::optional<std::pair<std::string, Word>>
Word::get_assignment_split() const
{
  if (segments.empty()) return std::nullopt;

  const WordSegment &first = segments[0];
  if (first.kind != WordSegment::Kind::UnquotedText) return std::nullopt;

  usize equals_position = first.text.find('=');
  if (equals_position == std::string::npos || equals_position == 0)
    return std::nullopt;

  if (!lexer::is_variable_name_start(first.text[0])) return std::nullopt;
  for (usize i = 1; i < equals_position; i++) {
    if (!lexer::is_variable_name(first.text[i])) return std::nullopt;
  }

  std::string name = first.text.substr(0, equals_position);

  Word value{};
  /* The value always begins with an unquoted segment, even when empty, so that
     FOO= produces one empty field rather than no field at all. */
  value.segments.push_back(WordSegment{WordSegment::Kind::UnquotedText,
                                       first.text.substr(equals_position + 1),
                                       false});
  for (usize i = 1; i < segments.size(); i++)
    value.segments.push_back(segments[i]);

  return std::make_pair(std::move(name), std::move(value));
}

namespace tokens {

#define KEYWORD_TOKEN_DECLS(t, s)                                              \
  t::t(SourceLocation location) : Token(location) {}                           \
  Token::Kind t::kind() const { return Token::Kind::t; }                       \
  Token::Flags t::flags() const { return Token::Flag::Keyword; }               \
  std::string t::raw_string() const { return s; }

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
  Token::Kind t::kind() const { return Token::Kind::t; }                       \
  Token::Flags t::flags() const                                                \
  {                                                                            \
    return Token::Flag::Sentinel | Token::Flag::CompoundList;                  \
  }                                                                            \
  std::string t::raw_string() const { return s; }

SENTINEL_TOKEN_DECLS_COMPOUND(Newline, "newline");
SENTINEL_TOKEN_DECLS_COMPOUND(Semicolon, ";");

#define SENTINEL_TOKEN_DECLS(t, s)                                             \
  t::t(SourceLocation location) : Token(location) {}                           \
  Token::Kind t::kind() const { return Token::Kind::t; }                       \
  Token::Flags t::flags() const { return Token::Flag::Sentinel; }              \
  std::string t::raw_string() const { return s; }

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

Value::Value(SourceLocation location, std::string_view sv)
    : Token(location), m_value(sv)
{}

std::string
Value::raw_string() const
{
  return m_value;
}

Number::Number(SourceLocation location, std::string_view sv)
    : Value(location, sv)
{}

Token::Kind
Number::kind() const
{
  return Token::Kind::Number;
}

Token::Flags
Number::flags() const
{
  return Token::Flag::Value;
}

Assignment::Assignment(SourceLocation location, std::string_view key,
                       Word value)
    : Token(location), m_key(key), m_value(std::move(value))
{}

Token::Kind
Assignment::kind() const
{
  return Token::Kind::Assignment;
}

Token::Flags
Assignment::flags() const
{
  return Token::Flag::Special;
}

std::string
Assignment::raw_string() const
{
  return m_key + "=" + m_value.to_literal_string();
}

const std::string &
Assignment::key() const
{
  return m_key;
}

const Word &
Assignment::value_word() const
{
  return m_value;
}

WordToken::WordToken(SourceLocation location, Word word)
    : Value(location, ""), m_word(std::move(word))
{
  m_value = m_word.to_literal_string();
}

Token::Kind
WordToken::kind() const
{
  return Token::Kind::Word;
}

Token::Flags
WordToken::flags() const
{
  return Token::Flag::Value;
}

const Word &
WordToken::word() const
{
  return m_word;
}

Identifier::Identifier(SourceLocation location, std::string_view sv)
    : Value(location, sv)
{}

Token::Kind
Identifier::kind() const
{
  return Token::Kind::Identifier;
}

Token::Flags
Identifier::flags() const
{
  return Token::Flag::Value;
}

Redirection::Redirection(SourceLocation location, std::string_view what_fd,
                         std::string_view to_file)
    : Token(location), m_from_fd(what_fd), m_to_file(to_file)
{}

Token::Kind
Redirection::kind() const
{
  return Token::Kind::Redirection;
}

Token::Flags
Redirection::flags() const
{
  return Token::Flag::Special;
}

const std::string &
Redirection::from_fd() const
{
  return m_from_fd;
}

const std::string &
Redirection::to_file() const
{
  return m_to_file;
}

Operator::Operator(SourceLocation location) : Token(location) {}

u8
Operator::left_precedence() const
{
  return 0;
}

u8
Operator::unary_precedence() const
{
  return 0;
}

bool
Operator::binary_left_associative() const
{
  return true;
}

std::unique_ptr<Expression>
Operator::construct_binary_expression(const Expression *lhs,
                                      const Expression *rhs) const
{
  SHIT_UNUSED(lhs);
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid binary operator construction of type %d",
                   SHIT_ENUM(kind()));
}

std::unique_ptr<Expression>
Operator::construct_unary_expression(const Expression *rhs) const
{
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid unary operator construction of type %d",
                   SHIT_ENUM(kind()));
}

#define BINARY_UNARY_OPERATOR_TOKEN_DECLS(t, s, up, bp, uexpr, bexpr)          \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind t::kind() const { return Token::Kind::t; }                       \
  Token::Flags t::flags() const                                                \
  {                                                                            \
    return Token::Flag::BinaryOperator | Token::Flag::UnaryOperator;           \
  }                                                                            \
  std::string t::raw_string() const { return s; }                              \
  u8 t::left_precedence() const { return bp; }                                 \
  u8 t::unary_precedence() const { return up; }                                \
  std::unique_ptr<Expression> t::construct_binary_expression(                  \
      const Expression *lhs, const Expression *rhs) const                      \
  {                                                                            \
    return std::make_unique<expressions::bexpr>(source_location(), lhs, rhs);  \
  }                                                                            \
  std::unique_ptr<Expression> t::construct_unary_expression(                   \
      const Expression *rhs) const                                             \
  {                                                                            \
    return std::make_unique<expressions::uexpr>(source_location(), rhs);       \
  }

BINARY_UNARY_OPERATOR_TOKEN_DECLS(Plus, "+", 13, 11, Unnegate, Add);
BINARY_UNARY_OPERATOR_TOKEN_DECLS(Minus, "-", 13, 11, Negate, Subtract);

#define BINARY_OPERATOR_TOKEN_DECLS_COMPOUND(t, s, bp, bexpr)                  \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind t::kind() const { return Token::Kind::t; }                       \
  Token::Flags t::flags() const                                                \
  {                                                                            \
    return Token::Flag::BinaryOperator | Token::Flag::CompoundList;            \
  }                                                                            \
  std::string t::raw_string() const { return s; }                              \
  u8 t::left_precedence() const { return bp; }                                 \
  std::unique_ptr<Expression> t::construct_binary_expression(                  \
      const Expression *lhs, const Expression *rhs) const                      \
  {                                                                            \
    return std::make_unique<expressions::bexpr>(source_location(), lhs, rhs);  \
  }

#define BINARY_OPERATOR_TOKEN_DECLS(t, s, bp, bexpr)                           \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind t::kind() const { return Token::Kind::t; }                       \
  Token::Flags t::flags() const { return Token::Flag::BinaryOperator; }        \
  std::string t::raw_string() const { return s; }                              \
  u8 t::left_precedence() const { return bp; }                                 \
  std::unique_ptr<Expression> t::construct_binary_expression(                  \
      const Expression *lhs, const Expression *rhs) const                      \
  {                                                                            \
    return std::make_unique<expressions::bexpr>(source_location(), lhs, rhs);  \
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
  Token::Kind t::kind() const { return Token::Kind::t; }                       \
  Token::Flags t::flags() const { return Token::Flag::UnaryOperator; }         \
  std::string t::raw_string() const { return s; }                              \
  u8 t::unary_precedence() const { return up; }                                \
  std::unique_ptr<Expression> t::construct_unary_expression(                   \
      const Expression *rhs) const                                             \
  {                                                                            \
    return std::make_unique<expressions::uexpr>(source_location(), rhs);       \
  }

UNARY_OPERATOR_TOKEN_DECLS(ExclamationMark, "!", 13, LogicalNot);
UNARY_OPERATOR_TOKEN_DECLS(Tilde, "~", 13, BinaryComplement);

} /* namespace tokens */

} /* namespace shit */
