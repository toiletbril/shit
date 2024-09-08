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
    Identifier,
    Redirection,
    ExpandableIdentifier,

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
    Special        = 1 << 4,
    /* clang-format on */
  };

  Token() = delete;
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

  SourceLocation source_location() const;

protected:
  Token(SourceLocation location);

private:
  SourceLocation m_location;
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
  If(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Fi : public Token
{
  Fi(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Else : public Token
{
  Else(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Then : public Token
{
  Then(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct EndOfFile : public Token
{
  EndOfFile(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Newline : public Token
{
  Newline(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Semicolon : public Token
{
  Semicolon(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct Dot : public Token
{
  Dot(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct LeftParen : public Token
{
  LeftParen(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct RightParen : public Token
{
  RightParen(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct LeftSquareBracket : public Token
{
  LeftSquareBracket(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct RightSquareBracket : public Token
{
  RightSquareBracket(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct DoubleLeftSquareBracket : public Token
{
  DoubleLeftSquareBracket(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct DoubleRightSquareBracket : public Token
{
  DoubleRightSquareBracket(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct RightBracket : public Token
{
  RightBracket(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

struct LeftBracket : public Token
{
  LeftBracket(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;
};

/**
 * Tokens with important values
 */
struct Value : public Token
{
  Value(SourceLocation location, std::string_view sv);

  std::string raw_string() const override;

protected:
  std::string m_value;
};

struct Number : public Value
{
  Number(SourceLocation location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

struct String : public Value
{
  String(SourceLocation location, char quote_char, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;

  char quote_char() const;

protected:
  char m_quote_char;
};

struct ExpandableIdentifier : public Value
{
  ExpandableIdentifier(SourceLocation location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

struct Identifier : Value
{
  Identifier(SourceLocation location, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;
};

struct Redirection : Token
{
  Redirection(SourceLocation location, std::string_view what_fd,
              std::string_view to_file);

  Kind  kind() const override;
  Flags flags() const override;

  const std::string &from_fd() const;
  const std::string &to_file() const;

protected:
  std::string m_from_fd{};
  std::string m_to_file{};
};

/**
 * Operators
 */
struct Operator : public Token
{
  Operator(SourceLocation location);

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
  Plus(SourceLocation location);

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
  Minus(SourceLocation location);

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
  Slash(SourceLocation location);

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
  Asterisk(SourceLocation location);

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
  Percent(SourceLocation location);

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
  Tilde(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct ExclamationMark : Operator
{
  ExclamationMark(SourceLocation location);

  Kind        kind() const override;
  Flags       flags() const override;
  std::string raw_string() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct Ampersand : Operator
{
  Ampersand(SourceLocation location);

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
  DoubleAmpersand(SourceLocation location);

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
  Greater(SourceLocation location);

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
  DoubleGreater(SourceLocation location);

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
  GreaterEquals(SourceLocation location);

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
  Less(SourceLocation location);

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
  DoubleLess(SourceLocation location);

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
  LessEquals(SourceLocation location);

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
  Pipe(SourceLocation location);

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
  DoublePipe(SourceLocation location);

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
  Cap(SourceLocation location);

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
  Equals(SourceLocation location);

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
  DoubleEquals(SourceLocation location);

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
  ExclamationEquals(SourceLocation location);

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
