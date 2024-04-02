#include "Parser.hpp"

Parser::Parser(Lexer *lexer) : m_lexer(lexer) {}
Parser::~Parser() { delete m_lexer; }

std::unique_ptr<Expression>
Parser::construct_ast()
{
  std::unique_ptr<Expression> e = parse_expression(0);
  if (e == nullptr)
    throw m_error;
  return e;
}

std::unique_ptr<Expression>
Parser::parse_expression(u8 min_precedence)
{
  std::unique_ptr<Expression> lhs{};
  std::unique_ptr<Token>      t{m_lexer->next_token()};
  if ((m_error = m_lexer->error()))
    return nullptr;

  switch (t->type()) {
  case TokenType::Number:
    lhs = parse_number(static_cast<const Number *>(t.get()));
    break;
  case TokenType::LeftParen: {
    if (m_parentheses_depth >= 256) {
      m_error =
          Error{t->location(), "Bracket nesting level exceeded maximum of 256"};
      return nullptr;
    }

    m_parentheses_depth++;
    if ((lhs = parse_expression(0)) == nullptr)
      return nullptr;

    std::unique_ptr<Token> rp = std::unique_ptr<Token>{m_lexer->next_token()};
    if ((m_error = m_lexer->error()))
      return nullptr;

    if (rp->type() != TokenType::RightParen) {
      m_error = Error{t->location(), "Unterminated parenthesis"};
      return nullptr;
    }
    m_parentheses_depth--;
    break;
  }
  default:
    if (t->operator_flags() & OperatorFlag::Unary) {
      const TokenOperator *op = static_cast<const TokenOperator *>(t.get());

      std::unique_ptr<Expression> rhs =
          parse_expression(op->unary_precedence());
      if (rhs == nullptr)
        return nullptr;

      lhs = op->construct_unary_expression(rhs.release());
    } else {
      m_error = Error{t->location(),
                      "Expected a value, found '" + t->to_ast_string() + "'"};
      return nullptr;
    }
    break;
  }

  for (;;) {
    std::unique_ptr<Token> maybe_op{m_lexer->peek_token()};
    if ((m_error = m_lexer->error()))
      return nullptr;
    if (maybe_op->type() == TokenType::EndOfFile)
      return lhs;

    if (maybe_op->type() == TokenType::RightParen) {
      if (m_parentheses_depth == 0) {
        m_error = Error{maybe_op->location(), "Unexpected closing parenthesis"};
        return nullptr;
      }
      return lhs;
    }

    if (!(maybe_op->operator_flags() & OperatorFlag::Binary)) {
      m_error =
          Error{maybe_op->location(), "Expected an operator, found '" +
                                          maybe_op->to_ast_string() + "'"};
      return nullptr;
    }

    const TokenOperator *op =
        static_cast<const TokenOperator *>(maybe_op.get());
    if (op->left_precedence() < min_precedence)
      break;
    m_lexer->advance_past_peek();

    std::unique_ptr<Expression> rhs = parse_expression(
        op->left_precedence() + (op->binary_left_associative() ? 1 : -1));
    if (rhs == nullptr)
      return nullptr;

    lhs = op->construct_binary_expression(lhs.release(), rhs.release());
  }

  return lhs;
}

std::unique_ptr<Expression>
Parser::parse_number(const Number *n)
{
  i64 value = std::atoll(n->value().data());
  return std::make_unique<Constant>(n->location(), value);
}
