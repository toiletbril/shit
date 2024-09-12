#include "Tokens.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"

namespace shit {

Token::Token(SourceLocation location) : m_location(location) {}

SourceLocation
Token::source_location() const
{
  return m_location;
}

std::string
Token::to_ast_string() const
{
  return raw_string();
}

namespace tokens {

#define KEYWORD_TOKEN_DECLS(t)                                                 \
  t::t(SourceLocation location) : Token(location) {}                           \
  Token::Kind  t::kind() const { return Token::Kind::t; }                      \
  Token::Flags t::flags() const { return Token::Flag::Keyword; }               \
  std::string  t::raw_string() const { return #t; }

KEYWORD_TOKEN_DECLS(If);
KEYWORD_TOKEN_DECLS(Then);
KEYWORD_TOKEN_DECLS(Else);
KEYWORD_TOKEN_DECLS(Elif);
KEYWORD_TOKEN_DECLS(Fi);
KEYWORD_TOKEN_DECLS(For);
KEYWORD_TOKEN_DECLS(While);
KEYWORD_TOKEN_DECLS(Until);
KEYWORD_TOKEN_DECLS(Do);
KEYWORD_TOKEN_DECLS(Done);
KEYWORD_TOKEN_DECLS(Case);
KEYWORD_TOKEN_DECLS(When);
KEYWORD_TOKEN_DECLS(Esac);
KEYWORD_TOKEN_DECLS(Time);
KEYWORD_TOKEN_DECLS(Function);

#define SENTINEL_TOKEN_DECLS(t)                                                \
  t::t(SourceLocation location) : Token(location) {}                           \
  Token::Kind  t::kind() const { return Token::Kind::t; }                      \
  Token::Flags t::flags() const { return Token::Flag::Sentinel; }              \
  std::string  t::raw_string() const { return #t; }

SENTINEL_TOKEN_DECLS(EndOfFile);
SENTINEL_TOKEN_DECLS(Newline);
SENTINEL_TOKEN_DECLS(Semicolon);
SENTINEL_TOKEN_DECLS(Dot);

SENTINEL_TOKEN_DECLS(LeftParen);
SENTINEL_TOKEN_DECLS(RightParen);
SENTINEL_TOKEN_DECLS(LeftSquareBracket);
SENTINEL_TOKEN_DECLS(DoubleLeftSquareBracket);
SENTINEL_TOKEN_DECLS(RightSquareBracket);
SENTINEL_TOKEN_DECLS(DoubleRightSquareBracket);
SENTINEL_TOKEN_DECLS(LeftBracket);
SENTINEL_TOKEN_DECLS(RightBracket);

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

String::String(SourceLocation location, char quote_char, std::string_view sv)
    : Value(location, sv), m_quote_char(quote_char)
{}

Token::Kind
String::kind() const
{
  return Token::Kind::String;
}

Token::Flags
String::flags() const
{
  return Token::Flag::Value;
}

char
String::quote_char() const
{
  return m_quote_char;
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
  Token::Kind  t::kind() const { return Token::Kind::t; }                      \
  Token::Flags t::flags() const                                                \
  {                                                                            \
    return Token::Flag::BinaryOperator | Token::Flag::UnaryOperator;           \
  }                                                                            \
  std::string                 t::raw_string() const { return s; }              \
  u8                          t::left_precedence() const { return bp; }        \
  u8                          t::unary_precedence() const { return up; }       \
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

#define BINARY_OPERATOR_TOKEN_DECLS(t, s, bp, bexpr)                           \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind  t::kind() const { return Token::Kind::t; }                      \
  Token::Flags t::flags() const { return Token::Flag::BinaryOperator; }        \
  std::string  t::raw_string() const { return s; }                             \
  u8           t::left_precedence() const { return bp; }                       \
  std::unique_ptr<Expression> t::construct_binary_expression(                  \
      const Expression *lhs, const Expression *rhs) const                      \
  {                                                                            \
    return std::make_unique<expressions::bexpr>(source_location(), lhs, rhs);  \
  }

BINARY_OPERATOR_TOKEN_DECLS(Slash, "/", 12, Divide);
BINARY_OPERATOR_TOKEN_DECLS(Asterisk, "*", 12, Multiply);
BINARY_OPERATOR_TOKEN_DECLS(Percent, "%", 12, Module);
BINARY_OPERATOR_TOKEN_DECLS(Ampersand, "&", 7, BinaryAnd);
BINARY_OPERATOR_TOKEN_DECLS(DoubleAmpersand, "&&", 4, LogicalAnd);
BINARY_OPERATOR_TOKEN_DECLS(Greater, ">", 8, GreaterThan);
BINARY_OPERATOR_TOKEN_DECLS(DoubleGreater, ">>", 8, RightShift);
BINARY_OPERATOR_TOKEN_DECLS(GreaterEquals, ">=", 8, GreaterOrEqual);
BINARY_OPERATOR_TOKEN_DECLS(Less, "<", 8, LessThan);
BINARY_OPERATOR_TOKEN_DECLS(DoubleLess, "<<", 8, LeftShift);
BINARY_OPERATOR_TOKEN_DECLS(LessEquals, "<=", 8, LessOrEqual);
BINARY_OPERATOR_TOKEN_DECLS(Pipe, "|", 5, BinaryOr);
BINARY_OPERATOR_TOKEN_DECLS(DoublePipe, "||", 4, LogicalOr);
BINARY_OPERATOR_TOKEN_DECLS(Cap, "^", 9, Xor);
BINARY_OPERATOR_TOKEN_DECLS(Equals, "=", 3, BinaryDummyExpression);
BINARY_OPERATOR_TOKEN_DECLS(DoubleEquals, "==", 3, Equal);
BINARY_OPERATOR_TOKEN_DECLS(ExclamationEquals, "!=", 3, NotEqual);

#define UNARY_OPERATOR_TOKEN_DECLS(t, s, up, uexpr)                            \
  t::t(SourceLocation location) : Operator(location) {}                        \
  Token::Kind  t::kind() const { return Token::Kind::t; }                      \
  Token::Flags t::flags() const { return Token::Flag::UnaryOperator; }         \
  std::string  t::raw_string() const { return s; }                             \
  u8           t::unary_precedence() const { return up; }                      \
  std::unique_ptr<Expression> t::construct_unary_expression(                   \
      const Expression *rhs) const                                             \
  {                                                                            \
    return std::make_unique<expressions::uexpr>(source_location(), rhs);       \
  }

UNARY_OPERATOR_TOKEN_DECLS(ExclamationMark, "!", 13, LogicalNot);
UNARY_OPERATOR_TOKEN_DECLS(Tilde, "~", 13, BinaryComplement);

} /* namespace tokens */

} /* namespace shit */
