#include "Parser.hpp"

#include "Arena.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <cctype>

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

/* A brace is a reserved word only when a token is exactly '{' or '}' as a
   single unquoted segment. The lexer keeps a brace inside a larger word
   literal, so 'a{b}c' is one word and never matches here. A quoted or escaped
   brace carries a non-unquoted segment and is also rejected. */
hot pure static fn is_brace_word(const Token *token, char brace) wontthrow
    -> bool
{
  ASSERT(token != nullptr);
  if (token->kind() != Token::Kind::Word) return false;
  const Word &word = static_cast<const tokens::WordToken *>(token)->word();
  return word.segments.count() == 1 &&
         word.segments[0].kind == WordSegment::Kind::UnquotedText &&
         word.segments[0].text.count() == 1 &&
         word.segments[0].text[0] == brace;
}

/* True when the token is a single unquoted word with exactly the given text,
   the way [[ and ]] arrive from the lexer as ordinary words rather than
   dedicated operator tokens. */
hot pure static fn is_unquoted_word(const Token *token,
                                    StringView text) wontthrow -> bool
{
  if (token == nullptr || token->kind() != Token::Kind::Word) return false;
  const Word &word = static_cast<const tokens::WordToken *>(token)->word();
  if (word.segments.count() != 1 ||
      word.segments[0].kind != WordSegment::Kind::UnquotedText)
    return false;
  const auto &segment_text = word.segments[0].text;
  if (segment_text.count() != text.length) return false;
  for (usize i = 0; i < text.length; i++)
    if (segment_text[i] != text[i]) return false;
  return true;
}

/* Whether the peeked token terminates a command list. The closing brace of a
   brace group is a '}' word rather than a token kind, so RightBracket in the
   terminator set stands for a standalone '}' word here. */
hot pure static fn
is_list_terminator(const Token *token,
                   std::initializer_list<Token::Kind> terminators) wontthrow
    -> bool
{
  ASSERT(token != nullptr);
  if (kind_in(token->kind(), terminators)) return true;
  if (kind_in(Token::Kind::RightBracket, terminators) &&
      is_brace_word(token, '}'))
  {
    return true;
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
        "This '" + keyword +
            "' was read as an argument, put a ';' or a newline before it"};
  }
  throw ErrorWithLocationAndDetails{opener, what, fallback,
                                    "Expected '" + keyword + "'"};
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
  /* A '}' word in command position with no matching open brace. */
  if (is_brace_word(token, '}')) return "'}' has no matching '{'";
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

fn Parser::skip_newlines_after_pipe() throws -> void
{
  while (m_lexer.peek_shell_token()->kind() == Token::Kind::Newline)
    m_lexer.advance_past_last_peek();
}

/* Skip tokens until the next statement boundary, so parsing can resume after a
   syntax error. The boundary is a newline or a ';', which begins a fresh
   command, or the end of input. At least one token is always consumed, so a
   token that itself triggered the error cannot stall the loop forever. */
cold fn Parser::recover_to_next_statement() throws -> void
{
  LOG(verbosity::Debug, "skipping tokens to the next statement boundary");
  bool has_consumed_token = false;
  for (;;) {
    Token *token = m_lexer.peek_shell_token();
    ASSERT(token != nullptr);

    if (token->kind() == Token::Kind::EndOfFile) return;

    const bool is_boundary = token->kind() == Token::Kind::Newline ||
                             token->kind() == Token::Kind::Semicolon;

    /* Consume the boundary itself once at least one token has been skipped, so
       the resumed parse starts on the command after the boundary rather than on
       the boundary token. */
    if (is_boundary && has_consumed_token) {
      m_lexer.advance_past_last_peek();
      return;
    }

    m_lexer.advance_past_last_peek();
    has_consumed_token = true;
  }
}

/* Parse every top-level command, recovering from a syntax error instead of
   aborting at the first. Each top-level parse runs under a try, and a thrown
   located error is pushed into errors and parsing resumes at the next statement
   boundary. A clean file parses in a single iteration whose tree is returned
   for the caller to run. Once any error is recorded the tree never runs, so the
   remaining iterations only keep parsing to collect more errors. */
cold fn Parser::construct_ast(ArrayList<String> &errors) throws -> Expression *
{
  Expression *first_piece = nullptr;
  let last_location = SourceLocation{};

  for (;;) {
    Token *token = m_lexer.peek_shell_token();
    ASSERT(token != nullptr);
    last_location = token->source_location();
    if (token->kind() == Token::Kind::EndOfFile) break;

    try {
      Expression *piece = parse_command_list({});
      ASSERT(piece != nullptr);
      if (first_piece == nullptr) first_piece = piece;
    } catch (const ErrorWithLocationAndDetails &e) {
      /* Render both parts here, since the detail note would be sliced off a
         base-class copy and its hint lost. */
      LOG(verbosity::Debug,
          "recording a detailed parse error and recovering: %s",
          e.message().c_str());
      errors.push(e.to_string(m_lexer.source()));
      errors.push(e.details_to_string(m_lexer.source()));
      recover_to_next_statement();
    } catch (const ErrorWithLocation &e) {
      LOG(verbosity::Debug, "recording a parse error and recovering: %s",
          e.message().c_str());
      errors.push(e.to_string(m_lexer.source()));
      recover_to_next_statement();
    }
  }

  if (first_piece == nullptr)
    return m_lexer.arena().create<DummyExpression>(last_location);

  return first_piece;
}

fn Parser::reject_empty_loop_body(const Expression *body) throws -> void
{
  if (!body->is_dummy()) return;
  Token *terminator = m_lexer.peek_shell_token();
  ASSERT(terminator != nullptr);
  throw shit::ErrorWithLocation{
      terminator->source_location(),
      "Unable to parse the loop because its body between 'do' and 'done' is "
      "empty, a command is required there"};
}

hot fn Parser::parse_command_list(
    std::initializer_list<Token::Kind> terminators) throws -> Expression *
{
  /* Every nested compound command recurses through this list, so the depth
     guard here covers a subshell, a brace group, an if, a while, a for, and a
     case alike. A source nested past the limit throws here instead of
     overflowing the native stack. */
  m_command_depth++;
  defer { m_command_depth--; };
  if (m_command_depth > MAX_COMMAND_DEPTH) {
    Token *token = m_lexer.peek_shell_token();
    ASSERT(token != nullptr);
    throw shit::ErrorWithLocation{
        token->source_location(),
        "Compound command nested deeper than " +
            utils::int_to_text(static_cast<i64>(MAX_COMMAND_DEPTH))};
  }

  Command *lhs = nullptr;

  CompoundList *compound_list = m_lexer.arena().create<CompoundList>();
  CompoundListCondition::Kind next_cond = CompoundListCondition::Kind::None;

  bool should_parse_command = true;
  bool should_negate_pending = false;
  bool should_time_pending = false;
  bool time_posix_format = false;

  for (;;) {
    if (should_parse_command) {
      /* A leading time keyword times the command or pipeline that follows,
         including a compound command, and bash allows it before the ! negation.
         Its -p or --posix option selects the plain POSIX report. */
      Token *maybe_time = m_lexer.peek_shell_token();
      ASSERT(maybe_time != nullptr);
      if (maybe_time->kind() == Token::Kind::Time) {
        m_lexer.advance_past_last_peek();
        should_time_pending = true;
        Token *maybe_posix = m_lexer.peek_shell_token();
        if (is_unquoted_word(maybe_posix, "-p") ||
            is_unquoted_word(maybe_posix, "--posix"))
        {
          m_lexer.advance_past_last_peek();
          time_posix_format = true;
        }
      }
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
    if (is_list_terminator(token, terminators)) {
      if (lhs != nullptr) {
        if (should_negate_pending) {
          lhs->set_negated();
          should_negate_pending = false;
        }
        if (should_time_pending) {
          lhs->set_timed(time_posix_format);
          should_time_pending = false;
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
        if (should_time_pending) {
          lhs->set_timed(time_posix_format);
          should_time_pending = false;
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

    case Token::Kind::Pipe:
    case Token::Kind::PipeAmpersand: {
      if (lhs == nullptr) {
        throw shit::ErrorWithLocation{token->source_location(),
                                      "Expected a command before the pipe"};
      }

      /* A |& pipe routes the standard error of the command on its left into the
         pipe as well, the shorthand for 2>&1 |. */
      const bool left_pipes_stderr =
          token->kind() == Token::Kind::PipeAmpersand;
      m_lexer.advance_past_last_peek();
      skip_newlines_after_pipe();

      Pipeline *pipeline =
          m_lexer.arena().create<Pipeline>(token->source_location());
      pipeline->append_command(
          left_pipes_stderr ? wrap_with_stderr_to_stdout(lhs) : lhs);

      Token *last_pipe_token = token;

      for (;;) {
        Command *rhs = parse_simple_command();
        if (rhs == nullptr) {
          /* An ampersand glued to the pipe under POSIX mode means the script
             used the bash |& stderr pipe in a mode that reads | then &, so
             the hint names the dialect rather than leaving a bare pipeline
             complaint. */
          Token *after = m_lexer.peek_shell_token();
          if (m_lexer.is_posix_mode() && after != nullptr &&
              after->kind() == Token::Kind::Ampersand &&
              after->source_location().position ==
                  last_pipe_token->source_location().position + 1)
          {
            throw shit::ErrorWithLocation{
                last_pipe_token->source_location(),
                "Unable to build the pipeline because no command follows "
                "the pipe. The |& stderr pipe is a bashism that POSIX mode "
                "does not read, use 2>&1 | instead"};
          }
          throw shit::ErrorWithLocation{
              last_pipe_token->source_location(),
              "Unable to build the pipeline because no command follows the "
              "pipe to receive the output"};
        }

        last_pipe_token = m_lexer.peek_shell_token();
        ASSERT(last_pipe_token != nullptr);
        const bool another_pipe =
            last_pipe_token->kind() == Token::Kind::Pipe ||
            last_pipe_token->kind() == Token::Kind::PipeAmpersand;
        const bool this_pipes_stderr =
            last_pipe_token->kind() == Token::Kind::PipeAmpersand;
        pipeline->append_command(another_pipe && this_pipes_stderr
                                     ? wrap_with_stderr_to_stdout(rhs)
                                     : rhs);
        if (another_pipe) {
          m_lexer.advance_past_last_peek();
          skip_newlines_after_pipe();
          continue;
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

/* Build one redir for descriptor fd. The operator is already consumed, and
   op_location is its position. A & touching the operator means a descriptor
   duplication, n>&m, otherwise a filename word follows. */
/* The 2>&1 record that points the standard error at the standard output, the
   dup the &> redirection, the |& pipe stage, and the bare >&file spelling
   build. */
static fn stderr_to_stdout_dup() wontthrow -> expressions::Redirection
{
  expressions::Redirection dup{};
  dup.fd = 2;
  dup.target = nullptr;
  dup.kind = expressions::Redirection::Kind::DuplicateOutput;
  dup.dup_fd = 1;
  return dup;
}

fn Parser::build_file_or_dup_redirection(
    i32 fd, Token::Kind op_kind, SourceLocation op_location,
    Maybe<SourceLocation> &first_location,
    ArrayList<expressions::Redirection> &out, bool fd_was_explicit) throws
    -> void
{
  if (!first_location) first_location = op_location;

  expressions::Redirection redir{};
  redir.fd = fd;
  redir.target = nullptr;
  redir.dup_fd = -1;

  {
    Token *after = m_lexer.peek_shell_token();
    ASSERT(after != nullptr);
    if (after->kind() == Token::Kind::Ampersand &&
        after->source_location().position ==
            op_location.position + op_location.length)
    {
      m_lexer.advance_past_last_peek();
      Token *from = m_lexer.next_shell_token();
      if (from->kind() != Token::Kind::Word) {
        throw ErrorWithLocation{from->source_location(),
                                "Expected a descriptor after '&'"};
      }
      const Word &from_word = static_cast<tokens::WordToken *>(from)->word();

      redir.kind = (op_kind == Token::Kind::Less)
                       ? expressions::Redirection::Kind::DuplicateInput
                       : expressions::Redirection::Kind::DuplicateOutput;

      const String literal = from_word.to_literal_string();

      /* The close form >&- and <&- closes fd outright. The lexer hands the dash
         back as part of the following word, so it is matched on the literal. */
      if (literal == "-") {
        redir.dup_fd = expressions::Redirection::DUP_FD_CLOSE;
        out.push(redir);
        return;
      }

      /* A word that is wholly digits names the descriptor at parse time, the
         fast path that needs no expansion. Anything else, such as $4 or ${fd},
         is a dynamic descriptor resolved when the redirection runs. */
      bool is_all_digits = !literal.is_empty();
      for (usize i = 0; i < literal.count(); i++) {
        if (literal[i] < '0' || literal[i] > '9') {
          is_all_digits = false;
          break;
        }
      }

      if (is_all_digits) {
        const let parsed = utils::parse_decimal_integer(literal);
        if (parsed.is_error()) {
          throw ErrorWithLocation{from->source_location(),
                                  parsed.error().message()};
        }
        redir.dup_fd = static_cast<i32>(parsed.value());
        out.push(redir);
        return;
      }

      /* A bare >&word in every mood but POSIX may be the csh both-streams
         spelling, cmd >&/dev/null, which bash decides after the expansion,
         a number or a dash duplicates and anything else writes both streams
         to the file. The flag carries that reading to the resolution, while
         an explicit descriptor as in 2>&word keeps the strict error. */
      redir.target = from;
      redir.dup_fd = -1;
      redir.dup_may_be_filename = op_kind == Token::Kind::Greater &&
                                  !fd_was_explicit && !m_lexer.is_posix_mode();
      out.push(redir);
      return;
    }
  }

  {
    Token *after = m_lexer.peek_shell_token();
    ASSERT(after != nullptr);
    /* The second character must touch the operator, so a real pipe in cmd >file
       | next and a comparison stay separate from the >| and <> operators. */
    const bool is_adjacent = after->source_location().position ==
                             op_location.position + op_location.length;

    /* >| truncates the target even under noclobber, the explicit override. */
    if (op_kind == Token::Kind::Greater && after->kind() == Token::Kind::Pipe &&
        is_adjacent)
    {
      m_lexer.advance_past_last_peek();
      Token *target = m_lexer.next_shell_token();
      if (target->kind() != Token::Kind::Word) {
        throw ErrorWithLocation{target->source_location(),
                                "Expected a filename after '>|'"};
      }
      redir.kind = expressions::Redirection::Kind::TruncateOutputOverride;
      redir.target = target;
      out.push(redir);
      return;
    }

    /* <> opens the target for reading and writing, creating it if absent. */
    if (op_kind == Token::Kind::Less && after->kind() == Token::Kind::Greater &&
        is_adjacent)
    {
      m_lexer.advance_past_last_peek();
      Token *target = m_lexer.next_shell_token();
      if (target->kind() != Token::Kind::Word) {
        throw ErrorWithLocation{target->source_location(),
                                "Expected a filename after '<>'"};
      }
      redir.kind = expressions::Redirection::Kind::ReadWrite;
      redir.target = target;
      out.push(redir);
      return;
    }
  }

  Token *target = m_lexer.next_shell_token();
  ASSERT(target != nullptr);
  if (target->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{target->source_location(),
                            "Expected a filename after the redir"};
  }
  switch (op_kind) {
  case Token::Kind::Greater:
    redir.kind = expressions::Redirection::Kind::TruncateOutput;
    break;
  case Token::Kind::DoubleGreater:
    redir.kind = expressions::Redirection::Kind::AppendOutput;
    break;
  default: redir.kind = expressions::Redirection::Kind::ReadInput; break;
  }
  redir.target = target;
  out.push(redir);
}

fn Parser::build_both_streams_redirection(
    bool append, SourceLocation op_location,
    Maybe<SourceLocation> &first_location,
    ArrayList<expressions::Redirection> &out) throws -> void
{
  /* The standard output goes to the file, then the standard error is made to
     follow it, the same pair bash builds for &>file. */
  build_file_or_dup_redirection(
      1, append ? Token::Kind::DoubleGreater : Token::Kind::Greater,
      op_location, first_location, out, /*fd_was_explicit=*/true);
  out.push(stderr_to_stdout_dup());
}

fn Parser::build_here_string_redirection(
    SourceLocation op_location, Maybe<SourceLocation> &first_location,
    ArrayList<expressions::Redirection> &out) throws -> void
{
  if (!first_location) first_location = op_location;

  Token *word = m_lexer.next_shell_token();
  ASSERT(word != nullptr);
  if (word->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{word->source_location(),
                            "Expected a word after '<<<'"};
  }

  expressions::Redirection redir{};
  redir.fd = 0;
  redir.kind = expressions::Redirection::Kind::HereString;
  redir.target = word;
  redir.dup_fd = -1;
  redir.heredoc_body = nullptr;
  redir.heredoc_expand = false;
  out.push(redir);
}

mustuse fn Parser::wrap_with_stderr_to_stdout(Command *command) throws
    -> Command *
{
  ASSERT(command != nullptr);
  let redirections = ArrayList<expressions::Redirection>{};
  redirections.push(stderr_to_stdout_dup());
  return m_lexer.arena().create<RedirectedCommand>(
      command->source_location(), command, steal(redirections));
}

fn Parser::build_heredoc_redirection(
    i32 fd, SourceLocation op_location, Maybe<SourceLocation> &first_location,
    ArrayList<expressions::Redirection> &out) throws -> void
{
  if (!first_location) first_location = op_location;

  Token *delimiter_token = m_lexer.next_shell_token();
  ASSERT(delimiter_token != nullptr);
  if (delimiter_token->kind() != Token::Kind::Word) {
    /* A <<<word in POSIX mode tokenizes as << then <word, so the stray <
       here means the script used the bash here-string in a mode that does
       not read it. The hint names the dialect rather than leaving a bare
       delimiter complaint. */
    if (delimiter_token->kind() == Token::Kind::Less) {
      throw ErrorWithLocation{
          delimiter_token->source_location(),
          "Expected a heredoc delimiter. The <<< here-string is a bashism "
          "that POSIX mode does not read, use a heredoc instead"};
    }
    throw ErrorWithLocation{delimiter_token->source_location(),
                            "Expected a heredoc delimiter"};
  }
  const Word &delimiter_word =
      static_cast<tokens::WordToken *>(delimiter_token)->word();

  const let delimiter_literal = delimiter_word.to_literal_string();
  let delimiter = delimiter_literal.view();
  bool strip_tabs = false;
  /* <<- strips leading tabs, the dash touching the operator. The dash counts
     only when it is unquoted, so <<'-EOF' keeps the dash as part of the
     delimiter and is a plain heredoc terminated by -EOF rather than a tab
     stripping one terminated by EOF. */
  const let has_unquoted_leading_dash =
      !delimiter_word.segments.is_empty() &&
      delimiter_word.segments[0].kind == WordSegment::Kind::UnquotedText &&
      !delimiter_word.segments[0].text.is_empty() &&
      delimiter_word.segments[0].text.view()[0] == '-';
  if (has_unquoted_leading_dash && !delimiter.is_empty() &&
      delimiter[0] == '-' &&
      delimiter_token->source_location().position ==
          op_location.position + op_location.length)
  {
    strip_tabs = true;
    delimiter = delimiter.substring(1);
  }

  LOG(verbosity::Debug,
      "registering a heredoc redirection with delimiter '%.*s'",
      static_cast<int>(delimiter.length), delimiter.data);

  /* A quoted delimiter, such as <<'EOF', keeps the body literal. */
  bool should_expand = true;
  for (const WordSegment &segment : delimiter_word.segments) {
    if (segment.kind != WordSegment::Kind::UnquotedText) {
      should_expand = false;
      break;
    }
  }

  expressions::Redirection redir{};
  redir.fd = fd;
  redir.kind = expressions::Redirection::Kind::Heredoc;
  redir.target = nullptr;
  redir.dup_fd = -1;
  redir.heredoc_body = m_lexer.register_heredoc(delimiter, strip_tabs);
  redir.heredoc_expand = should_expand;
  out.push(redir);
}

mustuse fn Parser::try_parse_descriptor_prefixed_redirection(
    const tokens::WordToken *word_token, SourceLocation word_location,
    Maybe<SourceLocation> &first_location,
    ArrayList<expressions::Redirection> &out) throws -> bool
{
  m_lexer.advance_past_last_peek();
  Token *next = m_lexer.peek_shell_token();
  ASSERT(next != nullptr);
  const let nk = next->kind();
  if ((nk == Token::Kind::Greater || nk == Token::Kind::DoubleGreater ||
       nk == Token::Kind::Less || nk == Token::Kind::DoubleLess) &&
      next->source_location().position ==
          word_location.position + word_location.length)
  {
    const let op_location = next->source_location();
    m_lexer.advance_past_last_peek();
    const let literal = word_token->word().to_literal_string();
    const let parsed = utils::parse_decimal_integer(literal);
    if (parsed.is_error()) {
      throw ErrorWithLocation{word_location, parsed.error().message()};
    }
    const let fd = static_cast<i32>(parsed.value());
    if (nk == Token::Kind::DoubleLess) {
      build_heredoc_redirection(fd, op_location, first_location, out);
    } else {
      build_file_or_dup_redirection(fd, nk, op_location, first_location, out,
                                    /*fd_was_explicit=*/true);
    }
    return true;
  }
  return false;
}

/* Peek the next token and, when it begins a redirection, parse it. A digit word
   touching a redirect operator is a descriptor prefix, such as the 2 in 2>file.
   A bare redirect operator targets descriptor 0 for input or 1 for output. */
mustuse fn Parser::try_parse_trailing_redirection(
    ArrayList<expressions::Redirection> &out) throws -> bool
{
  Maybe<SourceLocation> ignored_first_location;

  Token *token = m_lexer.peek_shell_token();
  ASSERT(token != nullptr);

  switch (token->kind()) {
  case Token::Kind::Greater:
  case Token::Kind::DoubleGreater:
  case Token::Kind::Less: {
    const let op_kind = token->kind();
    const let op_location = token->source_location();
    m_lexer.advance_past_last_peek();
    build_file_or_dup_redirection((op_kind == Token::Kind::Less) ? 0 : 1,
                                  op_kind, op_location, ignored_first_location,
                                  out, /*fd_was_explicit=*/false);
    return true;
  }

  case Token::Kind::AmpersandGreater:
  case Token::Kind::AmpersandDoubleGreater: {
    const let op_kind = token->kind();
    const let op_location = token->source_location();
    m_lexer.advance_past_last_peek();
    build_both_streams_redirection(op_kind ==
                                       Token::Kind::AmpersandDoubleGreater,
                                   op_location, ignored_first_location, out);
    return true;
  }

  case Token::Kind::DoubleLess: {
    const let op_location = token->source_location();
    m_lexer.advance_past_last_peek();
    build_heredoc_redirection(0, op_location, ignored_first_location, out);
    return true;
  }

  case Token::Kind::TripleLess: {
    const let op_location = token->source_location();
    m_lexer.advance_past_last_peek();
    build_here_string_redirection(op_location, ignored_first_location, out);
    return true;
  }

  case Token::Kind::Word: {
    const tokens::WordToken *word_token =
        static_cast<tokens::WordToken *>(token);
    if (!word_token->word().is_all_ascii_digits()) return false;

    const let word_location = token->source_location();
    if (try_parse_descriptor_prefixed_redirection(word_token, word_location,
                                                  ignored_first_location, out))
    {
      return true;
    }

    /* A bare number that does not prefix a redirect operator is not a trailing
       redirection. The helper already consumed it, so report it as an
       unexpected token rather than silently dropping it. */
    throw ErrorWithLocation{word_location,
                            "Unexpected word after a compound command"};
  }

  default: return false;
  }
}

mustuse fn Parser::attach_trailing_redirections(Command *compound) throws
    -> Command *
{
  ASSERT(compound != nullptr);

  let redirections = ArrayList<expressions::Redirection>{};
  while (try_parse_trailing_redirection(redirections)) {
    /* Keep consuming, so a chain like done >out 2>&1 attaches every one. */
  }

  if (redirections.is_empty()) return compound;

  return m_lexer.arena().create<RedirectedCommand>(
      compound->source_location(), compound, steal(redirections));
}

/* return: a command, a compound command, or nullptr when a list terminator is
   next. A reserved word or a group opener in command position introduces a
   compound command. */
/* The bash assignment builtins, which parse a NAME=(...) argument as an array
   assignment rather than a word. local and declare assign in a scope while
   readonly and export reach the global store. */
static pure fn is_assignment_builtin_name(StringView name) wontthrow -> bool
{
  return name == "local" || name == "declare" || name == "typeset" ||
         name == "readonly" || name == "export";
}

hot fn Parser::parse_simple_command() throws -> Command *
{
  Maybe<SourceLocation> source_location;
  ArrayList<Token *> args_accumulator{};
  let local_vars = ArrayList<prefix_assignment>{heap_allocator()};
  let array_args = ArrayList<array_builtin_assignment>{heap_allocator()};
  let redirections = ArrayList<expressions::Redirection>{};

  auto build_command = [&]() -> Command * {
    if (!source_location) return nullptr;

    ArrayList<const Token *> args{};
    args.reserve(args_accumulator.count());
    for (Token *t : args_accumulator)
      args.push(t);

    SimpleCommand *c =
        m_lexer.arena().create<SimpleCommand>(*source_location, steal(args));
    if (local_vars.count() != 0) c->set_local_vars(steal(local_vars));
    if (!array_args.is_empty()) c->set_array_args(steal(array_args));
    if (!redirections.is_empty()) c->set_redirections(steal(redirections));
    return c;
  };

  auto add_redirection = [&](i32 fd, Token::Kind op_kind,
                             SourceLocation op_location, bool fd_was_explicit) {
    build_file_or_dup_redirection(fd, op_kind, op_location, source_location,
                                  redirections, fd_was_explicit);
  };

  for (;;) {
    Token *token = m_lexer.peek_shell_token();
    ASSERT(token != nullptr);

    /* A reserved word or a group opener in command position introduces a
       compound command. A list terminator means there is no command here. A
       bare array assignment already collected leaves array_args non-empty, so
       the command is not empty and a following terminator must build it rather
       than be read as a leading keyword. */
    if (args_accumulator.is_empty() && local_vars.count() == 0 &&
        array_args.is_empty())
    {
      /* A standalone '{' in command position opens a brace group, and a
         standalone '}' closes the enclosing one. Both arrive as words, so they
         are matched on the text before the kind switch. A '}' with no open
         group is left for the caller, which reports it as unexpected. */
      if (is_brace_word(token, '{')) {
        return attach_trailing_redirections(parse_brace_group());
      }
      if (is_brace_word(token, '}')) return nullptr;

      /* [[ arrives as an ordinary word, so it is matched on the text before the
         kind switch, the way the braces are. */
      if (is_unquoted_word(token, "[[")) {
        return attach_trailing_redirections(parse_conditional_command());
      }

      /* select is not a reserved word in the lexer, so it is matched on the
         text in bash mode and parsed like a for header that menus its words. */
      if (m_lexer.is_bash_compatible() && is_unquoted_word(token, "select")) {
        return attach_trailing_redirections(parse_select());
      }

      switch (token->kind()) {
      case Token::Kind::If: return attach_trailing_redirections(parse_if());
      case Token::Kind::While:
        return attach_trailing_redirections(parse_while_or_until(false));
      case Token::Kind::Until:
        return attach_trailing_redirections(parse_while_or_until(true));
      case Token::Kind::For: return attach_trailing_redirections(parse_for());
      case Token::Kind::Case: return attach_trailing_redirections(parse_case());
      case Token::Kind::LeftParen:
        return attach_trailing_redirections(parse_paren_command());
      case Token::Kind::DoubleLeftSquareBracket:
        return attach_trailing_redirections(parse_conditional_command());

      case Token::Kind::Then:
      case Token::Kind::Do:
      case Token::Kind::Done:
      case Token::Kind::Fi:
      case Token::Kind::Else:
      case Token::Kind::Elif:
      case Token::Kind::Esac:
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
    case Token::Kind::When: {
      /* A run of digits touching a redir operator is a descriptor prefix,
         such as the 2 in 2>file or 2>&1, not an argument. */
      if (token->kind() == Token::Kind::Word) {
        const tokens::WordToken *word_token =
            static_cast<tokens::WordToken *>(token);
        if (word_token->word().is_all_ascii_digits()) {
          const let word_location = token->source_location();
          if (try_parse_descriptor_prefixed_redirection(
                  word_token, word_location, source_location, redirections))
          {
            break;
          }
          /* A digit run with no adjacent redirect operator is an ordinary
             argument, and the helper already consumed it. */
          if (!source_location) source_location = word_location;
          args_accumulator.push(token);
          break;
        }
      }
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = token->source_location();
      args_accumulator.push(token);
    } break;

    case Token::Kind::Function:
      /* The bash function keyword begins a definition when it leads the
         command, with an optional empty () pair before the body. Anywhere else
         it is an ordinary command word. */
      if (args_accumulator.is_empty() && local_vars.count() == 0) {
        m_lexer.advance_past_last_peek();
        return parse_keyword_function_definition();
      }
      m_lexer.advance_past_last_peek();
      if (!source_location) source_location = token->source_location();
      args_accumulator.push(token);
      break;

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

      Assignment *a = static_cast<Assignment *>(token);

      /* Peek the next token. A compound list condition, a compound terminator,
       * or the end of input means the assignment stands alone. */
      Token *next = m_lexer.peek_shell_token();
      ASSERT(next != nullptr);

      /* NAME=(...) and NAME+=(...) are bash array assignments when the ( sits
         immediately after the =, with no space between them. */
      let const is_array_assignment =
          next->kind() == Token::Kind::LeftParen &&
          next->source_location().position ==
              a->source_location().position + a->source_location().length;

      /* Once a command word is present, an assignment-looking token is an
         ordinary argument, except an array assignment given to an assignment
         builtin such as local. That is captured so the builtin sets the array,
         rather than the ( being read as a stray command. */
      if (!args_accumulator.is_empty()) {
        if (is_array_assignment) {
          let const command_name = args_accumulator[0]->raw_string();
          if (is_assignment_builtin_name(command_name.view())) {
            ArrayList<const Token *> elements = consume_bash_array_assignment();
            array_args.push(array_builtin_assignment{
                a->key().clone(), steal(elements), a->is_append()});
            break;
          }
        }
        args_accumulator.push(token);
        break;
      }

      /* NAME=(...) leading the command is a bash array assignment. The element
         words are captured in every mood and join the prefix sequence the way
         a scalar assignment does, so a command-less line of several
         assignments, some of them arrays such as flags= pvars=() specs=(),
         applies them all in order. POSIX mode downgrades the assignment to an
         empty scalar at evaluation, so a sourced login profile that carries
         one keeps sourcing. */
      if (is_array_assignment) {
        ArrayList<const Token *> elements = consume_bash_array_assignment();
        array_args.push(array_builtin_assignment{
            a->key().clone(), steal(elements), a->is_append()});
        break;
      }

      if (local_vars.count() == 0 &&
          (next->flags() & Token::Flag::CompoundList ||
           next->kind() == Token::Kind::EndOfFile ||
           is_compound_terminator(next->kind())))
      {
        /* A lone assignment with no other on the line takes the dedicated
           AssignCommand fast path. */
        return m_lexer.arena().create<AssignCommand>(*source_location, a);
      } else {
        /* The assignment joins the prefix sequence in source order, either
           ahead of a command word or as one of several assignments on a
           command-less line. The ordered list lets a later assignment see an
           earlier one and a repeated name accumulate, which a map would lose.
           The command-less line persists the whole sequence in SimpleCommand.
         */
        local_vars.push(prefix_assignment{
            a->key().clone(), Word{a->value_word()}, a->is_append()});
      }
    } break;

    case Token::Kind::Greater:
    case Token::Kind::DoubleGreater:
    case Token::Kind::Less: {
      const let op_kind = token->kind();
      const let op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      add_redirection((op_kind == Token::Kind::Less) ? 0 : 1, op_kind,
                      op_location, /*fd_was_explicit=*/false);
    } break;

    case Token::Kind::AmpersandGreater:
    case Token::Kind::AmpersandDoubleGreater: {
      const let op_kind = token->kind();
      const let op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      build_both_streams_redirection(
          op_kind == Token::Kind::AmpersandDoubleGreater, op_location,
          source_location, redirections);
    } break;

    case Token::Kind::DoubleLess: {
      const let op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      build_heredoc_redirection(0, op_location, source_location, redirections);
    } break;

    case Token::Kind::TripleLess: {
      const let op_location = token->source_location();
      m_lexer.advance_past_last_peek();
      build_here_string_redirection(op_location, source_location, redirections);
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

  LOG(verbosity::Debug, "parsing an if clause at byte %zu", location.position);

  let branches = ArrayList<if_branch>{};
  const Expression *otherwise = nullptr;

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
    branches.push(if_branch{condition, body});

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

  return m_lexer.arena().create<IfClause>(location, steal(branches), otherwise);
}

hot fn Parser::parse_while_or_until(bool is_until) throws -> Command *
{
  Token *keyword = m_lexer.next_shell_token();
  ASSERT(keyword != nullptr);
  const let location = keyword->source_location();

  LOG(verbosity::Debug, "parsing a %s loop at byte %zu",
      is_until ? "until" : "while", location.position);

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
  reject_empty_loop_body(body);
  Token *done_token = m_lexer.next_shell_token();
  ASSERT(done_token != nullptr);
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated loop", m_lexer.source(), "done",
                       done_token->source_location());
  }

  let loop =
      m_lexer.arena().create<WhileLoop>(location, condition, body, is_until);
  const SourceLocation done_location = done_token->source_location();
  loop->set_source_end_position(done_location.position + done_location.length);
  return loop;
}

static fn word_token_from_assignment(BumpArena &arena,
                                     const Assignment *a) throws
    -> tokens::WordToken *;

/* Whether the (( construct opening just before body_start closes with two
   adjacent right parentheses at depth zero, the test that separates an
   arithmetic command from a subshell whose first child is a subshell. The
   scan tracks quote runs and backslash escapes so a parenthesis inside a
   string stays text. An unterminated construct answers no and the subshell
   parser reports it. */
static fn double_paren_closes_adjacent(StringView source,
                                       usize body_start) wontthrow -> bool
{
  usize depth = 0;
  char quote = 0;
  for (usize i = body_start; i < source.length; i++) {
    let const c = source[i];
    if (quote == '\'') {
      if (c == '\'') quote = 0;
      continue;
    }
    if (c == '\\') {
      i++;
      continue;
    }
    if (quote == '"') {
      if (c == '"') quote = 0;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == '(') {
      depth++;
      continue;
    }
    if (c == ')') {
      if (depth > 0) {
        depth--;
        continue;
      }
      return i + 1 < source.length && source[i + 1] == ')';
    }
  }
  return false;
}

static fn word_token_from_raw(BumpArena &arena, StringView text,
                              SourceLocation location) throws
    -> tokens::WordToken *;

hot fn Parser::parse_for() throws -> Command *
{
  Token *keyword = m_lexer.next_shell_token();
  ASSERT(keyword != nullptr);
  const let location = keyword->source_location();

  LOG(verbosity::Debug, "parsing a for loop at byte %zu", location.position);

  /* A for header that opens with (( is the bash C-style loop, distinct from
     the for name in words form. POSIX keeps the bare-name reading dash
     requires, while the bash and the default mood take the C-style header,
     the same mood policy the (( )) arithmetic command follows. */
  if (!m_lexer.is_posix_mode()) {
    Token *peeked = m_lexer.peek_shell_token();
    ASSERT(peeked != nullptr);
    if (peeked->kind() == Token::Kind::LeftParen) {
      Token *first_paren = m_lexer.next_shell_token();
      Token *second = m_lexer.peek_shell_token();
      ASSERT(second != nullptr);
      if (second->kind() == Token::Kind::LeftParen &&
          second->source_location().position ==
              first_paren->source_location().position + 1)
      {
        return parse_c_style_for(location, first_paren);
      }
      throw ErrorWithLocation{first_paren->source_location(),
                              "Expected '((' or a variable name after 'for'"};
    }
  }

  Token *name_token = m_lexer.next_shell_token();
  ASSERT(name_token != nullptr);
  /* A keyword such as for names the loop variable when it sits in the name
     slot, rebuilt into a word from its source text the way a case pattern takes
     a keyword as a literal. */
  if (name_token->kind() != Token::Kind::Word) {
    const String raw = name_token->raw_string();
    if (KEYWORDS.find(raw.view()).has_value())
      name_token = word_token_from_raw(m_lexer.arena(), raw.view(),
                                       name_token->source_location());
  }
  if (name_token->kind() != Token::Kind::Word) {
    /* A (( in the name slot under POSIX mode means the script used the bash
       C-style loop in a mode that keeps the dash reading, so the hint names
       the dialect rather than leaving a bare name complaint. */
    if (m_lexer.is_posix_mode() && name_token->kind() == Token::Kind::LeftParen)
    {
      throw ErrorWithLocation{
          name_token->source_location(),
          "Expected a variable name after 'for'. The for ((...)) C-style "
          "loop is a bashism that POSIX mode does not read, use a while "
          "loop instead"};
    }
    throw ErrorWithLocation{name_token->source_location(),
                            "Expected a variable name after 'for'"};
  }

  /* The loop variable must be a plain name. A $ expansion such as for $f, a
     quoted word, or a non-identifier names a variable the user did not mean, so
     it is rejected the way dash and bash reject it. */
  const Word &name_word =
      static_cast<const tokens::WordToken *>(name_token)->word();
  bool name_is_plain =
      name_word.segments.count() == 1 &&
      name_word.segments[0].kind == WordSegment::Kind::UnquotedText;
  if (name_is_plain) {
    const StringView name_text = name_word.segments[0].text.view();
    let const is_name_start = [](char c) wontthrow -> bool {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    name_is_plain = name_text.length > 0 && is_name_start(name_text[0]);
    for (usize i = 1; name_is_plain && i < name_text.length; i++)
      name_is_plain = is_name_start(name_text[i]) ||
                      (name_text[i] >= '0' && name_text[i] <= '9');
  }
  if (!name_is_plain) {
    throw ErrorWithLocation{
        name_token->source_location(),
        StringView{"Bad for loop variable, '"} + name_token->raw_string() +
            "' is not a plain name, drop the '$' and any quotes"};
  }

  const let variable_name = name_token->raw_string();

  ArrayList<const Token *> words{};
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
      /* A NAME=VALUE word in the list lexes as an assignment, so it is rebuilt
         into a plain word the way a case pattern is. */
      if (word->kind() == Token::Kind::Assignment) {
        m_lexer.advance_past_last_peek();
        words.push(word_token_from_assignment(m_lexer.arena(),
                                              static_cast<Assignment *>(word)));
        continue;
      }
      if (word->kind() != Token::Kind::Word) {
        /* A keyword such as function or time is an ordinary word in the list,
           rebuilt from its source text the way a case pattern takes one. A
           separator or operator that is not a keyword ends the list. */
        const String raw = word->raw_string();
        if (!KEYWORDS.find(raw.view()).has_value()) break;
        m_lexer.advance_past_last_peek();
        words.push(word_token_from_raw(m_lexer.arena(), raw.view(),
                                       word->source_location()));
        continue;
      }
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
    String detail = "Expected 'do'";
    if (!has_in_clause) {
      detail = "Expected 'do', or 'in WORDS' before it; without 'in' the loop "
               "walks the positional parameters";
    }
    throw ErrorWithLocationAndDetails{location, "Unterminated for loop",
                                      do_token->source_location(), detail};
  }

  Expression *body = parse_command_list({Token::Kind::Done});
  reject_empty_loop_body(body);
  Token *done_token = m_lexer.next_shell_token();
  ASSERT(done_token != nullptr);
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated for loop", m_lexer.source(),
                       "done", done_token->source_location());
  }

  let loop = m_lexer.arena().create<ForLoop>(location, variable_name.view(),
                                             steal(words), has_in_clause, body);
  const SourceLocation done_location = done_token->source_location();
  loop->set_source_end_position(done_location.position + done_location.length);
  return loop;
}

/* A bash select loop, select name in words; do BODY; done. It shares the for
   header shape, a name then an optional in clause, but at run time it prints a
   numbered menu and reads a choice rather than walking the words. */
hot fn Parser::parse_select() throws -> Command *
{
  Token *keyword = m_lexer.next_shell_token();
  ASSERT(keyword != nullptr);
  ASSERT(is_unquoted_word(keyword, "select"));
  const let location = keyword->source_location();

  LOG(verbosity::Debug, "parsing a select loop at byte %zu", location.position);

  Token *name_token = m_lexer.next_shell_token();
  ASSERT(name_token != nullptr);
  /* A keyword such as for names the loop variable when it sits in the name
     slot, rebuilt into a word from its source text the way a case pattern takes
     a keyword as a literal. */
  if (name_token->kind() != Token::Kind::Word) {
    const String raw = name_token->raw_string();
    if (KEYWORDS.find(raw.view()).has_value())
      name_token = word_token_from_raw(m_lexer.arena(), raw.view(),
                                       name_token->source_location());
  }
  if (name_token->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{name_token->source_location(),
                            "Expected a variable name after 'select'"};
  }
  const let variable_name = name_token->raw_string();

  ArrayList<const Token *> words{};
  bool has_in_clause = false;
  Token *peeked = m_lexer.peek_shell_token();
  ASSERT(peeked != nullptr);
  if (peeked->kind() == Token::Kind::Word && peeked->raw_string() == "in") {
    m_lexer.advance_past_last_peek();
    has_in_clause = true;
    for (;;) {
      Token *word = m_lexer.peek_shell_token();
      ASSERT(word != nullptr);
      /* A NAME=VALUE word in the list lexes as an assignment, so it is rebuilt
         into a plain word the way a case pattern is. */
      if (word->kind() == Token::Kind::Assignment) {
        m_lexer.advance_past_last_peek();
        words.push(word_token_from_assignment(m_lexer.arena(),
                                              static_cast<Assignment *>(word)));
        continue;
      }
      if (word->kind() != Token::Kind::Word) {
        /* A keyword such as function or time is an ordinary word in the list,
           rebuilt from its source text the way a case pattern takes one. A
           separator or operator that is not a keyword ends the list. */
        const String raw = word->raw_string();
        if (!KEYWORDS.find(raw.view()).has_value()) break;
        m_lexer.advance_past_last_peek();
        words.push(word_token_from_raw(m_lexer.arena(), raw.view(),
                                       word->source_location()));
        continue;
      }
      m_lexer.advance_past_last_peek();
      words.push(word);
    }
  }

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
    throw ErrorWithLocationAndDetails{location, "Unterminated select loop",
                                      do_token->source_location(),
                                      "Expected 'do'"};
  }

  Expression *body = parse_command_list({Token::Kind::Done});
  reject_empty_loop_body(body);
  Token *done_token = m_lexer.next_shell_token();
  ASSERT(done_token != nullptr);
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated select loop", m_lexer.source(),
                       "done", done_token->source_location());
  }

  return m_lexer.arena().create<SelectLoop>(location, variable_name.view(),
                                            steal(words), has_in_clause, body);
}

/* A NAME=VALUE token lexes as an assignment, but in a case word or a case
   pattern it is a plain word, so it is rebuilt into a word token that keeps the
   value's expansion segments after the NAME= prefix. */
static fn word_token_from_assignment(BumpArena &arena,
                                     const Assignment *a) throws
    -> tokens::WordToken *
{
  Word word{};
  String prefix = a->key().clone();
  prefix += a->is_append() ? "+=" : "=";
  word.segments.push(
      WordSegment{WordSegment::Kind::UnquotedText, steal(prefix), false});
  for (const WordSegment &segment : a->value_word().segments) {
    WordSegment copy{segment.kind, segment.text.clone(),
                     segment.is_in_double_quotes};
    copy.folded_arithmetic_result = segment.folded_arithmetic_result;
    copy.cached_substitution_ast = segment.cached_substitution_ast;
    copy.cached_substitution_generation =
        segment.cached_substitution_generation;
    word.segments.push(steal(copy));
  }
  return arena.create<tokens::WordToken>(a->source_location(), steal(word));
}

/* A keyword such as done or for is a plain literal word when it sits in a case
   pattern, so it is rebuilt into a word token from its source text. */
static fn word_token_from_raw(BumpArena &arena, StringView text,
                              SourceLocation location) throws
    -> tokens::WordToken *
{
  Word word{};
  word.segments.push(
      WordSegment{WordSegment::Kind::UnquotedText, String{text}, false});
  return arena.create<tokens::WordToken>(location, steal(word));
}

hot fn Parser::parse_case() throws -> Command *
{
  Token *keyword = m_lexer.next_shell_token();
  ASSERT(keyword != nullptr);
  const let location = keyword->source_location();

  LOG(verbosity::Debug, "parsing a case clause at byte %zu", location.position);

  Token *word = m_lexer.next_shell_token();
  ASSERT(word != nullptr);
  if (word->kind() == Token::Kind::Assignment) {
    word = word_token_from_assignment(m_lexer.arena(),
                                      static_cast<Assignment *>(word));
  } else if (word->kind() != Token::Kind::Word) {
    /* A keyword such as esac is the matched word when it sits in the word
       slot, rebuilt from its source text the way the for and select names
       take one. */
    let const raw = word->raw_string();
    if (KEYWORDS.find(raw.view()).has_value())
      word = word_token_from_raw(m_lexer.arena(), raw.view(),
                                 word->source_location());
    else
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

  let items = ArrayList<case_item>{};

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

    for (;;) {
      Token *pattern = m_lexer.next_shell_token();
      ASSERT(pattern != nullptr);

      if (pattern->kind() == Token::Kind::Assignment) {
        pattern = word_token_from_assignment(
            m_lexer.arena(), static_cast<Assignment *>(pattern));
      } else if (pattern->kind() != Token::Kind::Word) {
        /* A keyword such as done used as a literal pattern, the way ble.sh
           writes (done), is taken by its source text rather than rejected. */
        const SourceLocation pattern_location = pattern->source_location();
        const StringView text = m_lexer.source().substring_of_length(
            pattern_location.position, pattern_location.length);
        if (KEYWORDS.find(text).has_value()) {
          pattern =
              word_token_from_raw(m_lexer.arena(), text, pattern_location);
        } else {
          throw ErrorWithLocationAndDetails{
              location, "Unterminated case", pattern_location,
              "Expected a pattern to start an arm, or 'esac' to end the case"};
        }
      }

      patterns.push(pattern);

      Token *separator = m_lexer.next_shell_token();
      ASSERT(separator != nullptr);

      if (separator->kind() == Token::Kind::Pipe) continue;
      if (separator->kind() == Token::Kind::RightParen) break;

      throw ErrorWithLocation{separator->source_location(),
                              "Expected '|' or ')' in a case pattern"};
    }

    Expression *body = parse_command_list(
        {Token::Kind::DoubleSemicolon, Token::Kind::SemicolonAmpersand,
         Token::Kind::DoubleSemicolonAmpersand, Token::Kind::Esac});

    Token *after = m_lexer.peek_shell_token();
    ASSERT(after != nullptr);
    case_terminator terminator = case_terminator::Break;
    bool is_last_arm = false;
    switch (after->kind()) {
    case Token::Kind::DoubleSemicolon: m_lexer.advance_past_last_peek(); break;
    case Token::Kind::SemicolonAmpersand:
      terminator = case_terminator::FallThrough;
      m_lexer.advance_past_last_peek();
      break;
    case Token::Kind::DoubleSemicolonAmpersand:
      terminator = case_terminator::ContinueMatch;
      m_lexer.advance_past_last_peek();
      break;
    case Token::Kind::Esac:
      m_lexer.advance_past_last_peek();
      is_last_arm = true;
      break;
    default: break;
    }

    items.push(case_item{steal(patterns), body, terminator});
    if (is_last_arm) break;
  }

  return m_lexer.arena().create<CaseClause>(location, word, steal(items));
}

hot fn Parser::parse_brace_group() throws -> Command *
{
  Token *open = m_lexer.next_shell_token();
  ASSERT(open != nullptr);
  ASSERT(is_brace_word(open, '{'));

  LOG(verbosity::Debug, "parsing a brace group at byte %zu",
      open->source_location().position);

  /* RightBracket in the terminator set stands for a standalone '}' word, which
     the command list leaves for this parser to consume. */
  Expression *body = parse_command_list({Token::Kind::RightBracket});

  Token *close = m_lexer.next_shell_token();
  ASSERT(close != nullptr);
  if (!is_brace_word(close, '}')) {
    /* The closing '}' is a reserved word only at the start of a command, so
       when it lacks a ';' or a newline before it the lexer reads it as an
       argument to the last command and the group never closes.
       throw_unterminated finds that stray '}' in the source and points the
       caret at it, telling the reader to put a ';' before it. */
    throw_unterminated(open->source_location(), "Unterminated brace group",
                       m_lexer.source(), "}", close->source_location());
  }

  BraceGroup *group =
      m_lexer.arena().create<BraceGroup>(open->source_location(), body);
  const SourceLocation close_location = close->source_location();
  group->set_source_end_position(close_location.position +
                                 close_location.length);
  return group;
}

/* A parenthesis in command position opens either a subshell or, when a second
   parenthesis sits right against it, a (( )) arithmetic command. The first
   parenthesis is consumed here so the next token can be peeked to choose. */
hot fn Parser::parse_paren_command() throws -> Command *
{
  Token *open = m_lexer.next_shell_token();
  ASSERT(open != nullptr);
  ASSERT(open->kind() == Token::Kind::LeftParen);

  /* A (( )) arithmetic command changes the POSIX meaning of two opening
     parentheses, which is a nested subshell, so POSIX mode keeps the
     nested-subshell reading the way dash parses it, while the bash and the
     default mood take the arithmetic command, the same mood policy the array
     literal follows. A (( that closes with a lone ) at depth zero, such as
     ((cmd; cmd); cmd), is a subshell whose first child is a subshell, the
     same backoff bash performs, decided by a quote-aware scan of the raw
     source before the arithmetic reading commits. */
  Token *next = m_lexer.peek_shell_token();
  ASSERT(next != nullptr);
  if (!m_lexer.is_posix_mode() && next->kind() == Token::Kind::LeftParen &&
      next->source_location().position ==
          open->source_location().position + 1 &&
      double_paren_closes_adjacent(m_lexer.source(),
                                   next->source_location().position + 1))
  {
    return parse_arithmetic_command(open);
  }
  return parse_subshell(open);
}

hot fn Parser::parse_subshell(Token *open) throws -> Command *
{
  ASSERT(open != nullptr);
  ASSERT(open->kind() == Token::Kind::LeftParen);

  LOG(verbosity::Debug, "parsing a subshell at byte %zu",
      open->source_location().position);

  Expression *body = parse_command_list({Token::Kind::RightParen});

  Token *close = m_lexer.next_shell_token();
  ASSERT(close != nullptr);
  if (close->kind() != Token::Kind::RightParen) {
    throw ErrorWithLocationAndDetails{open->source_location(),
                                      "Unterminated subshell",
                                      close->source_location(), "Expected ')'"};
  }

  let subshell =
      m_lexer.arena().create<Subshell>(open->source_location(), body);
  const SourceLocation close_location = close->source_location();
  subshell->set_source_end_position(close_location.position +
                                    close_location.length);
  return subshell;
}

/* Read the body of a (( )) construct. The first parenthesis is already consumed
   as open, so the second is taken here and then the tokens are walked, tracking
   parenthesis depth, until the closing )). The returned view points into the
   source between the two pairs. Shared by the arithmetic command and the
   C-style for header. */
hot fn Parser::capture_double_paren_body(Token *open) throws -> StringView
{
  ASSERT(open != nullptr);
  Token *second = m_lexer.next_shell_token();
  ASSERT(second != nullptr);
  ASSERT(second->kind() == Token::Kind::LeftParen);

  const usize body_start = second->source_location().position + 1;
  usize body_end = body_start;
  usize depth = 0;
  for (;;) {
    Token *t = m_lexer.next_shell_token();
    ASSERT(t != nullptr);
    if (t->kind() == Token::Kind::EndOfFile) {
      throw ErrorWithLocationAndDetails{open->source_location(),
                                        "Unterminated '(('",
                                        t->source_location(), "Expected '))'"};
    }
    if (t->kind() == Token::Kind::LeftParen) {
      depth++;
      continue;
    }
    if (t->kind() == Token::Kind::RightParen) {
      if (depth > 0) {
        depth--;
        continue;
      }
      Token *closing = m_lexer.peek_shell_token();
      ASSERT(closing != nullptr);
      if (closing->kind() == Token::Kind::RightParen &&
          closing->source_location().position ==
              t->source_location().position + 1)
      {
        body_end = t->source_location().position;
        m_lexer.advance_past_last_peek();
        break;
      }
      throw ErrorWithLocationAndDetails{open->source_location(),
                                        "Unterminated '(('",
                                        t->source_location(), "Expected '))'"};
    }
  }

  return m_lexer.source().substring_of_length(body_start,
                                              body_end - body_start);
}

/* A (( expr )) arithmetic command. The body is sliced from the source between
   the parentheses and evaluated as arithmetic at run time. */
hot fn Parser::parse_arithmetic_command(Token *open) throws -> Command *
{
  LOG(verbosity::Debug, "parsing an arithmetic command at byte %zu",
      open->source_location().position);

  const StringView body = capture_double_paren_body(open);
  /* The command's location spans the whole (( body )), the two opening and two
     closing parentheses around the captured body, so a runtime arithmetic error
     underlines the entire expression rather than only the opening parentheses.
   */
  const SourceLocation open_location = open->source_location();
  const SourceLocation full_location{open_location.position, body.length + 4,
                                     open_location.filename};
  return m_lexer.arena().create<expressions::ArithmeticCommand>(
      full_location, String{bump_allocator(m_lexer.arena()), body});
}

/* A bash C-style for, for (( init; cond; step )); do BODY; done. The header
   slice between the parentheses is split on its two top-level semicolons into
   the three arithmetic clauses, each evaluated at run time. */
hot fn Parser::parse_c_style_for(SourceLocation location, Token *open) throws
    -> Command *
{
  LOG(verbosity::Debug, "parsing a c-style for header at byte %zu",
      location.position);

  const StringView header = capture_double_paren_body(open);

  /* The two clause separators are the semicolons at parenthesis depth zero, so
     a grouped subexpression in a clause keeps its own semicolon-free shape. */
  usize separators[2] = {0, 0};
  usize separator_count = 0;
  usize depth = 0;
  for (usize i = 0; i < header.length; i++) {
    const char c = header[i];
    if (c == '(') {
      depth++;
    } else if (c == ')') {
      if (depth > 0) depth--;
    } else if (c == ';' && depth == 0) {
      if (separator_count < 2) separators[separator_count] = i;
      separator_count++;
    }
  }
  if (separator_count != 2) {
    throw ErrorWithLocation{
        location, "Expected '(( init; condition; step ))' in a C-style for"};
  }

  const Allocator allocator = bump_allocator(m_lexer.arena());
  const String init{allocator, header.substring_of_length(0, separators[0])};
  const String condition{
      allocator, header.substring_of_length(separators[0] + 1,
                                            separators[1] - separators[0] - 1)};
  const String step{allocator, header.substring(separators[1] + 1)};

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
    throw ErrorWithLocationAndDetails{location, "Unterminated for loop",
                                      do_token->source_location(),
                                      "Expected 'do'"};
  }

  Expression *body = parse_command_list({Token::Kind::Done});
  reject_empty_loop_body(body);
  Token *done_token = m_lexer.next_shell_token();
  ASSERT(done_token != nullptr);
  if (done_token->kind() != Token::Kind::Done) {
    throw_unterminated(location, "Unterminated for loop", m_lexer.source(),
                       "done", done_token->source_location());
  }

  return m_lexer.arena().create<expressions::CStyleForLoop>(
      location, steal(init), steal(condition), steal(step), body);
}

hot fn Parser::parse_conditional_command() throws -> Command *
{
  Token *open = m_lexer.next_shell_token();
  ASSERT(open != nullptr);
  ASSERT(is_unquoted_word(open, "[["));

  LOG(verbosity::Debug, "parsing a conditional command at byte %zu",
      open->source_location().position);

  /* The tokens between [[ and ]] are collected raw rather than run through the
     command parser, so a < or > inside is a string comparison and not a
     redirection, and && and || join primaries. The operand words are kept for
     the evaluator to expand without field splitting. */
  let elements = ArrayList<conditional_element>{};
  for (;;) {
    Token *t = m_lexer.next_shell_token();
    ASSERT(t != nullptr);
    if (is_unquoted_word(t, "]]")) break;
    if (t->kind() == Token::Kind::EndOfFile) {
      throw ErrorWithLocationAndDetails{open->source_location(),
                                        "Unterminated '[['",
                                        t->source_location(), "Expected ']]'"};
    }

    using Kind = conditional_element::Kind;
    switch (t->kind()) {
    case Token::Kind::DoubleAmpersand:
      elements.push({Kind::And, nullptr});
      break;
    case Token::Kind::DoublePipe: elements.push({Kind::Or, nullptr}); break;
    case Token::Kind::LeftParen:
      elements.push({Kind::OpenParen, nullptr});
      break;
    case Token::Kind::RightParen:
      elements.push({Kind::CloseParen, nullptr});
      break;
    case Token::Kind::Less: elements.push({Kind::Less, nullptr}); break;
    case Token::Kind::Greater: elements.push({Kind::Greater, nullptr}); break;
    case Token::Kind::Newline: continue;
    case Token::Kind::Word: {
      /* A bare ! is the negation operator, every other word is an operand the
         evaluator classifies by its text. */
      const String word_literal =
          static_cast<tokens::WordToken *>(t)->word().to_literal_string();
      if (word_literal == "!") {
        elements.push({Kind::Not, nullptr});
        break;
      }
      elements.push({Kind::Operand, t});

      /* The right side of =~ is a regular expression where (, ), and | are
         ordinary characters, so the lexer's split into separate paren and word
         tokens is rejoined here. Tokens with no whitespace between them form
         one regex operand, taken from the source span so the metacharacters
         survive. A single token, such as a variable or a quoted regex, is left
         alone so its expansion still happens. */
      if (word_literal == "=~") {
        Token *peek = m_lexer.peek_shell_token();
        if (peek != nullptr && !is_unquoted_word(peek, "]]") &&
            peek->kind() != Token::Kind::EndOfFile)
        {
          m_lexer.advance_past_last_peek();
          Token *first = peek;
          const usize start = first->source_location().position;
          usize end = start + first->source_location().length;
          usize joined_token_count = 1;
          for (;;) {
            Token *next = m_lexer.peek_shell_token();
            if (next == nullptr || is_unquoted_word(next, "]]") ||
                next->kind() == Token::Kind::EndOfFile)
              break;
            if (next->source_location().position != end) break;
            m_lexer.advance_past_last_peek();
            end = next->source_location().position +
                  next->source_location().length;
            joined_token_count++;
          }
          if (joined_token_count == 1) {
            elements.push({Kind::Operand, first});
          } else {
            const StringView regex_source =
                m_lexer.source().substring_of_length(start, end - start);
            elements.push({Kind::Operand,
                           word_token_from_raw(m_lexer.arena(), regex_source,
                                               first->source_location())});
          }
        }
      }
      break;
    }
    default:
      /* A reserved word or other token used as an operand keeps its token so
         the evaluator can read its text. */
      elements.push({Kind::Operand, t});
      break;
    }
  }

  return m_lexer.arena().create<expressions::ConditionalCommand>(
      open->source_location(), steal(elements));
}

hot fn Parser::parse_function_definition(Token *name_token) throws -> Command *
{
  ASSERT(name_token != nullptr);
  const let location = name_token->source_location();
  const let name = name_token->raw_string();

  LOG(verbosity::Debug, "parsing a function definition for '%s'", name.c_str());

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

  /* The definition's span ends where its body ends, recorded so declare -f
     can print the definition text from the source. */
  let definition =
      m_lexer.arena().create<FunctionDefinition>(location, name.view(), body);
  definition->set_source_end_position(body->source_end_position());
  return definition;
}

fn Parser::parse_keyword_function_definition() throws -> Command *
{
  /* The 'function' keyword was consumed by the caller, so the name follows. */
  Token *name_token = m_lexer.next_shell_token();
  ASSERT(name_token != nullptr);
  if (name_token->kind() != Token::Kind::Word) {
    throw ErrorWithLocation{name_token->source_location(),
                            "Expected a name after the 'function' keyword"};
  }
  const let location = name_token->source_location();
  const let name = name_token->raw_string();

  LOG(verbosity::Debug, "parsing a keyword function definition for '%s'",
      name.c_str());

  /* An empty () pair may follow the name in the bash function form, where the
     POSIX form requires it. */
  Token *after_name = m_lexer.peek_shell_token();
  ASSERT(after_name != nullptr);
  if (after_name->kind() == Token::Kind::LeftParen) {
    m_lexer.advance_past_last_peek();
    Token *close = m_lexer.next_shell_token();
    ASSERT(close != nullptr);
    if (close->kind() != Token::Kind::RightParen) {
      throw ErrorWithLocation{close->source_location(),
                              "Expected ')' in a function definition"};
    }
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

  /* The definition's span ends where its body ends, recorded so declare -f
     can print the definition text from the source. */
  let definition =
      m_lexer.arena().create<FunctionDefinition>(location, name.view(), body);
  definition->set_source_end_position(body->source_end_position());
  return definition;
}

fn Parser::consume_bash_array_assignment() throws -> ArrayList<const Token *>
{
  Token *open = m_lexer.next_shell_token();
  ASSERT(open != nullptr);
  ASSERT(open->kind() == Token::Kind::LeftParen);

  /* The elements are arbitrary words and may nest further parens, so the depth
     counter tracks the matching close rather than the first one. Every token
     inside the outermost pair is kept so bash mode can expand them as the array
     elements, while POSIX mode discards the list. */
  ArrayList<const Token *> elements{};
  usize depth = 1;
  for (;;) {
    Token *t = m_lexer.next_shell_token();
    ASSERT(t != nullptr);
    if (t->kind() == Token::Kind::EndOfFile) {
      throw ErrorWithLocation{open->source_location(),
                              "Unterminated array assignment, expected ')'"};
    }
    if (t->kind() == Token::Kind::LeftParen) {
      depth++;
      elements.push(t);
    } else if (t->kind() == Token::Kind::RightParen) {
      depth--;
      if (depth == 0) break;
      elements.push(t);
    } else {
      if (t->kind() != Token::Kind::Newline) elements.push(t);
    }
  }
  return elements;
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
            utils::int_to_text(static_cast<i64>(MAX_RECURSION_DEPTH))};
  }

  Expression *lhs = nullptr;

  /* Handle leaf type. We expect either a value, or an unary operator. */
  switch (t->kind()) {
  case Token::Kind::Number: {
    const let parsed = utils::parse_decimal_integer(t->raw_string());
    if (parsed.is_error())
      throw ErrorWithLocation{t->source_location(), parsed.error().message()};
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
          after->source_location(), "Expected 'Fi' here"};
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
              utils::int_to_text(static_cast<i64>(MAX_RECURSION_DEPTH))};
    }

    m_parentheses_depth++;
    lhs = parse_expression();
    m_parentheses_depth--;

    Token *rp = m_lexer.next_expression_token();
    ASSERT(rp != nullptr);
    if (rp->kind() != Token::Kind::RightParen) {
      throw ErrorWithLocationAndDetails{
          t->source_location(), "Unterminated parenthesis",
          rp->source_location(), "Expected a closing parenthesis here"};
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
