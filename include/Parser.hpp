#pragma once

#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"

struct Parser
{
  Parser(Lexer *lexer);

  ~Parser();

  std::unique_ptr<Expression>
  construct_ast();

private:
  Lexer     *m_lexer;
  ErrorBase *m_error{};
  usize m_parentheses_depth{0};

  std::unique_ptr<Expression>
  parse_expression(u8 min_precedence);

  std::unique_ptr<Expression>
  parse_number(const Number *n);
};
