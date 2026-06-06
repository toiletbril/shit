#include "Parser.hpp"

#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <cctype>
#include <memory>

namespace shit {

using namespace tokens;
using namespace expressions;

hot pure static fn get_sequence_kind(Token::Kind tk) wontthrow
    -> CompoundListCondition::Kind
{
  switch (tk) {
  case Token::Kind::Newline:
  case Token::Kind::EndOfFile:
  case Token::Kind::Ampersand:
  case Token::Kind::Semicolon:
  case Token::Kind::DoubleSemicolon: return CompoundListCondition::Kind::None;
  case Token::Kind::DoubleAmpersand: return CompoundListCondition::Kind::And;
  case Token::Kind::DoublePipe: return CompoundListCondition::Kind::Or; break;

  default: unreachable("Invalid shell sequence token: %d", ENUM(tk));
  }
}

Parser::Parser(Lexer &&lexer) : m_lexer(steal(lexer)) {}

Parser::~Parser() = default;

pure fn Parser::debug_words() const wontthrow -> const ArrayList<Word> &
{
  return m_lexer.debug_words();
}

hot pure static fn kind_in(Token::Kind kind,
                           std::initializer_list<Token::Kind> set) wontthrow
    -> bool
{
  for (Token::Kind k : set) {
    if (k == kind) return true;
  }
  return false;
}

/* The byte location of the keyword as a whole word somewhere in the source. A
   missing terminator usually means the keyword sits earlier in the input but
   was read as an argument, so the caret can point straight at it. */
cold pure static fn find_standalone_keyword(StringView source,
                                            StringView keyword) wontthrow
    -> Maybe<SourceLocation>
{
  auto is_boundary = [](char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0 || c == ';' ||
           c == '&' || c == '|';
  };

  if (keyword.length == 0 || keyword.length > source.length) return shit::None;

  for (usize pos = 0; pos + keyword.length <= source.length; pos++) {
    if (source.substring_of_length(pos, keyword.length) != keyword) continue;
    const let end = pos + keyword.length;
    const let left_ok = pos == 0 || is_boundary(source[pos - 1]);
    const let right_ok = end == source.length || is_boundary(source[end]);
    if (left_ok && right_ok) return SourceLocation{pos, keyword.length};
  }
  return shit::None;
}

/* Report a missing terminator. When the keyword is found earlier in the source,
   point the note at it and explain it was read as an argument, otherwise point
   at the token where the terminator was expected. */
cold [[noreturn]] static fn
throw_unterminated(SourceLocation opener, StringView what, StringView source,
                   StringView keyword, SourceLocation fallback) throws -> void
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

cold pure static fn is_empty_list(const Expression *expression) wontthrow
    -> bool
{
  ASSERT(expression != nullptr);
  return expression->is_dummy();
}

/* The reserved word ! is a single unquoted exclamation mark standing alone in
   command position. It is distinct from a != comparison or a quoted literal. */
hot pure static fn is_negation_token(const Token *token) wontthrow -> bool
{
  ASSERT(token != nullptr);
  if (token->kind() != Token::Kind::Word) return false;
  const Word &word = static_cast<const tokens::WordToken *>(token)->word();
  return word.segments.count() == 1 &&
         word.segments[0].kind == WordSegment::Kind::UnquotedText &&
         word.segments[0].text == "!";
}

/* A friendly message for a token that cannot start a command. A stray control
   keyword almost always means its opener is missing. */
cold static fn unexpected_command_token_message(const Token *token) throws
    -> String
{
  ASSERT(token != nullptr);
  switch (token->kind()) {
  case Token::Kind::Then:
  case Token::Kind::Else:
  case Token::Kind::Elif:
  case Token::Kind::Fi: {
    const let ast = token->to_ast_string();
    return "'" + ast.view() + "' has no matching 'if'";
  }
  case Token::Kind::Do:
  case Token::Kind::Done: {
    const let ast = token->to_ast_string();
    return "'" + ast.view() + "' has no matching 'while', 'until', or 'for'";
  }
  case Token::Kind::Esac: return "'esac' has no matching 'case'";
  case Token::Kind::DoubleSemicolon:
    return "';;' is only valid between the arms of a 'case'";
  case Token::Kind::RightParen: return "')' has no matching '('";
  case Token::Kind::RightBracket: return "'}' has no matching '{'";
  case Token::Kind::Pipe: return "'|' has no command before it to pipe from";
  default: {
    const let ast = token->to_ast_string();
    return "expected a command, found '" + ast.view() + "'";
  }
  }
}

/* TODO */
static fn require_simple_in_pipeline(Command *command) throws -> SimpleCommand *
{
  ASSERT(command != nullptr);
  if (!command->is_simple_command()) {
    throw ErrorWithLocation{
        command->source_location(),
        "A compound command in a pipeline is not supported"};
  }
  return static_cast<SimpleCommand *>(command);
}

hot pure static fn is_compound_terminator(Token::Kind kind) wontthrow -> bool
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

flatten fn Parser::construct_ast() throws -> Expression *
{
  /* The top-level list ends only at the end of input. */
  return parse_command_list({});
}

hot fn Parser::parse_command_list(
    std::initializer_list<Token::Kind> terminators) throws -> Expression *
{
  Command *lhs = nullptr;

  CompoundList *compound_list = m_lexer.arena().create<CompoundList>();
  CompoundListCondition::Kind next_cond = CompoundListCondition::Kind::None;

  bool should_parse_command = true;
  bool should_negate_pending = false;

  for (;;) {
    if (should_parse_command) {
      /* A leading ! negates the pipeline that follows. */
      Token *maybe_negation = m_lexer.peek_shell_token();
      ASSERT(maybe_negation != nullptr);
      if (is_negation_token(maybe_negation)) {
        m_lexer.advance_past_last_peek();
        should_negate_pending = true;
      }
      lhs = parse_simple_command();
    } else {
      should_parse_command = true;
    }

    Token *token = m_lexer.peek_shell_token();
    ASSERT(token != nullptr);

    /* A terminator keyword ends this list. Append the pending command and leave
       the terminator for the caller to consume. */
    if (kind_in(token->kind(), terminators)) {
      if (lhs != nullptr) {
        if (should_negate_pending) {
          lhs->set_negated();
          should_negate_pending = false;
        }
        compound_list->append_node(
            m_lexer.arena().create<CompoundListCondition>(
                token->source_location(), next_cond, lhs));
      } else if (next_cond != CompoundListCondition::Kind::None) {
        throw shit::ErrorWithLocation{token->source_location(),
                                      "Expected a command after an operator"};
      }
      if (compound_list->is_empty()) {
        return m_lexer.arena().create<DummyExpression>(
            token->source_location());
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
        const let ast = token->to_ast_string();
        String msg = "Expected a command ";
        msg += compound_list->is_empty() ? "before" : "after";
        msg += " operator, found '";
        msg += ast.view();
        msg += "'";
        throw shit::ErrorWithLocation{token->source_location(), msg};
      }
      [[fallthrough]];
    case Token::Kind::Newline:
    case Token::Kind::EndOfFile:
    case Token::Kind::DoubleSemicolon:
    case Token::Kind::Semicolon: {
      m_lexer.advance_past_last_peek();

      if (lhs != nullptr) {
        if (should_negate_pending) {
          lhs->set_negated();
          should_negate_pending = false;
        }
        compound_list->append_node(
            m_lexer.arena().create<CompoundListCondition>(
                token->source_location(), next_cond, lhs));
        next_cond = get_sequence_kind(token->kind());
      }

      if (token->kind() == Token::Kind::EndOfFile) {
        if (next_cond != CompoundListCondition::Kind::None) {
          throw shit::ErrorWithLocation{token->source_location(),
                                        "Expected a command after an operator"};
        }

        /* Empty input yields a dummy, since lhs is null when no command was
           parsed before the terminator. */
        if (compound_list->is_empty()) {
          return m_lexer.arena().create<DummyExpression>(
              token->source_location());
        }

        return compound_list;
      }
    } break;

    case Token::Kind::Pipe: {
      if (lhs == nullptr) {
        throw shit::ErrorWithLocation{token->source_location(),
                                      "Expected a command before the pipe"};
      }

      m_lexer.advance_past_last_peek();

      Pipeline *pipeline =
          m_lexer.arena().create<Pipeline>(token->source_location());
      pipeline->append_command(require_simple_in_pipeline(lhs));

      Token *last_pipe_token = token;

      /* Collect a pipe group. */
      for (;;) {
        Command *rhs = parse_simple_command();

        if (rhs != nullptr) {
          pipeline->append_command(require_simple_in_pipeline(rhs));
          last_pipe_token = m_lexer.peek_shell_token();
          ASSERT(last_pipe_token != nullptr);
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

      lhs = pipeline;

      should_parse_command = false;
    } break;

    default:
      throw ErrorWithLocation{token->source_location(),
                              unexpected_command_token_message(token)};
    }
  }

  unreachable();
}

/* return: a command, a compound command, or nullptr when a list terminator is
   next. A reserved word or a group opener in command position introduces a
   compound command. */
hot fn Parser::parse_simple_command() throws -> Command *
{
  Maybe<SourceLocation> source_location;
  ArrayList<Token *> args_accumulator{};
  HashMap<Word> local_vars{heap_allocator()};
  ArrayList<expressions::Redirection> redirections{};

  auto build_command = [&]() -> Command * {
    if (!source_location) return nullptr;

    ArrayList<const Token *> args{};
    args.reserve(args_accumulator.count());
    for (Token *t : args_accumulator)
      args.push(t);

    SimpleCommand *c =
        m_lexer.arena().create<SimpleCommand>(*source_location, steal(args));
    if (local_vars.count() != 0) c->set_local_vars(steal(local_vars));
    if (!redirections.is_empty()) c->set_redirections(steal(redirections));
    return c;
  };

  /* Build one redir for descriptor fd. The operator is already consumed,
     and op_location is its position. A & touching the operator means a
     descriptor duplication, n>&m, otherwise a filename word follows. */
  auto add_redirection = [&](i32 fd, Token::Kind op_kind,
                             SourceLocation op_location) {
    if (!source_location) source_location = op_location;

    expressions::Redirection redir{};
    redir.fd = fd;
    redir.target = nullptr;
    redir.dup_fd = -1;

    if (op_kind != Token::Kind::Less) {
      Token *after = m_lexer.peek_shell_token();
      ASSERT(after != nullptr);
      if (after->kind() == Token::Kind::Ampersand &&
          after->source_location().position ==
              op_location.position + op_location.length)
      {
        m_lexer.advance_past_last_peek();
        Token *from = m_lexer.next_shell_token();
        String digits{};
        if (from->kind() == Token::Kind::Word) {
          digits = static_cast<tokens::WordToken *>(from)
                       ->word()
                       .to_literal_string();
        }
        i32 from_fd = -1;
        if (!digits.is_empty()) {
          const let parsed = utils::parse_decimal_integer(digits);
          if (parsed.is_error()) throw parsed.error();
          from_fd = static_cast<i32>(parsed.value());
        }
        if (from_fd < 0) {
          throw ErrorWithLocation{from->source_location(),
                                  "Expected a descriptor after '&'"};
        }
        redir.kind = expressions::Redirection::Kind::DuplicateOutput;
        redir.dup_fd = from_fd;
        redirections.push(redir);
        return;
      }
    }

    Token *target = m_lexer.next_shell_token();
    ASSERT(target != nullptr);
    if (target->kind() != Token::Kind::Word) {
      throw ErrorWithLocation{target->source_location(),
                              "Expected a filename after the redir"};
    }
    if (op_kind == Token::Kind::Greater)
      redir.kind = expressions::Redirection::Kind::TruncateOutput;
    else if (op_kind == Token::Kind::DoubleGreater)
      redir.kind = expressions::Redirection::Kind::AppendOutput;
    else
      redir.kind = expressions::Redirection::Kind::ReadInput;
    redir.target = target;
    redirections.push(redir);
  };

  for (;;) {
    Token *token = m_lexer.peek_shell_token();
    ASSERT(token != nullptr);

    /* A reserved word or a group opener in command position introduces a
       compound command. A list terminator means there is no command here. */
    if (args_accumulator.is_empty() && local_vars.count() == 0) {
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
      /* A run of digits touching a redir operator is a descriptor prefix,
         such as the 2 in 2>file or 2>&1, not an argument. */
      if (token->kind() == Token::Kind::Word) {
        const let literal =
            static_cast<tokens::WordToken *>(token)->word().to_literal_string();
        bool is_all_digits = !literal.is_empty();
        for (usize i = 0; i < literal.count(); i++) {
          if (literal[i] < '0' || literal[i] > '9') {
            is_all_digits = false;
            break;
          }
        }
        if (is_all_digits) {
          const let word_location = token->source_location();
          m_lexer.advance_past_last_peek();
          Token *next = m_lexer.peek_shell_token();
          ASSERT(next != nullptr);
          const let nk = next->kind();
          if ((nk == Token::Kind::Greater || nk == Token::Kind::DoubleGreater ||
               nk == Token::Kind::Less) &&
              next->source_location().position ==
                  word_location.position + word_location.length)
          {
            const let op_location = next->source_location();
            m_lexer.advance_past_last_peek();
            const let parsed = utils::parse_decimal_integer(literal);
            if (parsed.is_error()) throw parsed.error();
            add_redirection(static_cast<i32>(parsed.value()), nk, op_location);
            break;
          }
          if (!source_location) source_location = word_location;
          args_accumulator.push(token);
          break;
        }
      }
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = token->source_location();
      args_accumulator.push(token);
    } break;

    case Token::Kind::LeftParen:
      /* A single word followed by () is a function definition. */
      if (args_accumulator.count() == 1 && local_vars.count() == 0 &&
          args_accumulator[0]->kind() == Token::Kind::Word)
      {
        return parse_function_definition(args_accumulator[0]);
      }
      return build_command();

    case Token::Kind::Assignment: {
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = token->source_location();

      /* Once a command word is present, an assignment-looking token is just an
       * ordinary argument. */
      if (!args_accumulator.is_empty()) {
        args_accumulator.push(token);
        break;
      }

      Assignment *a = static_cast<Assignment *>(token);

      /* Peek the next token. A compound list condition, a compound terminator,
       * or the end of input means the assignment stands alone. */
      Token *next = m_lexer.peek_shell_token();
      ASSERT(next != nullptr);
      if (next->flags() & Token::Flag::CompoundList ||
          next->kind() == Token::Kind::EndOfFile ||
          is_compound_terminator(next->kind()))
      {
        return m_lexer.arena().create<AssignCommand>(*source_location, a);
      } else {
        /* Single-command variable. */
        const String &assignment_key = a->key();
        local_vars.set(assignment_key, Word{a->value_word()});
      }
    } break;

    case Token::Kind::Greater:
    case Token::Kind::DoubleGreater:
    case Token::Kind::Less: {
      const let op_kind = token->kind();
      const let op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      add_redirection((op_kind == Token::Kind::Less) ? 0 : 1, op_kind,
                      op_location);
    } break;

    case Token::Kind::DoubleLess: {
      const let op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = op_location;

      Token *delimiter_token = m_lexer.next_shell_token();
      ASSERT(delimiter_token != nullptr);
      if (delimiter_token->kind() != Token::Kind::Word) {
        throw ErrorWithLocation{delimiter_token->source_location(),
                                "Expected a heredoc delimiter"};
      }
      const Word &delimiter_word =
          static_cast<tokens::WordToken *>(delimiter_token)->word();

      const let delimiter_literal = delimiter_word.to_literal_string();
      StringView delimiter = delimiter_literal.view();
      bool strip_tabs = false;
      /* <<- strips leading tabs, the dash touching the operator. */
      if (!delimiter.is_empty() && delimiter[0] == '-' &&
          delimiter_token->source_location().position ==
              op_location.position + op_location.length)
      {
        strip_tabs = true;
        delimiter = delimiter.substring(1);
      }

      /* A quoted delimiter, such as <<'EOF', keeps the body literal. */
      bool should_expand = true;
      for (const WordSegment &segment : delimiter_word.segments) {
        if (segment.kind != WordSegment::Kind::UnquotedText) {
          should_expand = false;
          break;
        }
      }

      expressions::Redirection redir{};
      redir.fd = 0;
      redir.kind = expressions::Redirection::Kind::Heredoc;
      redir.target = nullptr;
      redir.dup_fd = -1;
      redir.heredoc_body = m_lexer.register_heredoc(delimiter, strip_tabs);
      redir.heredoc_expand = should_expand;
      redirections.push(redir);
    } break;

    /* A separator, an operator, or a list terminator ends the command. */
    default: return build_command();
    }
  }

  unreachable();
}

hot fn Parser::parse_if() throws -> Command *
{
  Token *if_token = m_lexer.next_shell_token();
  ASSERT(if_token != nullptr);
  ASSERT(if_token->kind() == Token::Kind::If);
  const let location = if_token->source_location();

  ArrayList<IfBranch> branches{};
  const Expression *otherwise = nullptr;
  /* Free the released branch nodes if a later branch fails to parse. */
  defer
  {
    for (auto &[condition, body] : branches) {
      delete condition;
      delete body;
    }
    delete otherwise;
  };

  for (;;) {
    Expression *condition = parse_command_list({Token::Kind::Then});
    Token *then_token = m_lexer.next_shell_token();
    ASSERT(then_token != nullptr);
    if (then_token->kind() != Token::Kind::Then) {
      const let detail = is_empty_list(condition)
                             ? "expected a command for the condition"
                             : "expected 'then' after the condition";
      throw ErrorWithLocationAndDetails{location, "Unterminated if",
                                        then_token->source_location(), detail};
    }

    Expression *body = parse_command_list(
        {Token::Kind::Elif, Token::Kind::Else, Token::Kind::Fi});
    branches.push(IfBranch{condition, body});

    Token *after = m_lexer.next_shell_token();
    ASSERT(after != nullptr);
    if (after->kind() == Token::Kind::Elif) {
      continue;
    } else if (after->kind() == Token::Kind::Else) {
      otherwise = parse_command_list({Token::Kind::Fi});
      Token *fi_token = m_lexer.next_shell_token();
      ASSERT(fi_token != nullptr);
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

  IfClause *node =
      m_lexer.arena().create<IfClause>(location, steal(branches), otherwise);
  /* Ownership of the else body moved into the node, so the cleanup guard must
     not also free it. The branches vector was moved from and is now empty. */
  otherwise = nullptr;
  return node;
}

hot fn Parser::parse_while_or_until(bool is_until) throws -> Command *
{
  Token *keyword = m_lexer.next_shell_token();
  ASSERT(keyword != nullptr);
  const let location = keyword->source_location();

  Expression *condition = parse_command_list({Token::Kind::Do});
  Token *do_token = m_lexer.next_shell_token();
  ASSERT(do_token != nullptr);
  if (do_token->kind() != Token::Kind::Do) {
    const let detail = is_empty_list(condition)
                           ? "expected a command for the loop condition"
                           : "expected 'do'";
    throw ErrorWithLocationAndDetails{location, "Unterminated loop",
                                      do_token->source_location(), detail};
  }

  Expression *body = parse_command_list({Token::Kind::Done});
  Token *done_token = m_lexer.next_shell_token();
  ASSERT(done_token != nullptr);
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated loop", m_lexer.source(), "done",
                       done_token->source_location());
  }

  return m_lexer.arena().create<WhileLoop>(location, condition, body, is_until);
}

hot fn Parser::parse_for() throws -> Command *
{
  Token *keyword = m_lexer.next_shell_token();
  ASSERT(keyword != nullptr);
  const let location = keyword->source_location();

  Token *name_token = m_lexer.next_shell_token();
  ASSERT(name_token != nullptr);
  if (name_token->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{name_token->source_location(),
                            "Expected a variable name after 'for'"};
  }
  const let variable_name = name_token->raw_string();

  ArrayList<const Token *> words{};
  /* Free the released word tokens if the loop fails to parse. */
  defer
  {
    for (const Token *word : words)
      delete word;
  };
  bool has_in_clause = false;

  /* An optional 'in WORDS' clause. The word 'in' is not a keyword token. */
  Token *peeked = m_lexer.peek_shell_token();
  ASSERT(peeked != nullptr);
  if (peeked->kind() == Token::Kind::Word && peeked->raw_string() == "in") {
    m_lexer.advance_past_last_peek();
    has_in_clause = true;
    for (;;) {
      Token *word = m_lexer.peek_shell_token();
      ASSERT(word != nullptr);
      if (word->kind() != Token::Kind::Word) break;
      m_lexer.advance_past_last_peek();
      words.push(word);
    }
  }

  /* Skip the separators between the header and 'do'. */
  for (;;) {
    Token *t = m_lexer.peek_shell_token();
    ASSERT(t != nullptr);
    if (t->kind() == Token::Kind::Semicolon ||
        t->kind() == Token::Kind::Newline)
    {
      m_lexer.advance_past_last_peek();
      continue;
    }
    break;
  }

  Token *do_token = m_lexer.next_shell_token();
  ASSERT(do_token != nullptr);
  if (do_token->kind() != Token::Kind::Do) {
    String detail = "expected 'do'";
    if (!has_in_clause) {
      detail = "expected 'do', or 'in WORDS' before it; without 'in' the loop "
               "walks the positional parameters";
    }
    throw ErrorWithLocationAndDetails{location, "Unterminated for loop",
                                      do_token->source_location(), detail};
  }

  Expression *body = parse_command_list({Token::Kind::Done});
  Token *done_token = m_lexer.next_shell_token();
  ASSERT(done_token != nullptr);
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated for loop", m_lexer.source(),
                       "done", done_token->source_location());
  }

  return m_lexer.arena().create<ForLoop>(location, variable_name.view(),
                                         steal(words), has_in_clause, body);
}

hot fn Parser::parse_case() throws -> Command *
{
  Token *keyword = m_lexer.next_shell_token();
  ASSERT(keyword != nullptr);
  const let location = keyword->source_location();

  Token *word = m_lexer.next_shell_token();
  ASSERT(word != nullptr);
  if (word->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{word->source_location(),
                            "Expected a word to match on after 'case'"};
  }

  Token *in_token = m_lexer.next_shell_token();
  ASSERT(in_token != nullptr);
  if (!(in_token->kind() == Token::Kind::Word &&
        in_token->raw_string() == "in"))
  {
    throw ErrorWithLocation{in_token->source_location(),
                            "Expected 'in' after the case word"};
  }

  ArrayList<case_item> items{};
  /* A parse error before the clause is built abandons these arena nodes, so
     free their tokens and bodies to keep the leak checker happy. */
  defer
  {
    for (case_item &item : items) {
      for (const Token *pattern : item.patterns)
        delete pattern;
      delete item.body;
    }
  };

  for (;;) {
    Token *t = m_lexer.peek_shell_token();
    ASSERT(t != nullptr);

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
    defer
    {
      for (const Token *pattern : patterns)
        delete pattern;
    };

    for (;;) {
      Token *pattern = m_lexer.next_shell_token();
      ASSERT(pattern != nullptr);

      if (pattern->kind() != Token::Kind::Word) {
        throw ErrorWithLocationAndDetails{
            location, "Unterminated case", pattern->source_location(),
            "expected a pattern to start an arm, or 'esac' to end the case"};
      }

      patterns.push(pattern);

      Token *separator = m_lexer.next_shell_token();
      ASSERT(separator != nullptr);

      if (separator->kind() == Token::Kind::Pipe) continue;
      if (separator->kind() == Token::Kind::RightParen) break;

      throw ErrorWithLocation{separator->source_location(),
                              "Expected '|' or ')' in a case pattern"};
    }

    Expression *body =
        parse_command_list({Token::Kind::DoubleSemicolon, Token::Kind::Esac});
    items.push(case_item{steal(patterns), body});

    Token *after = m_lexer.peek_shell_token();
    ASSERT(after != nullptr);
    if (after->kind() == Token::Kind::DoubleSemicolon) {
      m_lexer.advance_past_last_peek();
    } else if (after->kind() == Token::Kind::Esac) {
      m_lexer.advance_past_last_peek();
      break;
    }
  }

  return m_lexer.arena().create<CaseClause>(location, word, steal(items));
}

hot fn Parser::parse_brace_group() throws -> Command *
{
  Token *open = m_lexer.next_shell_token();
  ASSERT(open != nullptr);
  ASSERT(open->kind() == Token::Kind::LeftBracket);

  Expression *body = parse_command_list({Token::Kind::RightBracket});

  Token *close = m_lexer.next_shell_token();
  ASSERT(close != nullptr);
  if (close->kind() != Token::Kind::RightBracket) {
    throw ErrorWithLocationAndDetails{open->source_location(),
                                      "Unterminated brace group",
                                      close->source_location(), "expected '}'"};
  }

  return m_lexer.arena().create<BraceGroup>(open->source_location(), body);
}

hot fn Parser::parse_subshell() throws -> Command *
{
  Token *open = m_lexer.next_shell_token();
  ASSERT(open != nullptr);
  ASSERT(open->kind() == Token::Kind::LeftParen);

  Expression *body = parse_command_list({Token::Kind::RightParen});

  Token *close = m_lexer.next_shell_token();
  ASSERT(close != nullptr);
  if (close->kind() != Token::Kind::RightParen) {
    throw ErrorWithLocationAndDetails{open->source_location(),
                                      "Unterminated subshell",
                                      close->source_location(), "expected ')'"};
  }

  return m_lexer.arena().create<Subshell>(open->source_location(), body);
}

hot fn Parser::parse_function_definition(Token *name_token) throws -> Command *
{
  ASSERT(name_token != nullptr);
  const let location = name_token->source_location();
  const let name = name_token->raw_string();

  /* The opening parenthesis was peeked by the caller. Consume the empty pair.
   */
  m_lexer.advance_past_last_peek();
  Token *close = m_lexer.next_shell_token();
  ASSERT(close != nullptr);
  if (close->kind() != Token::Kind::RightParen) {
    throw ErrorWithLocation{close->source_location(),
                            "Expected ')' in a function definition"};
  }

  /* Skip newlines before the body. */
  for (;;) {
    Token *t = m_lexer.peek_shell_token();
    ASSERT(t != nullptr);
    if (t->kind() != Token::Kind::Newline) break;
    m_lexer.advance_past_last_peek();
  }

  /* The body is parsed into the persistent function arena, so it outlives the
     command that defined it once the per-command arena resets. */
  BumpArena &per_command_arena = m_lexer.arena();
  if (FUNCTION_ARENA != nullptr) m_lexer.set_arena(*FUNCTION_ARENA);
  Command *body = parse_simple_command();
  m_lexer.set_arena(per_command_arena);

  if (body == nullptr) {
    throw ErrorWithLocation{location,
                            "Expected a compound command as the function body"};
  }

  return m_lexer.arena().create<FunctionDefinition>(location, name.view(),
                                                    body);
}

/* A standard pratt-parser for expressions. */
hot fn Parser::parse_expression(u8 min_precedence) throws -> Expression *
{
  m_recursion_depth++;
  defer { m_recursion_depth--; };

  Token *t = m_lexer.next_expression_token();
  ASSERT(t != nullptr);

  if (m_recursion_depth > MAX_RECURSION_DEPTH) {
    throw ErrorWithLocation{
        t->source_location(),
        "Expression nesting level exceeded maximum of " +
            utils::integer_to_string(static_cast<i64>(MAX_RECURSION_DEPTH))};
  }

  Expression *lhs = nullptr;

  /* Handle leaf type. We expect either a value, or an unary operator. */
  switch (t->kind()) {
  case Token::Kind::Number: {
    const let parsed = utils::parse_decimal_integer(t->raw_string());
    if (parsed.is_error()) throw parsed.error();
    lhs = m_lexer.arena().create<ConstantNumber>(t->source_location(),
                                                 parsed.value());
  } break;

  case Token::Kind::If: {
    /* if expr[;] then [...] [else [then] ...] fi */
    m_if_condition_depth++;

    Expression *condition = parse_expression();

    Token *after = m_lexer.next_expression_token();
    ASSERT(after != nullptr);
    if (after->kind() == Token::Kind::Semicolon) {
      after = m_lexer.next_expression_token();
    }
    if (after->kind() != Token::Kind::Then) {
      const let ast = after->to_ast_string();
      throw ErrorWithLocation{after->source_location(),
                              "Expected 'Then' after the condition, found '" +
                                  ast.view() + "'"};
    }

    Expression *then = parse_expression();

    Expression *otherwise = nullptr;
    after = m_lexer.next_expression_token();

    /* [else [then]] */
    if (after->kind() == Token::Kind::Else) {
      after = m_lexer.peek_expression_token();
      if (after->kind() == Token::Kind::Then) m_lexer.advance_past_last_peek();

      otherwise = parse_expression();

      after = m_lexer.next_expression_token();
    }

    if (after->kind() != Token::Kind::Fi) {
      throw ErrorWithLocationAndDetails{
          t->source_location(), "Unterminated If condition",
          after->source_location(), "expected 'Fi' here"};
    }

    m_if_condition_depth--;

    lhs = m_lexer.arena().create<IfStatement>(t->source_location(), condition,
                                              then, otherwise);
  } break;

  case Token::Kind::LeftParen: {
    if (m_recursion_depth + m_parentheses_depth > MAX_RECURSION_DEPTH) {
      throw ErrorWithLocation{
          t->source_location(),
          "Bracket nesting level exceeded maximum of " +
              utils::integer_to_string(static_cast<i64>(MAX_RECURSION_DEPTH))};
    }

    m_parentheses_depth++;
    lhs = parse_expression();
    m_parentheses_depth--;

    Token *rp = m_lexer.next_expression_token();
    ASSERT(rp != nullptr);
    if (rp->kind() != Token::Kind::RightParen) {
      throw ErrorWithLocationAndDetails{
          t->source_location(), "Unterminated parenthesis",
          rp->source_location(), "expected a closing parenthesis here"};
    }
  } break;

  /* Now it's either a unary operator or something odd */
  default:
    if (t->flags() & Token::Flag::UnaryOperator) {
      const let op = static_cast<const tokens::Operator *>(t);

      Expression *rhs = parse_expression(op->unary_precedence());

      lhs = op->construct_unary_expression(rhs);
    } else {
      const let raw = t->raw_string();
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
    Token *maybe_op = m_lexer.peek_expression_token();
    ASSERT(maybe_op != nullptr);

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
        const let raw = maybe_op->raw_string();
        throw ErrorWithLocation{maybe_op->source_location(),
                                "Unexpected '" + raw.view() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    case Token::Kind::Then: {
      if (m_if_condition_depth == 0) {
        const let raw = maybe_op->raw_string();
        throw ErrorWithLocation{maybe_op->source_location(),
                                "Unexpected '" + raw.view() +
                                    "' without matching If condition"};
      }
      return lhs;
    }

    default: break;
    }

    if (!(maybe_op->flags() & Token::Flag::BinaryOperator)) {
      const let raw = maybe_op->raw_string();
      throw ErrorWithLocation{maybe_op->source_location(),
                              "Expected a binary operator, found '" +
                                  raw.view() + "'"};
    }

    const let op = static_cast<const tokens::Operator *>(maybe_op);
    if (op->left_precedence() < min_precedence) break;
    m_lexer.advance_past_last_peek();

    Expression *rhs = parse_expression(
        op->left_precedence() + (op->binary_left_associative() ? 1 : -1));
    lhs = op->construct_binary_expression(lhs, rhs);
  }

  return lhs;
}

} /* namespace shit */
