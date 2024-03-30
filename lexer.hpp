#pragma once

#include "expr.hpp"
#include "parser.hpp"

struct Parser
{
  Parser(Lexer *parser) : m_parser(parser) {}

  Expression<int> *construct_ast(u8 precedence)
  {
    Token *lhs = m_parser->next_token();
    if (lhs->type() != TokenType::Number)
      throw "First token is not a number!";

    for (;;) {
      Expression<int> *node = parse_precendence(lhs, precedence);
      if (node == lhs)
        break;
      lhs = node;
    }
  }

  Expression<int> *parse_precendence(Token *lhs, u8 precedence)
  {

  }

private:
  Lexer *m_lexer;
};
