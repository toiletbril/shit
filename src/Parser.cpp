#include "Parser.hpp"

#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"

#include <cctype>
#include <memory>
#include <optional>

namespace shit {

using namespace tokens;
using namespace expressions;

static CompoundListCondition::Kind
get_sequence_kind(Token::Kind tk)
{
  switch (tk) {
  case Token::Kind::Newline:
  case Token::Kind::EndOfFile:
  case Token::Kind::Ampersand:
  case Token::Kind::Semicolon:
  case Token::Kind::DoubleSemicolon: return CompoundListCondition::Kind::None;
  case Token::Kind::DoubleAmpersand: return CompoundListCondition::Kind::And;
  case Token::Kind::DoublePipe: return CompoundListCondition::Kind::Or; break;

  default: SHIT_UNREACHABLE("Invalid shell sequence token: %d", SHIT_ENUM(tk));
  }
}

Parser::Parser(Lexer &&lexer) : m_lexer(lexer) {}

Parser::~Parser() = default;

const std::vector<Word> &
Parser::debug_words() const
{
  return m_lexer.debug_words();
}

static bool
kind_in(Token::Kind kind, std::initializer_list<Token::Kind> set)
{
  for (Token::Kind k : set) {
    if (k == kind) return true;
  }
  return false;
}

/* The byte location of the keyword as a whole word somewhere in the source. A
   missing terminator usually means the keyword sits earlier in the input but
   was read as an argument, so the caret can point straight at it. */
static std::optional<SourceLocation>
find_standalone_keyword(std::string_view source, std::string_view keyword)
{
  auto is_boundary = [](char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0 || c == ';' ||
           c == '&' || c == '|';
  };

  usize pos = 0;
  while ((pos = source.find(keyword, pos)) != std::string_view::npos) {
    usize end = pos + keyword.size();
    bool left_ok = pos == 0 || is_boundary(source[pos - 1]);
    bool right_ok = end == source.size() || is_boundary(source[end]);
    if (left_ok && right_ok) return SourceLocation{pos, keyword.size()};
    pos = end;
  }
  return std::nullopt;
}

/* Report a missing terminator. When the keyword is found earlier in the source,
   point the note at it and explain it was read as an argument, otherwise point
   at the token where the terminator was expected. */
[[noreturn]] static void
throw_unterminated(SourceLocation opener, const std::string &what,
                   std::string_view source, const std::string &keyword,
                   SourceLocation fallback)
{
  if (std::optional<SourceLocation> found =
          find_standalone_keyword(source, keyword);
      found.has_value())
  {
    throw ErrorWithLocationAndDetails{
        opener, what, *found,
        "this '" + keyword +
            "' was read as an argument, put a ';' or a newline before it"};
  }
  throw ErrorWithLocationAndDetails{opener, what, fallback,
                                    "expected '" + keyword + "'"};
}

static bool
is_empty_list(const Expression *expression)
{
  return expression->is_dummy();
}

/* A friendly message for a token that cannot start a command. A stray control
   keyword almost always means its opener is missing. */
static std::string
unexpected_command_token_message(const Token *token)
{
  switch (token->kind()) {
  case Token::Kind::Then:
  case Token::Kind::Else:
  case Token::Kind::Elif:
  case Token::Kind::Fi:
    return "'" + token->to_ast_string() + "' has no matching 'if'";
  case Token::Kind::Do:
  case Token::Kind::Done:
    return "'" + token->to_ast_string() +
           "' has no matching 'while', 'until', or 'for'";
  case Token::Kind::Esac: return "'esac' has no matching 'case'";
  case Token::Kind::DoubleSemicolon:
    return "';;' is only valid between the arms of a 'case'";
  case Token::Kind::RightParen: return "')' has no matching '('";
  case Token::Kind::RightBracket: return "'}' has no matching '{'";
  case Token::Kind::Pipe: return "'|' has no command before it to pipe from";
  default: return "expected a command, found '" + token->to_ast_string() + "'";
  }
}

/* A pipeline stage must be a simple command for now. Reject a compound command
   with a clear error instead of an invalid downcast that would corrupt memory.
 */
static SimpleCommand *
require_simple_in_pipeline(std::unique_ptr<Command> command)
{
  if (!command->is_simple_command()) {
    throw ErrorWithLocation{
        command->source_location(),
        "A compound command in a pipeline is not supported"};
  }
  return static_cast<SimpleCommand *>(command.release());
}

/* A token that closes a compound command or a case arm, so a command before it
   is complete. */
static bool
is_compound_terminator(Token::Kind kind)
{
  switch (kind) {
  case Token::Kind::RightParen:
  case Token::Kind::RightBracket:
  case Token::Kind::DoubleSemicolon:
  case Token::Kind::Then:
  case Token::Kind::Do:
  case Token::Kind::Done:
  case Token::Kind::Fi:
  case Token::Kind::Else:
  case Token::Kind::Elif:
  case Token::Kind::Esac: return true;
  default: return false;
  }
}

std::unique_ptr<Expression>
Parser::construct_ast()
{
  /* The top-level list ends only at the end of input. */
  return parse_command_list({});
}

std::unique_ptr<Expression>
Parser::parse_command_list(std::initializer_list<Token::Kind> terminators)
{
  std::unique_ptr<Command> lhs{};

  /* Sequence right at the start. */
  std::unique_ptr<CompoundList> compound_list{
      m_lexer.arena().create<CompoundList>()};
  CompoundListCondition::Kind next_cond = CompoundListCondition::Kind::None;

  bool should_parse_command = true;

  for (;;) {
    if (should_parse_command) {
      lhs = parse_simple_command();
    } else {
      should_parse_command = true;
    }

    /* Operator right after. */
    std::unique_ptr<Token> token{m_lexer.peek_shell_token()};

    /* A terminator keyword ends this list. Append the pending command and leave
       the terminator for the caller to consume. */
    if (kind_in(token->kind(), terminators)) {
      if (lhs) {
        compound_list->append_node(
            m_lexer.arena().create<CompoundListCondition>(
                token->source_location(), next_cond, lhs.release()));
      } else if (next_cond != CompoundListCondition::Kind::None) {
        throw shit::ErrorWithLocation{token->source_location(),
                                      "Expected a command after an operator"};
      }
      if (compound_list->is_empty()) {
        return std::unique_ptr<DummyExpression>{
            m_lexer.arena().create<DummyExpression>(token->source_location())};
      }
      return compound_list;
    }

    switch (token->kind()) {
    /* These operators require a command after them. */
    case Token::Kind::Ampersand:
      if (lhs) lhs->make_async();
      [[fallthrough]];
    case Token::Kind::DoublePipe:
    case Token::Kind::DoubleAmpersand:
      if (!lhs) {
        throw shit::ErrorWithLocation{
            token->source_location(),
            "Expected a command " +
                std::string(compound_list->is_empty() ? "before" : "after") +
                " operator, found '" + token->to_ast_string() + "'"};
      }
      [[fallthrough]];
    case Token::Kind::Newline:
    case Token::Kind::EndOfFile:
    case Token::Kind::DoubleSemicolon:
    case Token::Kind::Semicolon: {
      m_lexer.advance_past_last_peek();

      if (lhs) {
        compound_list->append_node(
            m_lexer.arena().create<CompoundListCondition>(
                token->source_location(), next_cond, lhs.release()));
        next_cond = get_sequence_kind(token->kind());
      }

      /* Terminate the command. */
      if (token->kind() == Token::Kind::EndOfFile) {
        if (next_cond != CompoundListCondition::Kind::None) {
          throw shit::ErrorWithLocation{token->source_location(),
                                        "Expected a command after an operator"};
        }

        /* Empty input? */
        if (compound_list->is_empty()) {
          SHIT_ASSERT(!lhs);
          return std::unique_ptr<DummyExpression>{
              m_lexer.arena().create<DummyExpression>(
                  token->source_location())};
        } else {
          return compound_list;
        }
      }
    } break;

    case Token::Kind::Pipe: {
      if (!lhs) {
        throw shit::ErrorWithLocation{token->source_location(),
                                      "Expected a command before the pipe"};
      }

      m_lexer.advance_past_last_peek();

      std::unique_ptr<Pipeline> pipeline{
          m_lexer.arena().create<Pipeline>(token->source_location())};
      pipeline->append_command(require_simple_in_pipeline(std::move(lhs)));

      std::unique_ptr<Token> last_pipe_token = std::move(token);

      /* Collect a pipe group. */
      for (;;) {
        std::unique_ptr<Command> rhs{parse_simple_command()};

        if (rhs) {
          pipeline->append_command(require_simple_in_pipeline(std::move(rhs)));
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

      lhs = std::move(pipeline);

      should_parse_command = false;
    } break;

    default:
      throw ErrorWithLocation{token->source_location(),
                              unexpected_command_token_message(token.get())};
    }
  }

  SHIT_UNREACHABLE();
}

/* return: a command, a compound command, or nullptr when a list terminator is
   next. A reserved word or a group opener in command position introduces a
   compound command. */
std::unique_ptr<Command>
Parser::parse_simple_command()
{
  std::optional<SourceLocation> source_location;
  std::vector<std::unique_ptr<Token>> args_accumulator{};
  std::unordered_map<std::string, Word> local_vars{};

  auto build_command = [&]() -> std::unique_ptr<Command> {
    if (!source_location) return nullptr;

    std::vector<const Token *> args{};
    args.reserve(args_accumulator.size());
    for (std::unique_ptr<Token> &t : args_accumulator)
      args.emplace_back(t.release());

    std::unique_ptr<SimpleCommand> c{m_lexer.arena().create<SimpleCommand>(
        *source_location, std::move(args))};
    if (!local_vars.empty()) c->set_local_vars(std::move(local_vars));
    return c;
  };

  for (;;) {
    std::unique_ptr<Token> token{m_lexer.peek_shell_token()};

    /* A reserved word or a group opener in command position introduces a
       compound command. A list terminator means there is no command here. */
    if (args_accumulator.empty() && local_vars.empty()) {
      switch (token->kind()) {
      case Token::Kind::If: return parse_if();
      case Token::Kind::While: return parse_while_or_until(false);
      case Token::Kind::Until: return parse_while_or_until(true);
      case Token::Kind::For: return parse_for();
      case Token::Kind::Case: return parse_case();
      case Token::Kind::LeftBracket: return parse_brace_group();
      case Token::Kind::LeftParen: return parse_subshell();

      case Token::Kind::Then:
      case Token::Kind::Do:
      case Token::Kind::Done:
      case Token::Kind::Fi:
      case Token::Kind::Else:
      case Token::Kind::Elif:
      case Token::Kind::Esac:
      case Token::Kind::RightBracket:
      case Token::Kind::RightParen:
      case Token::Kind::DoubleSemicolon: return nullptr;

      default: break;
      }
    }

    switch (token->kind()) {
    /* A reserved word that is not in command position is an ordinary word. */
    case Token::Kind::Word:
    case Token::Kind::If:
    case Token::Kind::Then:
    case Token::Kind::Else:
    case Token::Kind::Elif:
    case Token::Kind::Fi:
    case Token::Kind::While:
    case Token::Kind::Until:
    case Token::Kind::For:
    case Token::Kind::Do:
    case Token::Kind::Done:
    case Token::Kind::Case:
    case Token::Kind::Esac:
    case Token::Kind::Time:
    case Token::Kind::When:
    case Token::Kind::Function:
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = token->source_location();
      args_accumulator.emplace_back(std::move(token));
      break;

    case Token::Kind::LeftParen:
      /* A single word followed by () is a function definition. */
      if (args_accumulator.size() == 1 && local_vars.empty() &&
          args_accumulator[0]->kind() == Token::Kind::Word)
      {
        return parse_function_definition(std::move(args_accumulator[0]));
      }
      return build_command();

    case Token::Kind::Assignment: {
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = token->source_location();

      /* Once a command word is present, an assignment-looking token is just an
       * ordinary argument. */
      if (!args_accumulator.empty()) {
        args_accumulator.emplace_back(std::move(token));
        break;
      }

      std::unique_ptr<Assignment> a{static_cast<Assignment *>(token.release())};

      /* Peek the next token. A compound list condition, a compound terminator,
       * or the end of input means the assignment stands alone. */
      std::unique_ptr<Token> next{m_lexer.peek_shell_token()};
      if (next->flags() & Token::Flag::CompoundList ||
          next->kind() == Token::Kind::EndOfFile ||
          is_compound_terminator(next->kind()))
      {
        return std::unique_ptr<AssignCommand>{
            m_lexer.arena().create<AssignCommand>(*source_location,
                                                  a.release())};
      } else {
        /* Single-command variable. */
        local_vars[a->key()] = a->value_word();
      }
    } break;

    /* A separator, an operator, or a list terminator ends the command. */
    default: return build_command();
    }
  }

  SHIT_UNREACHABLE();
}

std::unique_ptr<Command>
Parser::parse_if()
{
  std::unique_ptr<Token> if_token{m_lexer.next_shell_token()};
  SourceLocation location = if_token->source_location();

  std::vector<std::pair<const Expression *, const Expression *>> branches{};
  const Expression *otherwise = nullptr;
  /* Free the released branch nodes if a later branch fails to parse. */
  SHIT_DEFER
  {
    for (auto &[condition, body] : branches) {
      delete condition;
      delete body;
    }
    delete otherwise;
  };

  for (;;) {
    std::unique_ptr<Expression> condition{
        parse_command_list({Token::Kind::Then})};
    std::unique_ptr<Token> then_token{m_lexer.next_shell_token()};
    if (then_token->kind() != Token::Kind::Then) {
      const char *detail = is_empty_list(condition.get())
                               ? "expected a command for the condition"
                               : "expected 'then' after the condition";
      throw ErrorWithLocationAndDetails{location, "Unterminated if",
                                        then_token->source_location(), detail};
    }

    std::unique_ptr<Expression> body{parse_command_list(
        {Token::Kind::Elif, Token::Kind::Else, Token::Kind::Fi})};
    branches.emplace_back(condition.release(), body.release());

    std::unique_ptr<Token> after{m_lexer.next_shell_token()};
    if (after->kind() == Token::Kind::Elif) {
      continue;
    } else if (after->kind() == Token::Kind::Else) {
      otherwise = parse_command_list({Token::Kind::Fi}).release();
      std::unique_ptr<Token> fi_token{m_lexer.next_shell_token()};
      if (fi_token->kind() != Token::Kind::Fi) {
        throw_unterminated(location, "Unterminated if", m_lexer.source(), "fi",
                           fi_token->source_location());
      }
      break;
    } else if (after->kind() == Token::Kind::Fi) {
      break;
    } else {
      throw_unterminated(location, "Unterminated if", m_lexer.source(), "fi",
                         after->source_location());
    }
  }

  std::unique_ptr<IfClause> node{m_lexer.arena().create<IfClause>(
      location, std::move(branches), otherwise)};
  /* Ownership of the else body moved into the node, so the cleanup guard must
     not also free it. The branches vector was moved from and is now empty. */
  otherwise = nullptr;
  return node;
}

std::unique_ptr<Command>
Parser::parse_while_or_until(bool is_until)
{
  std::unique_ptr<Token> keyword{m_lexer.next_shell_token()};
  SourceLocation location = keyword->source_location();

  std::unique_ptr<Expression> condition{parse_command_list({Token::Kind::Do})};
  std::unique_ptr<Token> do_token{m_lexer.next_shell_token()};
  if (do_token->kind() != Token::Kind::Do) {
    const char *detail = is_empty_list(condition.get())
                             ? "expected a command for the loop condition"
                             : "expected 'do'";
    throw ErrorWithLocationAndDetails{location, "Unterminated loop",
                                      do_token->source_location(), detail};
  }

  std::unique_ptr<Expression> body{parse_command_list({Token::Kind::Done})};
  std::unique_ptr<Token> done_token{m_lexer.next_shell_token()};
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated loop", m_lexer.source(), "done",
                       done_token->source_location());
  }

  return std::unique_ptr<WhileLoop>{m_lexer.arena().create<WhileLoop>(
      location, condition.release(), body.release(), is_until)};
}

std::unique_ptr<Command>
Parser::parse_for()
{
  std::unique_ptr<Token> keyword{m_lexer.next_shell_token()};
  SourceLocation location = keyword->source_location();

  std::unique_ptr<Token> name_token{m_lexer.next_shell_token()};
  if (name_token->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{name_token->source_location(),
                            "Expected a variable name after 'for'"};
  }
  std::string variable_name = name_token->raw_string();

  std::vector<const Token *> words{};
  /* Free the released word tokens if the loop fails to parse. */
  SHIT_DEFER
  {
    for (const Token *word : words)
      delete word;
  };
  bool has_in_clause = false;

  /* An optional 'in WORDS' clause. The word 'in' is not a keyword token. */
  std::unique_ptr<Token> peeked{m_lexer.peek_shell_token()};
  if (peeked->kind() == Token::Kind::Word && peeked->raw_string() == "in") {
    m_lexer.advance_past_last_peek();
    has_in_clause = true;
    for (;;) {
      std::unique_ptr<Token> word{m_lexer.peek_shell_token()};
      if (word->kind() != Token::Kind::Word) break;
      m_lexer.advance_past_last_peek();
      words.emplace_back(word.release());
    }
  }

  /* Skip the separators between the header and 'do'. */
  for (;;) {
    std::unique_ptr<Token> t{m_lexer.peek_shell_token()};
    if (t->kind() == Token::Kind::Semicolon ||
        t->kind() == Token::Kind::Newline)
    {
      m_lexer.advance_past_last_peek();
      continue;
    }
    break;
  }

  std::unique_ptr<Token> do_token{m_lexer.next_shell_token()};
  if (do_token->kind() != Token::Kind::Do) {
    std::string detail = "expected 'do'";
    if (!has_in_clause) {
      detail = "expected 'do', or 'in WORDS' before it; without 'in' the loop "
               "walks the positional parameters";
    }
    throw ErrorWithLocationAndDetails{location, "Unterminated for loop",
                                      do_token->source_location(), detail};
  }

  std::unique_ptr<Expression> body{parse_command_list({Token::Kind::Done})};
  std::unique_ptr<Token> done_token{m_lexer.next_shell_token()};
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated for loop", m_lexer.source(),
                       "done", done_token->source_location());
  }

  return std::unique_ptr<ForLoop>{m_lexer.arena().create<ForLoop>(
      location, std::move(variable_name), std::move(words), has_in_clause,
      body.release())};
}

std::unique_ptr<Command>
Parser::parse_case()
{
  std::unique_ptr<Token> keyword{m_lexer.next_shell_token()};
  SourceLocation location = keyword->source_location();

  std::unique_ptr<Token> word{m_lexer.next_shell_token()};
  if (word->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{word->source_location(),
                            "Expected a word to match on after 'case'"};
  }

  std::unique_ptr<Token> in_token{m_lexer.next_shell_token()};
  if (!(in_token->kind() == Token::Kind::Word &&
        in_token->raw_string() == "in"))
  {
    throw ErrorWithLocation{in_token->source_location(),
                            "Expected 'in' after the case word"};
  }

  std::vector<CaseItem> items{};
  /* A parse error before the clause is built abandons these arena nodes, so
     free their tokens and bodies to keep the leak checker happy. */
  SHIT_DEFER
  {
    for (CaseItem &item : items) {
      for (const Token *pattern : item.patterns)
        delete pattern;
      delete item.body;
    }
  };

  for (;;) {
    std::unique_ptr<Token> t{m_lexer.peek_shell_token()};

    if (t->kind() == Token::Kind::Newline ||
        t->kind() == Token::Kind::Semicolon)
    {
      m_lexer.advance_past_last_peek();
      continue;
    }
    if (t->kind() == Token::Kind::Esac) {
      m_lexer.advance_past_last_peek();
      break;
    }

    /* An optional opening parenthesis before the first pattern. */
    if (t->kind() == Token::Kind::LeftParen) m_lexer.advance_past_last_peek();

    std::vector<const Token *> patterns{};
    SHIT_DEFER
    {
      for (const Token *pattern : patterns)
        delete pattern;
    };

    for (;;) {
      std::unique_ptr<Token> pattern{m_lexer.next_shell_token()};
      if (pattern->kind() != Token::Kind::Word) {
        throw ErrorWithLocationAndDetails{
            location, "Unterminated case", pattern->source_location(),
            "expected a pattern to start an arm, or 'esac' to end the case"};
      }
      patterns.emplace_back(pattern.release());

      std::unique_ptr<Token> separator{m_lexer.next_shell_token()};
      if (separator->kind() == Token::Kind::Pipe) continue;
      if (separator->kind() == Token::Kind::RightParen) break;
      throw ErrorWithLocation{separator->source_location(),
                              "Expected '|' or ')' in a case pattern"};
    }

    std::unique_ptr<Expression> body{
        parse_command_list({Token::Kind::DoubleSemicolon, Token::Kind::Esac})};
    items.push_back(CaseItem{std::move(patterns), body.release()});

    std::unique_ptr<Token> after{m_lexer.peek_shell_token()};
    if (after->kind() == Token::Kind::DoubleSemicolon) {
      m_lexer.advance_past_last_peek();
    } else if (after->kind() == Token::Kind::Esac) {
      m_lexer.advance_past_last_peek();
      break;
    }
  }

  return std::unique_ptr<CaseClause>{m_lexer.arena().create<CaseClause>(
      location, word.release(), std::move(items))};
}

std::unique_ptr<Command>
Parser::parse_brace_group()
{
  std::unique_ptr<Token> open{m_lexer.next_shell_token()};

  std::unique_ptr<Expression> body{
      parse_command_list({Token::Kind::RightBracket})};

  std::unique_ptr<Token> close{m_lexer.next_shell_token()};
  if (close->kind() != Token::Kind::RightBracket) {
    throw ErrorWithLocationAndDetails{open->source_location(),
                                      "Unterminated brace group",
                                      close->source_location(), "expected '}'"};
  }

  return std::unique_ptr<BraceGroup>{m_lexer.arena().create<BraceGroup>(
      open->source_location(), body.release())};
}

std::unique_ptr<Command>
Parser::parse_subshell()
{
  std::unique_ptr<Token> open{m_lexer.next_shell_token()};

  std::unique_ptr<Expression> body{
      parse_command_list({Token::Kind::RightParen})};

  std::unique_ptr<Token> close{m_lexer.next_shell_token()};
  if (close->kind() != Token::Kind::RightParen) {
    throw ErrorWithLocationAndDetails{open->source_location(),
                                      "Unterminated subshell",
                                      close->source_location(), "expected ')'"};
  }

  return std::unique_ptr<Subshell>{m_lexer.arena().create<Subshell>(
      open->source_location(), body.release())};
}

std::unique_ptr<Command>
Parser::parse_function_definition(std::unique_ptr<Token> name_token)
{
  SourceLocation location = name_token->source_location();
  std::string name = name_token->raw_string();

  /* The opening parenthesis was peeked by the caller. Consume the empty pair.
   */
  m_lexer.advance_past_last_peek();
  std::unique_ptr<Token> close{m_lexer.next_shell_token()};
  if (close->kind() != Token::Kind::RightParen) {
    throw ErrorWithLocation{close->source_location(),
                            "Expected ')' in a function definition"};
  }

  /* Skip newlines before the body. */
  for (;;) {
    std::unique_ptr<Token> t{m_lexer.peek_shell_token()};
    if (t->kind() != Token::Kind::Newline) break;
    m_lexer.advance_past_last_peek();
  }

  std::unique_ptr<Command> body{parse_simple_command()};
  if (!body) {
    throw ErrorWithLocation{location,
                            "Expected a compound command as the function body"};
  }

  return std::unique_ptr<FunctionDefinition>{
      m_lexer.arena().create<FunctionDefinition>(location, std::move(name),
                                                 body.release())};
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
    lhs =
        std::unique_ptr<ConstantNumber>{m_lexer.arena().create<ConstantNumber>(
            t->source_location(), std::atoll(t->raw_string().data()))};
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
      if (after->kind() == Token::Kind::Then) m_lexer.advance_past_last_peek();

      otherwise = parse_expression();

      after.reset(m_lexer.next_expression_token());
    }

    /* fi */
    if (after->kind() != Token::Kind::Fi) {
      throw ErrorWithLocationAndDetails{
          t->source_location(), "Unterminated If condition",
          after->source_location(), "expected 'Fi' here"};
    }

    m_if_condition_depth--;

    lhs = std::unique_ptr<IfStatement>{m_lexer.arena().create<IfStatement>(
        t->source_location(), condition.release(), then.release(),
        otherwise.release())};
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
          rp->source_location(), "expected a closing parenthesis here"};
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
    if (op->left_precedence() < min_precedence) break;
    m_lexer.advance_past_last_peek();

    std::unique_ptr<Expression> rhs = parse_expression(
        op->left_precedence() + (op->binary_left_associative() ? 1 : -1));
    lhs = op->construct_binary_expression(lhs.release(), rhs.release());
  }

  return lhs;
}

} /* namespace shit */
