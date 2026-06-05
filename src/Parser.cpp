#include "Parser.hpp"

#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

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

Parser::Parser(Lexer &&lexer) : m_lexer(std::move(lexer)) {}

Parser::~Parser() = default;

const ArrayList<Word> &
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
static Maybe<SourceLocation>
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
  return shit::None;
}

/* Report a missing terminator. When the keyword is found earlier in the source,
   point the note at it and explain it was read as an argument, otherwise point
   at the token where the terminator was expected. */
[[noreturn]] static void
throw_unterminated(SourceLocation opener, const std::string &what,
                   std::string_view source, const std::string &keyword,
                   SourceLocation fallback)
{
  if (Maybe<SourceLocation> found = find_standalone_keyword(source, keyword);
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

/* The reserved word ! is a single unquoted exclamation mark standing alone in
   command position. It is distinct from a != comparison or a quoted literal. */
static bool
is_negation_token(const Token *token)
{
  if (token->kind() != Token::Kind::Word) return false;
  const Word &word = static_cast<const tokens::WordToken *>(token)->word();
  return word.segments.size() == 1 &&
         word.segments[0].kind == WordSegment::Kind::UnquotedText &&
         word.segments[0].text == "!";
}

/* A friendly message for a token that cannot start a command. A stray control
   keyword almost always means its opener is missing. */
static String
unexpected_command_token_message(const Token *token)
{
  switch (token->kind()) {
  case Token::Kind::Then:
  case Token::Kind::Else:
  case Token::Kind::Elif:
  case Token::Kind::Fi: {
    String ast = token->to_ast_string();
    return "'" + ast.view() + "' has no matching 'if'";
  }
  case Token::Kind::Do:
  case Token::Kind::Done: {
    String ast = token->to_ast_string();
    return "'" + ast.view() + "' has no matching 'while', 'until', or 'for'";
  }
  case Token::Kind::Esac: return "'esac' has no matching 'case'";
  case Token::Kind::DoubleSemicolon:
    return "';;' is only valid between the arms of a 'case'";
  case Token::Kind::RightParen: return "')' has no matching '('";
  case Token::Kind::RightBracket: return "'}' has no matching '{'";
  case Token::Kind::Pipe: return "'|' has no command before it to pipe from";
  default: {
    String ast = token->to_ast_string();
    return "expected a command, found '" + ast.view() + "'";
  }
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
  bool negate_pending = false;

  for (;;) {
    if (should_parse_command) {
      /* A leading ! negates the pipeline that follows. */
      std::unique_ptr<Token> maybe_negation{m_lexer.peek_shell_token()};
      if (is_negation_token(maybe_negation.get())) {
        m_lexer.advance_past_last_peek();
        negate_pending = true;
      }
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
        if (negate_pending) {
          lhs->set_negated();
          negate_pending = false;
        }
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
        String ast = token->to_ast_string();
        String message = "Expected a command ";
        message += compound_list->is_empty() ? "before" : "after";
        message += " operator, found '";
        message += ast.view();
        message += "'";
        throw shit::ErrorWithLocation{token->source_location(), message};
      }
      [[fallthrough]];
    case Token::Kind::Newline:
    case Token::Kind::EndOfFile:
    case Token::Kind::DoubleSemicolon:
    case Token::Kind::Semicolon: {
      m_lexer.advance_past_last_peek();

      if (lhs) {
        if (negate_pending) {
          lhs->set_negated();
          negate_pending = false;
        }
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
  Maybe<SourceLocation> source_location;
  ArrayList<std::unique_ptr<Token>> args_accumulator{};
  HashMap<Word> local_vars{heap_allocator()};
  ArrayList<expressions::Redirection> redirections{};

  auto build_command = [&]() -> std::unique_ptr<Command> {
    if (!source_location) return nullptr;

    ArrayList<const Token *> args{};
    args.reserve(args_accumulator.size());
    for (std::unique_ptr<Token> &t : args_accumulator)
      args.push(t.release());

    std::unique_ptr<SimpleCommand> c{m_lexer.arena().create<SimpleCommand>(
        *source_location, std::move(args))};
    if (local_vars.size() != 0) c->set_local_vars(std::move(local_vars));
    if (!redirections.empty()) c->set_redirections(std::move(redirections));
    return c;
  };

  /* Build one redirection for descriptor fd. The operator is already consumed,
     and op_location is its position. A & touching the operator means a
     descriptor duplication, n>&m, otherwise a filename word follows. */
  auto add_redirection = [&](i32 fd, Token::Kind op_kind,
                             SourceLocation op_location) {
    if (!source_location) source_location = op_location;

    expressions::Redirection redirection{};
    redirection.fd = fd;
    redirection.target = nullptr;
    redirection.dup_fd = -1;

    if (op_kind != Token::Kind::Less) {
      std::unique_ptr<Token> after{m_lexer.peek_shell_token()};
      if (after->kind() == Token::Kind::Ampersand &&
          after->source_location().position() ==
              op_location.position() + op_location.length())
      {
        m_lexer.advance_past_last_peek();
        std::unique_ptr<Token> from{m_lexer.next_shell_token()};
        String digits{};
        if (from->kind() == Token::Kind::Word) {
          digits = static_cast<tokens::WordToken *>(from.get())
                       ->word()
                       .to_literal_string();
        }
        i32 from_fd = -1;
        if (!digits.empty()) {
          ErrorOr<i64> parsed = utils::parse_decimal_integer(digits);
          if (parsed.is_error()) throw parsed.error();
          from_fd = static_cast<i32>(parsed.value());
        }
        if (from_fd < 0) {
          throw ErrorWithLocation{from->source_location(),
                                  "Expected a descriptor after '&'"};
        }
        redirection.kind = expressions::Redirection::Kind::DuplicateOutput;
        redirection.dup_fd = from_fd;
        redirections.push(redirection);
        return;
      }
    }

    std::unique_ptr<Token> target{m_lexer.next_shell_token()};
    if (target->kind() != Token::Kind::Word) {
      throw ErrorWithLocation{target->source_location(),
                              "Expected a filename after the redirection"};
    }
    if (op_kind == Token::Kind::Greater)
      redirection.kind = expressions::Redirection::Kind::TruncateOutput;
    else if (op_kind == Token::Kind::DoubleGreater)
      redirection.kind = expressions::Redirection::Kind::AppendOutput;
    else
      redirection.kind = expressions::Redirection::Kind::ReadInput;
    redirection.target = target.release();
    redirections.push(redirection);
  };

  for (;;) {
    std::unique_ptr<Token> token{m_lexer.peek_shell_token()};

    /* A reserved word or a group opener in command position introduces a
       compound command. A list terminator means there is no command here. */
    if (args_accumulator.empty() && local_vars.size() == 0) {
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
    case Token::Kind::Function: {
      /* A run of digits touching a redirection operator is a descriptor prefix,
         such as the 2 in 2>file or 2>&1, not an argument. */
      if (token->kind() == Token::Kind::Word) {
        String literal = static_cast<tokens::WordToken *>(token.get())
                             ->word()
                             .to_literal_string();
        bool is_all_digits = !literal.empty();
        for (usize i = 0; i < literal.size(); i++) {
          if (literal[i] < '0' || literal[i] > '9') {
            is_all_digits = false;
            break;
          }
        }
        if (is_all_digits) {
          SourceLocation word_location = token->source_location();
          m_lexer.advance_past_last_peek();
          std::unique_ptr<Token> next{m_lexer.peek_shell_token()};
          Token::Kind nk = next->kind();
          if ((nk == Token::Kind::Greater || nk == Token::Kind::DoubleGreater ||
               nk == Token::Kind::Less) &&
              next->source_location().position() ==
                  word_location.position() + word_location.length())
          {
            SourceLocation op_location = next->source_location();
            m_lexer.advance_past_last_peek();
            ErrorOr<i64> parsed = utils::parse_decimal_integer(literal);
            if (parsed.is_error()) throw parsed.error();
            add_redirection(static_cast<i32>(parsed.value()), nk, op_location);
            break;
          }
          if (!source_location) source_location = word_location;
          args_accumulator.push(std::move(token));
          break;
        }
      }
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = token->source_location();
      args_accumulator.push(std::move(token));
    } break;

    case Token::Kind::LeftParen:
      /* A single word followed by () is a function definition. */
      if (args_accumulator.size() == 1 && local_vars.size() == 0 &&
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
        args_accumulator.push(std::move(token));
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
        const String &assignment_key = a->key();
        local_vars.set(assignment_key, Word{a->value_word()});
      }
    } break;

    case Token::Kind::Greater:
    case Token::Kind::DoubleGreater:
    case Token::Kind::Less: {
      Token::Kind op_kind = token->kind();
      SourceLocation op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      add_redirection((op_kind == Token::Kind::Less) ? 0 : 1, op_kind,
                      op_location);
    } break;

    case Token::Kind::DoubleLess: {
      SourceLocation op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = op_location;

      std::unique_ptr<Token> delimiter_token{m_lexer.next_shell_token()};
      if (delimiter_token->kind() != Token::Kind::Word) {
        throw ErrorWithLocation{delimiter_token->source_location(),
                                "Expected a heredoc delimiter"};
      }
      const Word &delimiter_word =
          static_cast<tokens::WordToken *>(delimiter_token.get())->word();

      String delimiter_literal = delimiter_word.to_literal_string();
      std::string delimiter{delimiter_literal.c_str(), delimiter_literal.size()};
      bool strip_tabs = false;
      /* <<- strips leading tabs, the dash touching the operator. */
      if (!delimiter.empty() && delimiter[0] == '-' &&
          delimiter_token->source_location().position() ==
              op_location.position() + op_location.length())
      {
        strip_tabs = true;
        delimiter.erase(0, 1);
      }

      /* A quoted delimiter, such as <<'EOF', keeps the body literal. */
      bool should_expand = true;
      for (const WordSegment &segment : delimiter_word.segments) {
        if (segment.kind != WordSegment::Kind::UnquotedText) {
          should_expand = false;
          break;
        }
      }

      expressions::Redirection redirection{};
      redirection.fd = 0;
      redirection.kind = expressions::Redirection::Kind::Heredoc;
      redirection.target = nullptr;
      redirection.dup_fd = -1;
      redirection.heredoc_body =
          m_lexer.register_heredoc(delimiter, strip_tabs);
      redirection.heredoc_expand = should_expand;
      redirections.push(redirection);
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

  ArrayList<std::pair<const Expression *, const Expression *>> branches{};
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
    branches.push(std::pair<const Expression *, const Expression *>{
        condition.release(), body.release()});

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
  String variable_name_string = name_token->raw_string();
  std::string variable_name{variable_name_string.c_str(),
                            variable_name_string.size()};

  ArrayList<const Token *> words{};
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
      words.push(word.release());
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
    String detail = "expected 'do'";
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

  ArrayList<CaseItem> items{};
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

    ArrayList<const Token *> patterns{heap_allocator()};
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
      patterns.push(pattern.release());

      std::unique_ptr<Token> separator{m_lexer.next_shell_token()};
      if (separator->kind() == Token::Kind::Pipe) continue;
      if (separator->kind() == Token::Kind::RightParen) break;
      throw ErrorWithLocation{separator->source_location(),
                              "Expected '|' or ')' in a case pattern"};
    }

    std::unique_ptr<Expression> body{
        parse_command_list({Token::Kind::DoubleSemicolon, Token::Kind::Esac})};
    items.push(CaseItem{std::move(patterns), body.release()});

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
  String name_string = name_token->raw_string();
  std::string name{name_string.c_str(), name_string.size()};

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

  /* The body is parsed into the persistent function arena, so it outlives the
     command that defined it once the per-command arena resets. */
  BumpArena &per_command_arena = m_lexer.arena();
  if (g_function_arena != nullptr) m_lexer.set_arena(*g_function_arena);
  std::unique_ptr<Command> body{parse_simple_command()};
  m_lexer.set_arena(per_command_arena);

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
  case Token::Kind::Number: {
    ErrorOr<i64> parsed = utils::parse_decimal_integer(t->raw_string());
    if (parsed.is_error()) throw parsed.error();
    lhs =
        std::unique_ptr<ConstantNumber>{m_lexer.arena().create<ConstantNumber>(
            t->source_location(), parsed.value())};
  } break;

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
      String ast = after->to_ast_string();
      throw ErrorWithLocation{after->source_location(),
                              "Expected 'Then' after the condition, found '" +
                                  ast.view() + "'"};
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
      String raw = t->raw_string();
      throw ErrorWithLocation{t->source_location(),
                              "Expected a value or an expression, found '" +
                                  raw.view() + "'"};
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
        String raw = maybe_op->raw_string();
        throw ErrorWithLocation{maybe_op->source_location(),
                                "Unexpected '" + raw.view() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    case Token::Kind::Then: {
      if (m_if_condition_depth == 0) {
        String raw = maybe_op->raw_string();
        throw ErrorWithLocation{maybe_op->source_location(),
                                "Unexpected '" + raw.view() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    default: break;
    }

    if (!(maybe_op->flags() & Token::Flag::BinaryOperator)) {
      String raw = maybe_op->raw_string();
      throw ErrorWithLocation{maybe_op->source_location(),
                              "Expected a binary operator, found '" +
                                  raw.view() + "'"};
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
