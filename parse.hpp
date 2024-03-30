#pragma once

#include "error.hpp"
#include "expr.hpp"
#include "lex.hpp"
#include "token.hpp"

struct Parser
{
  Parser(Lexer *lexer) : m_lexer(lexer) {}
  ~Parser() { delete m_lexer; }

  std::unique_ptr<Expression>
  construct_ast()
  {
    std::unique_ptr<Expression> e = parse(0);
    if (e == nullptr)
      throw m_error;
    return e;
  }

private:
  Lexer *m_lexer;
  Error *m_error{};

  usize m_parens_depth{0};

  std::unique_ptr<Expression>
  parse(u8 min_precedence)
  {
    std::unique_ptr<Expression> lhs{};
    std::unique_ptr<Token>      t = m_lexer->next_token();
    if ((m_error = m_lexer->error()))
      return nullptr;

    switch (t->type()) {
    case TokenType::Number:
      lhs = parse_number(static_cast<const Number *>(t.get()));
      break;
    case TokenType::LeftParen: {
      m_parens_depth++;
      if ((lhs = parse(0)) == nullptr)
        return nullptr;
      std::unique_ptr<Token> rp = m_lexer->next_token();
      if ((m_error = m_lexer->error()))
        return nullptr;
      if (rp->type() != TokenType::RightParen) {
        m_error = new ParserError{t->location(), m_lexer->source(),
                                  "Unterminated parenthesis"};
        return nullptr;
      }
      m_parens_depth--;
      break;
    }
    default:
      if (t->operator_flags() & OperatorFlag::Unary) {
        const TokenOperator *op = static_cast<const TokenOperator *>(t.get());
        std::unique_ptr<Expression> rhs = parse(op->unary_precedence());
        if (rhs == nullptr)
          return nullptr;
        lhs = construct_unary_expression(op, rhs.release());
      } else {
        m_error = new ParserError{t->location(), m_lexer->source(),
                                  "Expected a leaf type, found " +
                                      t->to_ast_string()};
        return nullptr;
      }
      break;
    }

    for (;;) {
      std::unique_ptr<Token> maybe_op = m_lexer->peek();
      if ((m_error = m_lexer->error()))
        return nullptr;
      if (maybe_op->type() == TokenType::EndOfFile)
        return lhs;
      if (maybe_op->type() == TokenType::RightParen) {
        if (m_parens_depth == 0) {
          m_error = new ParserError{t->location(), m_lexer->source(),
                                    "Unexpected closing parenthesis"};
          return nullptr;
        }
        return lhs;
      }
      if (maybe_op->operator_flags() == OperatorFlag::NotAnOperator) {
        throw ParserError{maybe_op->location(), m_lexer->source(),
                          "Expected a binary operator, found " +
                              maybe_op->to_ast_string()};
      }
      const TokenOperator *op =
          static_cast<const TokenOperator *>(maybe_op.get());
      if (op->left_precedence() < min_precedence)
        break;
      m_lexer->next_token();
      std::unique_ptr<Expression> rhs = parse(op->left_precedence() + 1);
      if (rhs == nullptr)
        return nullptr;
      lhs = construct_binary_expression(lhs.release(), op, rhs.release());
    }

    return lhs;
  }

  std::unique_ptr<Expression>
  parse_number(const Number *n)
  {
    i64 value = std::atoll(n->value().data());
    return std::make_unique<Constant>(value);
  }

  std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs, const TokenOperator *t,
                              const Expression *rhs)
  {
    switch (t->type()) {
    case TokenType::Minus:
      return std::make_unique<Subtract>(lhs, rhs);
    case TokenType::Plus:
      return std::make_unique<Add>(lhs, rhs);
    case TokenType::Percent:
      return std::make_unique<Module>(lhs, rhs);
    case TokenType::Asterisk:
      return std::make_unique<Multiply>(lhs, rhs);
    case TokenType::Slash:
      return std::make_unique<Divide>(lhs, rhs);
    default:
      __builtin_unreachable();
    }
  }

  std::unique_ptr<Expression>
  construct_unary_expression(const TokenOperator *t, const Expression *rhs)
  {
    switch (t->type()) {
    case TokenType::Minus:
      return std::make_unique<Negate>(rhs);
    default:
      __builtin_unreachable();
    }
  }
};
