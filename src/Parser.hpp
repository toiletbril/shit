#pragma once

#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"

struct Parser
{
  Parser(Lexer *lexer);
  ~Parser();

  std::unique_ptr<Expression> construct_ast();

private:
  Lexer            *m_lexer;
  ErrorWithLocation m_error{};

  usize m_parentheses_depth{0};
  usize m_condition_depth{0};
  usize m_if_depth{0};

  std::unique_ptr<Expression> parse_expression(u8 min_precedence = 0);

  std::unique_ptr<Expression> parse_identifier(const TokenIdentifier *n);
  std::unique_ptr<Expression> parse_number(const TokenNumber *n);
  std::unique_ptr<Expression> parse_string(const TokenString *n);
};
