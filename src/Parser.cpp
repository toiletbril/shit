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
  case Token::Kind::Number:
    lhs = std::make_unique<ConstantNumber>(t->location(),
                                           std::atoll(t->value().data()));
    break;

  case Token::Kind::String:
    lhs = std::make_unique<ConstantString>(t->location(), t->value());
    break;

  case Token::Kind::Dot:
  case Token::Kind::Slash:
  case Token::Kind::Identifier: {
    /* t's value contains a program/path to execute. Parse arguments until we
     * encounter a pipe or an redirection. */
    std::vector<std::string> args;
    std::string              program;

    /* Is this the path of a program? */
    if (t->type() == Token::Kind::Identifier)
      program = t->value();
    else { /* Or it's a dot or a slash? */
      std::unique_ptr<Token> p{m_lexer->next_identifier()};
      /* Concatenate the first char with the rest of the path. */
      program = t->value() + p->value();
    }

    bool should_break = false;

    for (;;) {
      std::unique_ptr<Token> maybe_arg{m_lexer->peek_token()};

      switch (maybe_arg->type()) {
      /* Sentinels. */
      case Token::Kind::EndOfFile:
      case Token::Kind::RightParen: should_break = true; break;

      /* These actually mean something. */
      case Token::Kind::Pipe:
      case Token::Kind::Greater:
      case Token::Kind::LeftParen: DEBUGTRAP("TODO");

      default:
        /* There were no special operators. We are getting arguments to execute
         * a program, so no need to separate anything by other tokens, only by
         * whitespace. */
        maybe_arg.reset(m_lexer->next_identifier());
      }

      if (should_break)
        break;

      args.push_back(maybe_arg->value());
    }

    lhs = std::make_unique<Exec>(t->location(), program, args);
  } break;

  /* Keywords */
  case Token::Kind::If: {
    m_if_depth++;
    /* if expr[;] then [...] [else [then] ...] fi */

    /* condition */
    m_if_condition_depth++;
    std::unique_ptr<Expression> condition = parse_expression();
    m_if_condition_depth--;

    /* [;] then */
    std::unique_ptr<Token> after{m_lexer->next_token()};
    if (after->type() == Token::Kind::Semicolon) {
      after.reset(m_lexer->next_token());
    }
    if (after->type() != Token::Kind::Then) {
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
    if (after->type() == Token::Kind::Else) {
      after.reset(m_lexer->peek_token());
      if (after->type() == Token::Kind::Then)
        m_lexer->advance_past_peek();

      m_if_condition_depth++;
      otherwise = parse_expression();
      m_if_condition_depth--;

      after.reset(m_lexer->next_token());
    }

    /* fi */
    if (after->type() != Token::Kind::Fi) {
      throw ErrorWithLocationAndDetails{
          t->location(), "Unterminated If condition", after->location(),
          "Expected 'Fi' here"};
    }

    lhs = std::make_unique<If>(t->location(), condition.release(),
                               then.release(), otherwise.release());

    m_if_depth--;
  } break;

  /* Blocks */
  case Token::Kind::LeftParen: {
    if (m_parentheses_depth >= 256) {
      throw ErrorWithLocation{t->location(),
                              "Bracket nesting level exceeded maximum of 256"};
    }
    m_parentheses_depth++;

    lhs = parse_expression();

    /* Do we have a corresponding closing parenthesis? */
    std::unique_ptr<Token> rp{m_lexer->next_token()};
    if (rp->type() != Token::Kind::RightParen) {
      throw ErrorWithLocationAndDetails{
          t->location(), "Unterminated parenthesis", rp->location(),
          "Expected a closing parenthesis here"};
    }
    m_parentheses_depth--;
  } break;

  /* Now it's either a unary operator or something odd */
  default:
    if (t->flags() & Token::Flag::UnaryOperator) {
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
    case Token::Kind::EndOfFile: return lhs;
    case Token::Kind::RightParen: {
      if (m_parentheses_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected closing parenthesis"};
      }
      return lhs;
    }
    case Token::Kind::Else:
    case Token::Kind::Fi: {
      if (m_if_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected '" + maybe_op->value() +
                                    "' without matching If condition"};
      }
      return lhs;
    }
    case Token::Kind::Then:
    case Token::Kind::Semicolon: {
      if (m_if_condition_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected '" + maybe_op->value() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    default: break;
    }

    if (!(maybe_op->flags() & Token::Flag::BinaryOperator)) {
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
