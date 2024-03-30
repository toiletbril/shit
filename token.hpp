#pragma once

#include "types.hpp"
#include "expr.hpp"

#include <memory>
#include <string>

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
    return value();
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
  virtual ~TokenOperator(){};

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

  virtual std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs, const Expression *rhs) const
  {
    (void) lhs;
    (void) rhs;
    __builtin_unreachable();
  }

  virtual std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const
  {
    (void) rhs;
    __builtin_unreachable();
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
    return OperatorFlag::Binary | OperatorFlag::Unary;
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

  u8
  unary_precedence() const override
  {
    return 4;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<Add>(lhs, rhs);
  }

  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override
  {
    return std::make_unique<Unnegate>(rhs);
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
    return 4;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<Subtract>(lhs, rhs);
  }

  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override
  {
    return std::make_unique<Negate>(rhs);
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

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<Divide>(lhs, rhs);
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

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<Multiply>(lhs, rhs);
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

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<Module>(lhs, rhs);
  }
};

struct LeftParen : public Token
{
  LeftParen(usize source_position) : Token(source_position) {}

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
    return OperatorFlag::NotAnOperator;
  }
};

struct RightParen : public Token
{
  RightParen(usize source_position) : Token(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::RightParen;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::NotAnOperator;
  }

  std::string
  value() const override
  {
    return ")";
  }
};

struct Tilde : public TokenOperator
{
  Tilde(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Tilde;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Unary;
  }

  std::string
  value() const override
  {
    return "~";
  }

  u8
  unary_precedence() const override
  {
    return 2;
  }

  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override
  {
    return std::make_unique<BinaryComplement>(rhs);
  }
};

struct Ampersand : public TokenOperator
{
  Ampersand(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Ampersand;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "&";
  }

  u8
  left_precedence() const override
  {
    return 2;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<BinaryAnd>(lhs, rhs);
  }
};

struct DoubleAmpersand : public TokenOperator
{
  DoubleAmpersand(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::DoubleAmpersand;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "&&";
  }

  u8
  left_precedence() const override
  {
    return 11;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<LogicalAnd>(lhs, rhs);
  }
};

struct Greater : public TokenOperator
{
  Greater(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Greater;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return ")";
  }

  u8
  left_precedence() const override
  {
    return 6;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<GreaterThan>(lhs, rhs);
  }
};

struct DoubleGreater : public TokenOperator
{
  DoubleGreater(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::DoubleGreater;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return ">>";
  }

  u8
  left_precedence() const override
  {
    return 5;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<RightShift>(lhs, rhs);
  }
};

struct GreaterEquals : public TokenOperator
{
  GreaterEquals(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::GreaterEquals;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return ">=";
  }

  u8
  left_precedence() const override
  {
    return 6;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<GreaterOrEqualTo>(lhs, rhs);
  }
};

struct Less : public TokenOperator
{
  Less(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Less;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "<";
  }

  u8
  left_precedence() const override
  {
    return 6;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<LessThan>(lhs, rhs);
  }
};

struct DoubleLess : public TokenOperator
{
  DoubleLess(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::DoubleLess;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "<<";
  }

  u8
  left_precedence() const override
  {
    return 5;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<LeftShift>(lhs, rhs);
  }
};

struct LessEquals : public TokenOperator
{
  LessEquals(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::LessEquals;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "<=";
  }

  u8
  left_precedence() const override
  {
    return 6;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<LessOrEqualTo>(lhs, rhs);
  }
};

struct Pipe : public TokenOperator
{
  Pipe(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Pipe;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "|";
  }

  u8
  left_precedence() const override
  {
    return 10;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<BinaryOr>(lhs, rhs);
  }
};

struct DoublePipe : public TokenOperator
{
  DoublePipe(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::DoublePipe;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "||";
  }

  u8
  left_precedence() const override
  {
    return 12;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<LogicalOr>(lhs, rhs);
  }
};

struct Cap : public TokenOperator
{
  Cap(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type() const override
  {
    return TokenType::Cap;
  }

  OperatorFlags
  operator_flags() const override
  {
    return OperatorFlag::Binary;
  }

  std::string
  value() const override
  {
    return "^";
  }

  u8
  left_precedence() const override
  {
    return 9;
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override
  {
    return std::make_unique<Xor>(lhs, rhs);
  }
};
