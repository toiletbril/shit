#pragma once

#include "Common.hpp"
#include "Expressions.hpp"

#include <memory>
#include <string>
#include <string_view>

typedef u8 OperatorFlags;

enum OperatorFlag
{
  NotAnOperator = 0,
  Unary = 1 << 0,
  Binary = 1 << 1,
};

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
  Equals,
  DoubleEquals,
  ExclamationMark,
  ExclamationEquals,
};

/*
 * Base classes
 */

struct Token
{
  virtual ~Token() = default;

  virtual TokenType     type() const = 0;
  virtual OperatorFlags operator_flags() const = 0;
  virtual std::string   value() const = 0;

  virtual std::string to_ast_string() const;

  usize location() const;

protected:
  Token(usize location);

private:
  usize m_location;
};

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

/*
 * Specific token types
 */

struct Number : public Token
{
  Number(usize location, std::string_view sv);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

private:
  std::string m_value;
};

struct EndOfFile : public Token
{
  EndOfFile(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

private:
  std::string m_value;
};

struct Plus : public TokenOperator
{
  Plus(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct Minus : public TokenOperator
{
  Minus(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct Slash : public TokenOperator
{
  Slash(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Asterisk : public TokenOperator
{
  Asterisk(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Percent : public TokenOperator
{
  Percent(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct LeftParen : public Token
{
  LeftParen(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;
};

struct RightParen : public Token
{
  RightParen(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;
};

struct Tilde : public TokenOperator
{
  Tilde(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct ExclamationMark : public TokenOperator
{
  ExclamationMark(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 unary_precedence() const override;
  std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const override;
};

struct Ampersand : public TokenOperator
{
  Ampersand(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleAmpersand : public TokenOperator
{
  DoubleAmpersand(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Greater : public TokenOperator
{
  Greater(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleGreater : public TokenOperator
{
  DoubleGreater(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct GreaterEquals : public TokenOperator
{
  GreaterEquals(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Less : public TokenOperator
{
  Less(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleLess : public TokenOperator
{
  DoubleLess(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct LessEquals : public TokenOperator
{
  LessEquals(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Pipe : public TokenOperator
{
  Pipe(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoublePipe : public TokenOperator
{
  DoublePipe(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Cap : public TokenOperator
{
  Cap(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct Equals : public TokenOperator
{
  Equals(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct DoubleEquals : public TokenOperator
{
  DoubleEquals(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};

struct ExclamationEquals : public TokenOperator
{
  ExclamationEquals(usize location);

  TokenType     type() const override;
  OperatorFlags operator_flags() const override;
  std::string   value() const override;

  u8 left_precedence() const override;
  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const override;
};
