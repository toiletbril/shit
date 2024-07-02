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
    RightParen,
    LeftParen,
    LeftSquareBracket,
    RightSquareBracket,
    DoubleLeftSquareBracket,
    DoubleRightSquareBracket,
    RightBracket,
    LeftBracket,

    EndOfFile,
    Newline,
    Semicolon,
    Dot,
    Dollar,

    /* Values */
    Number,
    String,
    Expandable,
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
    Expandable     = 1 << 3,
    /* clang-format on */
  };

  virtual ~Token() = default;

  /* Each token should provide it's own way to copy it. */
  Token(const Token &) = delete;
  Token(Token &&) noexcept = delete;
  Token &operator=(const Token &) = delete;
  Token &operator=(Token &&) noexcept = delete;

  virtual Kind        kind() const = 0;
  virtual Flags       flags() const = 0;
  virtual std::string raw_string() const = 0;

  virtual std::string to_ast_string() const;

  usize source_location() const;

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

namespace tokens {

struct If : public Token
{
  If(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Fi : public Token
{
  Fi(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Else : public Token
{
  Else(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Then : public Token
{
  Then(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct EndOfFile : public Token
{
  EndOfFile(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Newline : public Token
{
  Newline(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Semicolon : public Token
{
  Semicolon(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Dot : public Token
{
  Dot(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct LeftParen : public Token
{
  LeftParen(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct RightParen : public Token
{
  RightParen(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct LeftSquareBracket : public Token
{
  LeftSquareBracket(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct RightSquareBracket : public Token
{
  RightSquareBracket(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct DoubleLeftSquareBracket : public Token
{
  DoubleLeftSquareBracket(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct DoubleRightSquareBracket : public Token
{
  DoubleRightSquareBracket(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct RightBracket : public Token
{
  RightBracket(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct LeftBracket : public Token
{
  LeftBracket(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

/**
 * Tokens with important values
 */
struct Value : public Token
{
  Value(usize location, std::string_view sv);

  std::string raw_string() const override;

protected:
  std::string m_value;
};

struct Number : public Value
{
  Number(usize location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

struct String : public Value
{
  String(usize location, char quote_char, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;

  char quote_char() const;

protected:
  char m_quote_char;
};

/* Expand cases:
 * 1. ~[user]/some/path;
 * 2. /some/path/file*;
 * 5. $VARIABLE. */
struct Expandable : public Value
{
  Expandable(usize location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

struct Identifier : Value
{
  Identifier(usize location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

/**
 * Operators
 */
struct Operator : public Token
{
  Operator(usize location);

  virtual bool binary_left_associative() const;

  virtual u8 left_precedence() const;
  virtual std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const;

  virtual u8 unary_precedence() const;
  virtual std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const;
};

struct Plus : Operator
{
  Plus(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct Minus : Operator
{
  Minus(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct Slash : Operator
{
  Slash(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Asterisk : Operator
{
  Asterisk(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Percent : Operator
{
  Percent(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Tilde : Operator
{
  Tilde(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct ExclamationMark : Operator
{
  ExclamationMark(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct Ampersand : Operator
{
  Ampersand(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleAmpersand : Operator
{
  DoubleAmpersand(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Greater : Operator
{
  Greater(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleGreater : Operator
{
  DoubleGreater(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct GreaterEquals : Operator
{
  GreaterEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Less : Operator
{
  Less(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleLess : Operator
{
  DoubleLess(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct LessEquals : Operator
{
  LessEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Pipe : Operator
{
  Pipe(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoublePipe : Operator
{
  DoublePipe(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Cap : Operator
{
  Cap(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Equals : Operator
{
  Equals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleEquals : Operator
{
  DoubleEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct ExclamationEquals : Operator
{
  ExclamationEquals(usize location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

} /* namespace tokens */

} /* namespace shit */
