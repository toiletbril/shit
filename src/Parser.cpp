#include "Parser.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <memory>

namespace shit {

static SequenceNode::Kind
get_sequence_kind(Token::Kind tk)
{
  switch (tk) {
  case Token::Kind::EndOfFile:
  case Token::Kind::Semicolon: return SequenceNode::Kind::Simple;
  case Token::Kind::DoubleAmpersand: return SequenceNode::Kind::And;
  case Token::Kind::DoublePipe: return SequenceNode::Kind::Or; break;
  default: SHIT_UNREACHABLE("Invalid shell sequence token: %d", E(tk));
  }
}

Parser::Parser(Lexer *lexer) : m_lexer(lexer) {}
Parser::~Parser() { delete m_lexer; }

std::unique_ptr<Expression>
Parser::construct_ast()
{
  return parse_command();
}

std::optional<std::unique_ptr<Exec>>
Parser::parse_exec()
{
  std::vector<std::string>              args_accumulator{};
  std::optional<std::unique_ptr<Token>> program_token{};

  for (;;) {
    std::unique_ptr<Token> token{m_lexer->peek_shell_token()};

    switch (token->kind()) {
    case Token::Kind::Identifier:
    case Token::Kind::String:
      m_lexer->advance_past_last_peek();
      if (!program_token) {
        program_token = std::move(token);
      } else {
        args_accumulator.emplace_back(token->value());
      }
      break;

    default:
      if (!program_token) {
        return std::nullopt;
      }
      const Token *pt = program_token.value().get();
      return std::make_unique<Exec>(pt->location(), pt->value(),
                                    args_accumulator);
    }
  }

  SHIT_UNREACHABLE();
}

/* Generates a Sequence of Exec and PipeExec expressions. */
std::unique_ptr<Expression>
Parser::parse_command()
{
  std::optional<std::unique_ptr<Expression>> lhs{};
  std::vector<std::unique_ptr<SequenceNode>> nodes{};

  bool               should_parse_command = true;
  SequenceNode::Kind next_sk = SequenceNode::Kind::Simple;

  for (;;) {
    if (should_parse_command) {
      lhs = parse_exec();
    } else {
      should_parse_command = true;
    }

    /* Operator right after. */
    std::unique_ptr<Token> token{m_lexer->peek_shell_token()};

    switch (token->kind()) {
    case Token::Kind::DoublePipe:
    case Token::Kind::DoubleAmpersand:
      /* Two operators back to back, error */
      if (!lhs) {
        throw shit::ErrorWithLocation{
            token->location(), "Expected a value after an operator, found an " +
                                   token->to_ast_string()};
      }
      /* fallthrough */
    case Token::Kind::EndOfFile:
    case Token::Kind::Semicolon: {
      m_lexer->advance_past_last_peek();

      if (lhs) {
        SequenceNode *sn =
            new SequenceNode{token->location(), next_sk, lhs.value().release()};

        nodes.emplace_back(sn);

        next_sk = get_sequence_kind(token->kind());
      }

      if (token->kind() != Token::Kind::EndOfFile && !nodes.empty() && !lhs) {
        /* We have Nothing after the operator, error. */
        throw shit::ErrorWithLocation{token->location(),
                                      "Expected a value after an operator"};
      }

      if (token->kind() == Token::Kind::EndOfFile) {
        if (nodes.empty()) {
          /* This exec is the whole expression? */
          return std::unique_ptr<Expression>(std::move(lhs.value()));
        } else {
          /* Form a sequence. */
          std::vector<const SequenceNode *> sequence_nodes{};
          sequence_nodes.reserve(nodes.size());
          for (std::unique_ptr<SequenceNode> &e : nodes) {
            sequence_nodes.emplace_back(e.release());
          }
          return std::make_unique<Sequence>(0, sequence_nodes);
        }
      }
    } break;

    case Token::Kind::Pipe: {
      m_lexer->advance_past_last_peek();

      std::vector<const Exec *> pipe_group = {
          static_cast<Exec *>(lhs->release())};

      /* Collect a pipe group. */
      for (;;) {
        std::optional<std::unique_ptr<Exec>> rhs{parse_exec()};

        if (rhs) {
          pipe_group.push_back(rhs->release());
          std::unique_ptr<Token> maybe_pipe{m_lexer->peek_shell_token()};

          if (maybe_pipe->kind() == Token::Kind::Pipe) {
            m_lexer->advance_past_last_peek();
            continue;
          }
        }

        break;
      }

      if (pipe_group.size() < 2) {
        throw shit::ErrorWithLocation{token->location(),
                                      "Nowhere to pipe output to"};
      }

      lhs = std::make_unique<ExecPipeSequence>(token->location(), pipe_group);

      should_parse_command = false;
    } break;

    case Token::Kind::Greater:
      throw ErrorWithLocation{token->location(), "Not implemented (Parser)"};

    default:
      throw ErrorWithLocation{token->location(),
                              "Expected a shell operator, found '" +
                                  token->to_ast_string() + "'"};
    }
  }

  /* TODO: find a way to indroduce expressions and use parse_expression() */
  SHIT_UNREACHABLE();
}

/* A standard pratt-parser for expressions. */
std::unique_ptr<Expression>
Parser::parse_expression(u8 min_precedence)
{
  m_recursion_depth++;
  SHIT_DEFER { m_recursion_depth--; };

  std::unique_ptr<Token> t{m_lexer->next_expression_token()};

  if (m_recursion_depth > MAX_RECURSION_DEPTH) {
    throw ErrorWithLocation{t->location(),
                            "Expression nesting level exceeded maximum of " +
                                std::to_string(MAX_RECURSION_DEPTH)};
  }

  std::unique_ptr<Expression> lhs{};

  /* Handle leaf type. We expect either a value, or an unary operator. */
  switch (t->kind()) {
  /* Values */
  case Token::Kind::Number:
    lhs = std::make_unique<ConstantNumber>(t->location(),
                                           std::atoll(t->value().data()));
    break;

  case Token::Kind::String:
    lhs = std::make_unique<ConstantString>(t->location(), t->value());
    break;

  /* Keywords */
  case Token::Kind::If: {
    /* if expr[;] then [...] [else [then] ...] fi */

    /* condition */
    m_if_condition_depth++;

    std::unique_ptr<Expression> condition = parse_expression();

    /* [;] then */
    std::unique_ptr<Token> after{m_lexer->next_expression_token()};
    if (after->kind() == Token::Kind::Semicolon) {
      after.reset(m_lexer->next_expression_token());
    }
    if (after->kind() != Token::Kind::Then) {
      throw ErrorWithLocation{after->location(),
                              "Expected 'Then' after the condition, found '" +
                                  after->to_ast_string() + "'"};
    }

    /* expression */
    std::unique_ptr<Expression> then{parse_expression()};

    std::unique_ptr<Expression> otherwise{};
    after.reset(m_lexer->next_expression_token());

    /* [else [then]] */
    if (after->kind() == Token::Kind::Else) {
      after.reset(m_lexer->peek_expression_token());
      if (after->kind() == Token::Kind::Then)
        m_lexer->advance_past_last_peek();

      otherwise = parse_expression();

      after.reset(m_lexer->next_expression_token());
    }

    /* fi */
    if (after->kind() != Token::Kind::Fi) {
      throw ErrorWithLocationAndDetails{
          t->location(), "Unterminated If condition", after->location(),
          "Expected 'Fi' here"};
    }

    m_if_condition_depth--;

    lhs = std::make_unique<If>(t->location(), condition.release(),
                               then.release(), otherwise.release());
  } break;

  /* Blocks */
  case Token::Kind::LeftParen: {
    if (m_recursion_depth + m_parentheses_depth > MAX_RECURSION_DEPTH) {
      throw ErrorWithLocation{t->location(),
                              "Bracket nesting level exceeded maximum of " +
                                  std::to_string(MAX_RECURSION_DEPTH)};
    }

    m_parentheses_depth++;
    lhs = parse_expression();
    m_parentheses_depth--;

    /* Do we have a corresponding closing parenthesis? */
    std::unique_ptr<Token> rp{m_lexer->next_expression_token()};
    if (rp->kind() != Token::Kind::RightParen) {
      throw ErrorWithLocationAndDetails{
          t->location(), "Unterminated parenthesis", rp->location(),
          "Expected a closing parenthesis here"};
    }
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
   * in a loop with the same precedence, or break, if found operator has
   * higher precedence. */
  for (;;) {
    std::unique_ptr<Token> maybe_op{m_lexer->peek_expression_token()};

    /* Check for tokens that terminate the parser. */
    switch (maybe_op->kind()) {
    case Token::Kind::EndOfFile:
    case Token::Kind::Semicolon: return lhs;

    case Token::Kind::RightParen: {
      if (m_parentheses_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected closing parenthesis"};
      }
      return lhs;
    }

    case Token::Kind::Else:
    case Token::Kind::Fi: {
      if (m_recursion_depth == 0) {
        throw ErrorWithLocation{maybe_op->location(),
                                "Unexpected '" + maybe_op->value() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    case Token::Kind::Then: {
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
    m_lexer->advance_past_last_peek();

    std::unique_ptr<Expression> rhs = parse_expression(
        op->left_precedence() + (op->binary_left_associative() ? 1 : -1));
    lhs = op->construct_binary_expression(lhs.release(), rhs.release());
  }

  return lhs;
}

} // namespace shit
