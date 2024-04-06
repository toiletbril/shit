#pragma once

#include "Common.hpp"
#include "Expressions.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

typedef u8 TokenFlags;

enum TokenFlag
{
  /* clang-format off */
  Sentinel       = 0,
  Value          = 1,
  UnaryOperator  = 1 << 1,
  BinaryOperator = 1 << 2,
  /* clang-format on */
};

enum class TokenType
{
  Invalid,

  /* Significant symbols */
  EndOfFile,
  RightParen,
  LeftParen,
  Semicolon,

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

const std::unordered_map<std::string, TokenType> keywords = {
    {"if",   TokenType::If  },
    {"then", TokenType::Then},
    {"else", TokenType::Else},
    {"fi",   TokenType::Fi  },
    {"echo", TokenType::Echo},
    {"exit", TokenType::Exit},
};

/**
 * Simple tokens
 */
struct Token
{
  virtual ~Token();

  virtual TokenType   type() const = 0;
  virtual TokenFlags  flags() const = 0;
  virtual std::string value() const = 0;

  virtual std::string to_ast_string() const;

  usize location() const;

protected:
  Token(usize location);

private:
  usize m_location;
};

struct TokenIf : public Token
{
  TokenIf(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenFi : public Token
{
  TokenFi(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenElse : public Token
{
  TokenElse(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenThen : public Token
{
  TokenThen(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenEcho : public Token
{
  TokenEcho(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenExit : public Token
{
  TokenExit(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenEndOfFile : public Token
{
  TokenEndOfFile(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenSemicolon : public Token
{
  TokenSemicolon(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenLeftParen : public Token
{
  TokenLeftParen(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;
};

struct TokenRightParen : public Token
{
  TokenRightParen(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
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

  TokenType  type() const override;
  TokenFlags flags() const override;
};

struct TokenString : public TokenValue
{
  TokenString(usize location, std::string_view sv);

  TokenType  type() const override;
  TokenFlags flags() const override;
};

struct TokenIdentifier : public TokenValue
{
  TokenIdentifier(usize location, std::string_view sv);

  TokenType  type() const override;
  TokenFlags flags() const override;
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

  TokenType   type() const override;
  TokenFlags  flags() const override;
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

  TokenType   type() const override;
  TokenFlags  flags() const override;
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

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenAsterisk : public TokenOperator
{
  TokenAsterisk(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenPercent : public TokenOperator
{
  TokenPercent(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenTilde : public TokenOperator
{
  TokenTilde(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct TokenExclamationMark : public TokenOperator
{
  TokenExclamationMark(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct TokenAmpersand : public TokenOperator
{
  TokenAmpersand(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleAmpersand : public TokenOperator
{
  TokenDoubleAmpersand(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenGreater : public TokenOperator
{
  TokenGreater(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleGreater : public TokenOperator
{
  TokenDoubleGreater(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenGreaterEquals : public TokenOperator
{
  TokenGreaterEquals(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenLess : public TokenOperator
{
  TokenLess(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleLess : public TokenOperator
{
  TokenDoubleLess(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenLessEquals : public TokenOperator
{
  TokenLessEquals(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenPipe : public TokenOperator
{
  TokenPipe(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoublePipe : public TokenOperator
{
  TokenDoublePipe(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenCap : public TokenOperator
{
  TokenCap(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenEquals : public TokenOperator
{
  TokenEquals(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenDoubleEquals : public TokenOperator
{
  TokenDoubleEquals(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct TokenExclamationEquals : public TokenOperator
{
  TokenExclamationEquals(usize location);

  TokenType   type() const override;
  TokenFlags  flags() const override;
  std::string value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};
