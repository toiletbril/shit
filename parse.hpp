#pragma once

#include "error.hpp"
#include "expr.hpp"
#include "lex.hpp"

struct Parser
{
  Parser(Lexer *lexer) : m_lexer(lexer) {}

  ~Parser() { delete m_lexer; }

  Expression *
  construct_ast()
  {
    return parse(0);
  }

private:
  Lexer *m_lexer;

  Expression *
  parse(u8 min_precedence)
  {
    Expression *lhs;
    std::cout << "parsing leaf!" << std::endl;
    Token *t = m_lexer->next_token();

    switch (t->type()) {
    case TokenType::Number:
      lhs = consume_number(static_cast<Number *>(t));
      break;
    default:
      throw ParserError{t->location(), m_lexer->source(),
                        "Expected a leaf type, found " + t->to_string()};
    }

    for (;;) {
      std::cout << "parsing operator!" << std::endl;
      Token *maybe_op = m_lexer->next_token();
      if (maybe_op->type() == TokenType::EndOfFile)
        break;
      if (!maybe_op->is_binary())
        throw ParserError{maybe_op->location(), m_lexer->source(),
                          "Expected a binary operator, found " +
                              maybe_op->to_string()};
      TokenOperator *op = static_cast<TokenOperator *>(maybe_op);
      if (op->precendence() <= min_precedence)
        break;
      Expression *rhs = parse(op->precendence());
      lhs             = op->construct_binary_expression(lhs, rhs);
    }

    return lhs;
  }

  Expression *
  consume_number(Number *n)
  {
    i64 value = std::atoll(n->source_string().data());
    return new Constant{value};
  }
};
