#pragma once

#include "Common.hpp"
#include "Expressions.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace shit {

/**
 * Simple tokens
 */
struct Token
{
  enum class Kind : uint8_t
  {
    Invalid,

    /* Significant symbols */
    EndOfFile,
    RightParen,
    LeftParen,
    Semicolon,
    Dot,

    /* Values */
    Number,
    String,
    Identifier,

    /* Operators */
    Plus,
    Minus,
    Asterisk,
    Slash,
    Percent,
    Tilde,
    Ampersand,
    DoubleAmpersand,
    Greater,
    DoubleGreater,
    GreaterEquals,
    Less,
    DoubleLess,
    LessEquals,
    Pipe,
    DoublePipe,
    Cap,
    Equals,
    DoubleEquals,
    ExclamationMark,
    ExclamationEquals,

    /* Keywords */
    If,
    Then,
    Else,
    Fi,
    Echo,
    Exit,
  };

  using Flags = u8;

  enum Flag : uint8_t
  {
    /* clang-format off */
    Sentinel       = 0,
    Value          = 1,
    UnaryOperator  = 1 << 1,
    BinaryOperator = 1 << 2,
    /* clang-format on */
  };

  virtual ~Token();

  virtual Kind        kind() const = 0;
  virtual Flags       flags() const = 0;
  virtual std::string value() const = 0;

  virtual std::string to_ast_string() const;

  usize location() const;

protected:
  Token(usize location);

private:
  usize m_location;
};

const std::unordered_map<std::string, Token::Kind> KEYWORDS = {
    {"if",   Token::Kind::If  },
    {"then", Token::Kind::Then},
    {"else", Token::Kind::Else},
    {"fi",   Token::Kind::Fi  },
};

struct TokenIf : public Token
{
  TokenIf(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenFi : public Token
{
  TokenFi(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenElse : public Token
{
  TokenElse(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenThen : public Token
{
  TokenThen(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenEndOfFile : public Token
{
  TokenEndOfFile(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenSemicolon : public Token
{
  TokenSemicolon(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenDot : public Token
{
  TokenDot(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenLeftParen : public Token
{
  TokenLeftParen(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

struct TokenRightParen : public Token
{
  TokenRightParen(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;
};

/**
 * Tokens with important values
 */
struct TokenValue : public Token
{
  TokenValue(usize location, std::string_view sv);

  std::string value() const override;

protected:
  std::string m_value;
};

struct TokenNumber : public TokenValue
{
  TokenNumber(usize location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

struct TokenString : public TokenValue
{
  TokenString(usize location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

struct TokenIdentifier : public TokenValue
{
  TokenIdentifier(usize location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

/**
 * Operators
 */
struct TokenOperator : public Token
{
  TokenOperator(usize location);

  virtual bool binary_left_associative() const;

  virtual u8 left_precedence() const;
  virtual std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const;

  virtual u8 unary_precedence() const;
  virtual std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const;
};

struct TokenPlus : public TokenOperator
{
  TokenPlus(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct TokenMinus : public TokenOperator
{
  TokenMinus(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct TokenSlash : public TokenOperator
{
  TokenSlash(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenAsterisk : public TokenOperator
{
  TokenAsterisk(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenPercent : public TokenOperator
{
  TokenPercent(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenTilde : public TokenOperator
{
  TokenTilde(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct TokenExclamationMark : public TokenOperator
{
  TokenExclamationMark(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct TokenAmpersand : public TokenOperator
{
  TokenAmpersand(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleAmpersand : public TokenOperator
{
  TokenDoubleAmpersand(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenGreater : public TokenOperator
{
  TokenGreater(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleGreater : public TokenOperator
{
  TokenDoubleGreater(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenGreaterEquals : public TokenOperator
{
  TokenGreaterEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenLess : public TokenOperator
{
  TokenLess(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleLess : public TokenOperator
{
  TokenDoubleLess(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenLessEquals : public TokenOperator
{
  TokenLessEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenPipe : public TokenOperator
{
  TokenPipe(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoublePipe : public TokenOperator
{
  TokenDoublePipe(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenCap : public TokenOperator
{
  TokenCap(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenEquals : public TokenOperator
{
  TokenEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleEquals : public TokenOperator
{
  TokenDoubleEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenExclamationEquals : public TokenOperator
{
  TokenExclamationEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

} /* namespace shit */
