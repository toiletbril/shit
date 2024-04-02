#include "Parser.hpp"

#include "Expressions.hpp"

Parser::Parser(Lexer *lexer) : m_lexer(lexer) {}
Parser::~Parser() { delete m_lexer; }

std::unique_ptr<Expression>
Parser::construct_ast()
{
  std::unique_ptr<Expression> e = parse_expression();
  if (e == nullptr)
    throw m_error;
  return e;
}

/* A standard pratt-parser for expressions. */
std::unique_ptr<Expression>
Parser::parse_expression(u8 min_precedence)
{
  std::unique_ptr<Expression> lhs{};
  std::unique_ptr<Token>      t{m_lexer->next_token()};
  if ((m_error = m_lexer->error()))
    return nullptr;

  /* Handle leaf type. We expect either a value, or an unary operator. */
  switch (t->type()) {
  case TokenType::Number:
    lhs = parse_number(static_cast<const TokenNumber *>(t.get()));
    break;

  case TokenType::String:
    lhs = parse_string(static_cast<const TokenString *>(t.get()));
    break;

  case TokenType::Identifier:
    lhs = parse_identifier(static_cast<const TokenIdentifier *>(t.get()));
    break;

  case TokenType::LeftParen: {
    if (m_parentheses_depth >= 256) {
      m_error = ErrorWithLocation{
          t->location(), "Bracket nesting level exceeded maximum of 256"};
      return nullptr;
    }

    m_parentheses_depth++;
    if ((lhs = parse_expression()) == nullptr)
      return nullptr;

    std::unique_ptr<Token> rp = std::unique_ptr<Token>{m_lexer->next_token()};
    if ((m_error = m_lexer->error()))
      return nullptr;

    if (rp->type() != TokenType::RightParen) {
      m_error = ErrorWithLocation{t->location(), "Unterminated parenthesis"};
      return nullptr;
    }
    m_parentheses_depth--;
    break;
  }

  default:
    if (t->flags() & TokenFlag::UnaryOperator) {
      const TokenOperator *op = static_cast<const TokenOperator *>(t.get());

      std::unique_ptr<Expression> rhs =
          parse_expression(op->unary_precedence());
      if (rhs == nullptr)
        return nullptr;

      lhs = op->construct_unary_expression(rhs.release());
    } else {
      m_error = ErrorWithLocation{t->location(), "Expected a value, found '" +
                                                     t->value() + "'"};
      return nullptr;
    }
    break;
  }

  /* Next goes the precedence parsing. Need to find an operator and decide
   * whether we should go into recursion with more precedence, continue
   * in a loop with the same precedence, or break, if found operator has higher
   * precedence. */
  for (;;) {
    std::unique_ptr<Token> maybe_op{m_lexer->peek_token()};
    if ((m_error = m_lexer->error()))
      return nullptr;
    if (maybe_op->type() == TokenType::EndOfFile)
      return lhs;

    if (maybe_op->type() == TokenType::RightParen) {
      if (m_parentheses_depth == 0) {
        m_error = ErrorWithLocation{maybe_op->location(),
                                    "Unexpected closing parenthesis"};
        return nullptr;
      }
      return lhs;
    }

    if (!(maybe_op->flags() & TokenFlag::BinaryOperator)) {
      m_error = ErrorWithLocation{maybe_op->location(),
                                  "Expected a binary operator, found '" +
                                      maybe_op->value() + "'"};
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
Parser::parse_identifier(const TokenIdentifier *n)
{
  UNUSED(n);
  return std::make_unique<DummyExpression>();
}

std::unique_ptr<Expression>
Parser::parse_string(const TokenString *s)
{
  return std::make_unique<ConstantString>(s->location(), s->value());
}

std::unique_ptr<Expression>
Parser::parse_number(const TokenNumber *n)
{
  i64 value = std::atoll(n->value().data());
  return std::make_unique<ConstantNumber>(n->location(), value);
}
