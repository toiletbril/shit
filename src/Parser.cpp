#include "Parser.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"

#include <memory>
#include <optional>

namespace shit {

static expressions::SequenceNode::Kind
get_sequence_kind(Token::Kind tk)
{
  switch (tk) {
  case Token::Kind::Newline:
  case Token::Kind::EndOfFile:
  case Token::Kind::Semicolon: return expressions::SequenceNode::Kind::Simple;
  case Token::Kind::DoubleAmpersand:
    return expressions::SequenceNode::Kind::And;
  case Token::Kind::DoublePipe:
    return expressions::SequenceNode::Kind::Or;
    break;

  default: SHIT_UNREACHABLE("Invalid shell sequence token: %d", E(tk));
  }
}

Parser::Parser(Lexer &&lexer) : m_lexer(lexer) {}

Parser::~Parser() = default;

/* Greedy parser for shell commands. Generates a Sequence of expressions::Exec
 * and PipeExec expressions. */
std::unique_ptr<Expression>
Parser::construct_ast()
{
  std::unique_ptr<Expression> lhs{};
  /* Sequence right at the start. */
  std::unique_ptr<expressions::Sequence> sequence =
      std::make_unique<expressions::Sequence>(0);

  bool                            should_parse_command = true;
  expressions::SequenceNode::Kind next_sk =
      expressions::SequenceNode::Kind::Simple;

  for (;;) {
    if (should_parse_command) {
      lhs = parse_shell_command();
    } else {
      should_parse_command = true;
    }

    /* Operator right after. */
    std::unique_ptr<Token> token{m_lexer.peek_shell_token()};

    switch (token->kind()) {
    case Token::Kind::DoublePipe:
    case Token::Kind::DoubleAmpersand:
      if (!lhs) {
        throw shit::ErrorWithLocation{
            token->source_location(),
            "Expected a command " +
                std::string(sequence->empty() ? "before" : "after") +
                " operator, found '" + token->to_ast_string() + "'"};
      }
      /* fallthrough */
    case Token::Kind::Newline:
    case Token::Kind::EndOfFile:
    case Token::Kind::Semicolon: {
      m_lexer.advance_past_last_peek();

      if (lhs) {
        expressions::SequenceNode *sn = new expressions::SequenceNode{
            token->source_location(), next_sk, lhs.release()};
        sequence->append_node(sn);
        next_sk = get_sequence_kind(token->kind());
      }

      /* Terminate the command. */
      if (token->kind() == Token::Kind::EndOfFile) {
        if (next_sk != expressions::SequenceNode::Kind::Simple) {
          throw shit::ErrorWithLocation{token->source_location(),
                                        "Expected a command after an operator"};
        }

        /* Empty input? */
        if (sequence->empty()) {
          SHIT_ASSERT(!lhs);
          return std::make_unique<expressions::DummyExpression>(
              token->source_location());
        } else {
          return sequence;
        }
      }
    } break;

    case Token::Kind::Pipe: {
      if (!lhs) {
        throw shit::ErrorWithLocation{token->source_location(),
                                      "Expected a command before the pipe"};
      }

      m_lexer.advance_past_last_peek();

      /* Don't prematurely release() the pointer, since we can still error out
       * before constructing the expression. */
      std::vector<const expressions::Exec *> pipe_group = {
          static_cast<expressions::Exec *>(lhs.get())};
      usize pipe_group_location = token->source_location();

      std::unique_ptr<Token> last_pipe_token = std::move(token);

      /* Collect a pipe group. */
      for (;;) {
        std::unique_ptr<expressions::Exec> rhs{parse_shell_command()};

        if (rhs) {
          pipe_group.emplace_back(rhs.release());
          last_pipe_token.reset(m_lexer.peek_shell_token());

          if (last_pipe_token->kind() == Token::Kind::Pipe) {
            m_lexer.advance_past_last_peek();
            continue;
          }
        } else {
          throw shit::ErrorWithLocation{last_pipe_token->source_location(),
                                        "Nowhere to pipe output to"};
        }

        break;
      }

      std::ignore = lhs.release();
      lhs = std::make_unique<expressions::ExecPipeSequence>(pipe_group_location,
                                                            pipe_group);

      should_parse_command = false;
    } break;

    case Token::Kind::Less:
    case Token::Kind::Greater:
    case Token::Kind::Ampersand:
      throw ErrorWithLocation{token->source_location(),
                              "Not implemented (Parser)"};

    default:
      throw ErrorWithLocation{token->source_location(),
                              "Expected a shell operator, found '" +
                                  token->to_ast_string() + "'"};
    }
  }

  /* TODO: find a way to indroduce expressions and use parse_expression() */
  SHIT_UNREACHABLE();
}

/* return: expressions::Exec or nullptr if no shell command is present. */
std::unique_ptr<expressions::Exec>
Parser::parse_shell_command()
{
  std::optional<usize>                source_location;
  std::vector<std::unique_ptr<Token>> args_accumulator{};

  for (;;) {
    std::unique_ptr<Token> token{m_lexer.peek_shell_token()};

    switch (token->kind()) {
    case Token::Kind::String: {
      char q = static_cast<const tokens::String *>(token.get())->quote_char();
      if (q == '`') {
        throw ErrorWithLocation{token->source_location(),
                                "Unimplemented quote type"};
      }
    }
    /* fallthrough */
    case Token::Kind::Identifier:
    case Token::Kind::ExpandableIdentifier:
      m_lexer.advance_past_last_peek();
      if (!source_location) {
        source_location = token->source_location();
      }
      args_accumulator.emplace_back(std::move(token));
      break;

    default: {
      if (!source_location) {
        return nullptr;
      }

      std::vector<const Token *> args{};
      args.reserve(args_accumulator.size());

      for (std::unique_ptr<Token> &t : args_accumulator) {
        args.emplace_back(t.release());
      }

      return std::make_unique<expressions::Exec>(*source_location,
                                                 std::move(args));
    }
    }
  }

  SHIT_UNREACHABLE();
}

/* A standard pratt-parser for expressions. */
std::unique_ptr<Expression>
Parser::parse_expression(u8 min_precedence)
{
  m_recursion_depth++;
  SHIT_DEFER { m_recursion_depth--; };

  std::unique_ptr<Token> t{m_lexer.next_expression_token()};

  if (m_recursion_depth > MAX_RECURSION_DEPTH) {
    throw ErrorWithLocation{t->source_location(),
                            "Expression nesting level exceeded maximum of " +
                                std::to_string(MAX_RECURSION_DEPTH)};
  }

  std::unique_ptr<Expression> lhs{};

  /* Handle leaf type. We expect either a value, or an unary operator. */
  switch (t->kind()) {
  /* Values */
  case Token::Kind::Number:
    lhs = std::make_unique<expressions::ConstantNumber>(
        t->source_location(), std::atoll(t->raw_string().data()));
    break;

  case Token::Kind::String:
    lhs = std::make_unique<expressions::ConstantString>(t->source_location(),
                                                        t->raw_string());
    break;

  /* Keywords */
  case Token::Kind::If: {
    /* if expr[;] then [...] [else [then] ...] fi */

    /* condition */
    m_if_condition_depth++;

    std::unique_ptr<Expression> condition = parse_expression();

    /* [;] then */
    std::unique_ptr<Token> after{m_lexer.next_expression_token()};
    if (after->kind() == Token::Kind::Semicolon) {
      after.reset(m_lexer.next_expression_token());
    }
    if (after->kind() != Token::Kind::Then) {
      throw ErrorWithLocation{after->source_location(),
                              "Expected 'Then' after the condition, found '" +
                                  after->to_ast_string() + "'"};
    }

    /* expression */
    std::unique_ptr<Expression> then{parse_expression()};

    std::unique_ptr<Expression> otherwise{};
    after.reset(m_lexer.next_expression_token());

    /* [else [then]] */
    if (after->kind() == Token::Kind::Else) {
      after.reset(m_lexer.peek_expression_token());
      if (after->kind() == Token::Kind::Then)
        m_lexer.advance_past_last_peek();

      otherwise = parse_expression();

      after.reset(m_lexer.next_expression_token());
    }

    /* fi */
    if (after->kind() != Token::Kind::Fi) {
      throw ErrorWithLocationAndDetails{
          t->source_location(), "Unterminated If condition",
          after->source_location(), "Expected 'Fi' here"};
    }

    m_if_condition_depth--;

    lhs = std::make_unique<expressions::If>(t->source_location(),
                                            condition.release(), then.release(),
                                            otherwise.release());
  } break;

  /* Blocks */
  case Token::Kind::LeftParen: {
    if (m_recursion_depth + m_parentheses_depth > MAX_RECURSION_DEPTH) {
      throw ErrorWithLocation{t->source_location(),
                              "Bracket nesting level exceeded maximum of " +
                                  std::to_string(MAX_RECURSION_DEPTH)};
    }

    m_parentheses_depth++;
    lhs = parse_expression();
    m_parentheses_depth--;

    /* Do we have a corresponding closing parenthesis? */
    std::unique_ptr<Token> rp{m_lexer.next_expression_token()};
    if (rp->kind() != Token::Kind::RightParen) {
      throw ErrorWithLocationAndDetails{
          t->source_location(), "Unterminated parenthesis",
          rp->source_location(), "Expected a closing parenthesis here"};
    }
  } break;

  /* Now it's either a unary operator or something odd */
  default:
    if (t->flags() & Token::Flag::UnaryOperator) {
      const tokens::Operator *op =
          static_cast<const tokens::Operator *>(t.get());

      std::unique_ptr<Expression> rhs =
          parse_expression(op->unary_precedence());

      lhs = op->construct_unary_expression(rhs.release());
    } else {
      throw ErrorWithLocation{t->source_location(),
                              "Expected a value or an expression, found '" +
                                  t->raw_string() + "'"};
    }
    break;
  }

  /* Next goes the precedence parsing. Need to find an operator and decide
   * whether we should go into recursion with more precedence, continue
   * in a loop with the same precedence, or break, if found operator has
   * higher precedence. */
  for (;;) {
    std::unique_ptr<Token> maybe_op{m_lexer.peek_expression_token()};

    /* Check for tokens that terminate the parser. */
    switch (maybe_op->kind()) {
    case Token::Kind::EndOfFile:
    case Token::Kind::Semicolon: return lhs;

    case Token::Kind::RightParen: {
      if (m_parentheses_depth == 0) {
        throw ErrorWithLocation{maybe_op->source_location(),
                                "Unexpected closing parenthesis"};
      }
      return lhs;
    }

    case Token::Kind::Else:
    case Token::Kind::Fi: {
      if (m_recursion_depth == 0) {
        throw ErrorWithLocation{maybe_op->source_location(),
                                "Unexpected '" + maybe_op->raw_string() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    case Token::Kind::Then: {
      if (m_if_condition_depth == 0) {
        throw ErrorWithLocation{maybe_op->source_location(),
                                "Unexpected '" + maybe_op->raw_string() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    default: break;
    }

    if (!(maybe_op->flags() & Token::Flag::BinaryOperator)) {
      throw ErrorWithLocation{maybe_op->source_location(),
                              "Expected a binary operator, found '" +
                                  maybe_op->raw_string() + "'"};
    }

    const tokens::Operator *op =
        static_cast<const tokens::Operator *>(maybe_op.get());
    if (op->left_precedence() < min_precedence)
      break;
    m_lexer.advance_past_last_peek();

    std::unique_ptr<Expression> rhs = parse_expression(
        op->left_precedence() + (op->binary_left_associative() ? 1 : -1));
    lhs = op->construct_binary_expression(lhs.release(), rhs.release());
  }

  return lhs;
}

} /* namespace shit */
