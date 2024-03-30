#pragma once

#include "expr.hpp"
#include "types.hpp"

#include <string>

struct Token;
static std::string token_to_str(const Token *t);

enum class TokenType
{
  Invalid,
  EndOfFile,

  Number,
  Plus,
  Minus,
  Asterisk,
  Slash,
  Percent,
  RightParen,
  LeftParen,
};

typedef u8 OperatorFlags;

enum OperatorFlag
{
  NotAnOperator = 0,
  Unary         = 1 << 0,
  Binary        = 1 << 1,
};

struct Token
{
  Token(usize location) : m_location(location){};
  virtual ~Token() {}

  virtual TokenType   type() const  = 0;
  virtual std::string value() const = 0;

  virtual OperatorFlags
  operator_flags() const
  {
    return OperatorFlag::NotAnOperator;
  }

  std::string
  to_ast_string() const
  {
    return token_to_str(this);
  }

  usize
  location() const
  {
    return m_location;
  }

protected:
  usize m_location{};
};

struct EndOfFile : public Token
{
  EndOfFile(usize source_position) : Token(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::EndOfFile;
  }

  std::string
  value() const override
  {
    return "EOF";
  }
};

struct Number : public Token
{
  Number(usize source_position, std::string sv)
      : Token(source_position), m_value(sv)
  {
  }

  TokenType
  type() const override
  {
    return TokenType::Number;
  }

  std::string
  value() const override
  {
    return m_value;
  }

protected:
  std::string m_value;
};

struct TokenOperator : public Token
{
  TokenOperator(usize source_position) : Token(source_position) {}
  virtual ~TokenOperator() {};

  virtual u8
  left_precedence() const
  {
    return 0;
  }

  virtual u8
  unary_precedence() const
  {
    return 0;
  }
};

struct Plus : public TokenOperator
{
  Plus(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Plus;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "+";
  }

  u8
  left_precedence() const override
  {
    return 2;
  }
};

struct Minus : public TokenOperator
{
  Minus(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Minus;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary | OperatorFlag::Unary;
  }

  std::string
  value() const override
  {
    return "-";
  }

  u8
  left_precedence() const override
  {
    return 2;
  }

  u8
  unary_precedence() const override
  {
    return 5;
  }
};

struct Slash : public TokenOperator
{
  Slash(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Slash;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "/";
  }

  u8
  left_precedence() const override
  {
    return 3;
  }
};

struct Asterisk : public TokenOperator
{
  Asterisk(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Asterisk;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "*";
  }

  u8
  left_precedence() const override
  {
    return 3;
  }
};

struct Percent : public TokenOperator
{
  Percent(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Percent;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "%";
  }

  u8
  left_precedence() const override
  {
    return 1;
  }
};

struct LeftParen : public TokenOperator
{
  LeftParen(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::LeftParen;
  }

  std::string
  value() const override
  {
    return "(";
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Unary;
  }

  u8
  unary_precedence() const override
  {
    return 5;
  }
};

struct RightParen : public TokenOperator
{
  RightParen(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::RightParen;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Unary;
  }

  std::string
  value() const override
  {
    return ")";
  }

  u8
  unary_precedence() const override
  {
    return 5;
  }
};

static std::string
token_to_str(const Token *t)
{
  switch (t->type()) {
  case TokenType::Invalid:
    return "invalid";
  case TokenType::EndOfFile:
    return "end of file";
  case TokenType::Number: {
    std::string s = "number ";
    s += static_cast<const Number *>(t)->value();
    return s;
  }
  case TokenType::Plus:
    return "plus";
  case TokenType::Minus:
    return "minus";
  case TokenType::Asterisk:
    return "asterisk";
  case TokenType::Slash:
    return "slash";
  case TokenType::Percent:
    return "percent";
  case TokenType::RightParen:
    return "right paren";
  case TokenType::LeftParen:
    return "left paren";
  default:
    __builtin_unreachable();
  }
}
