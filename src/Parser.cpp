#include "Parser.hpp"

#include "Expressions.hpp"
#include "Tokens.hpp"

#include <optional>

Parser::Parser(Lexer *lexer) : m_lexer(lexer) {}
Parser::~Parser() { delete m_lexer; }

std::unique_ptr<Expression>
Parser::construct_ast()
{
  return parse_expression();
}

/* A standard pratt-parser for expressions. */
std::unique_ptr<Expression>
Parser::parse_expression(u8 min_precedence)
{
  std::unique_ptr<Expression> lhs{};
  std::unique_ptr<Token>      t{m_lexer->next_token()};

  /* Handle leaf type. We expect either a value, or an unary operator. */
  switch (t->type()) {
  /* Values */
  case TokenType::Number:
    lhs = parse_number(static_cast<const TokenNumber *>(t.get()));
    break;

  case TokenType::String:
    lhs = parse_string(static_cast<const TokenString *>(t.get()));
    break;

  case TokenType::Identifier:
    lhs = parse_identifier(static_cast<const TokenIdentifier *>(t.get()));
    break;

  /* Keywords */
  case TokenType::If: {
    m_if_depth++;
    /* if expr[;] then [...] [else [then] ...] fi */

    /* condition */
    m_if_condition_depth++;
    std::unique_ptr<Expression> condition = parse_expression();
    m_if_condition_depth--;

    /* [;] then */
    std::unique_ptr<Token> after{m_lexer->next_token()};
    if (after->type() == TokenType::Semicolon) {
      after.reset(m_lexer->next_token());
    }
    if (after->type() != TokenType::Then) {
      throw ErrorWithLocation{after->location(),
                              "Expected 'Then' after the condition, found '" +
                                  after->to_ast_string() + "'"};
    }

    /* expression */
    std::unique_ptr<Expression> then;
    then = parse_expression();

    std::unique_ptr<Expression> otherwise{};
    after.reset(m_lexer->next_token());

    /* [else [then]] */
    if (after->type() == TokenType::Else) {
      after.reset(m_lexer->peek_token());

      if (after->type() == TokenType::Then)
        m_lexer->advance_past_peek();

      m_if_condition_depth++;
      otherwise = parse_expression();
      m_if_condition_depth--;

      after.reset(m_lexer->next_token());
    }

    /* fi */
    if (after->type() != TokenType::Fi) {
      throw ErrorWithLocationAndDetails{
          t->location(), "Unterminated If condition", after->location(),
          "Expected 'Fi' here"};
    }

    lhs = std::make_unique<If>(t->location(), condition.release(),
                               then.release(), otherwise.release());

    m_if_depth--;
  } break;

  /* Blocks */
  case TokenType::LeftParen: {
    if (m_parentheses_depth >= 256) {
      throw ErrorWithLocation{t->location(),
                              "Bracket nesting level exceeded maximum of 256"};
    }
    m_parentheses_depth++;

    lhs = parse_expression();

    /* Do we have a corresponding closing parenthesis? */
    std::unique_ptr<Token> rp{m_lexer->next_token()};
    if (rp->type() != TokenType::RightParen) {
      throw new ErrorWithLocationAndDetails{
          t->location(), "Unterminated parenthesis", rp->location(),
          "Expected a closing parenthesis here"};
    }
    m_parentheses_depth--;
  } break;

  /* Now it's either a unary operator or something odd */
  default:
    if (t->flags() & TokenFlag::UnaryOperator) {
      const TokenOperator *op = static_cast<const TokenOperator *>(t.get());

      std::unique_ptr<Expression> rhs =
          parse_expression(op->unary_precedence());

      lhs = op->construct_unary_expression(rhs.release());
    } else {
      throw ErrorWithLocation{t->location(),
                              "Expected a value or an expression, found '" +
                                  t->value() + "'"};
    }
    break;
  }

  /* Next goes the precedence parsing. Need to find an operator and decide
   * whether we should go into recursion with more precedence, continue
   * in a loop with the same precedence, or break, if found operator has higher
   * precedence. */
  for (;;) {
    std::unique_ptr<Token> maybe_op{m_lexer->peek_token()};

    /* Check for tokens that terminate the parser. */
    switch (maybe_op->type()) {
    case TokenType::EndOfFile: return lhs;

    case TokenType::RightParen: {
      if (m_parentheses_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected closing parenthesis"};
      }
      return lhs;
    }

    case TokenType::Else:
    case TokenType::Fi: {
      if (m_if_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected '" + maybe_op->value() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    case TokenType::Then:
    case TokenType::Semicolon: {
      if (m_if_condition_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected '" + maybe_op->value() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    default: break;
    }

    if (!(maybe_op->flags() & TokenFlag::BinaryOperator)) {
      throw ErrorWithLocation{maybe_op->location(),
                              "Expected a binary operator, found '" +
                                  maybe_op->value() + "'"};
    }

    const TokenOperator *op =
        static_cast<const TokenOperator *>(maybe_op.get());
    if (op->left_precedence() < min_precedence)
      break;
    m_lexer->advance_past_peek();

    std::unique_ptr<Expression> rhs = parse_expression(
        op->left_precedence() + (op->binary_left_associative() ? 1 : -1));
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
