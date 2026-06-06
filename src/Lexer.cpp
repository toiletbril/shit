#include "Lexer.hpp"

#include "Arena.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <string>

/* TODO: Rewrite the lexer and parser to suit the shell language better. */
/* TODO: Cache the token for repeated peeks. */

namespace shit {

namespace lexer {

static constexpr char CEOF = static_cast<char>(EOF);

bool
is_whitespace(char ch)
{
  switch (ch) {
  case ' ':
  case '\v':
  case '\r':
  case '\t': return true;
  default: return false;
  }
}

bool
is_number(char ch)
{
  return ch >= '0' && ch <= '9';
}

bool
is_expression_sentinel(char ch)
{
  switch (ch) {
  case '\n':
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case ')':
  case '(':
  case ';':
  case '~':
  case '&':
  case '|':
  case '>':
  case '<':
  case '^':
  case '=':
  case '.':
  case '!': return true;
  default: return false;
  };
}

/* TODO: Separate redirections from here. */
bool
is_shell_sentinel(char ch)
{
  switch (ch) {
  case '\n':
  case '|':
  case '{':
  case '}':
  case '(':
  case ')':
  case '&':
  case ';':
  case '<':
  case '>': return true;
  default: return false;
  };
}

bool
is_part_of_identifier(char ch)
{
  return !is_shell_sentinel(ch) && !is_whitespace(ch) && ch != CEOF;
}

bool
is_string_quote(char ch)
{
  /* A backtick is intentionally not a quote here. It stays a literal character
     so the prepass can warn and point at $(...), rather than failing the lex.
   */
  switch (ch) {
  case '"':
  case '\'': return true;
  default: return false;
  }
}

bool
is_ascii_char(char ch)
{
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool
is_expandable_char(char ch)
{
  switch (ch) {
  case '[':
  case '?':
  case '*': return true;
  default: return false;
  }
}

bool
is_variable_name_start(char ch)
{
  return is_ascii_char(ch) || ch == '_';
}

bool
is_variable_name(char ch)
{
  return is_variable_name_start(ch) || is_number(ch);
}

} /* namespace lexer */

Lexer::Lexer(std::string source, BumpArena &arena,
             bool should_collect_debug_words)
    : m_source(std::move(source)), m_arena(&arena),
      m_should_collect_debug_words(should_collect_debug_words)
{}

Lexer::~Lexer()
{
  for (std::string *body : m_heredoc_bodies)
    delete body;
}

Token *
Lexer::peek_expression_token()
{
  skip_whitespace();
  return lex_expression_token();
}

Token *
Lexer::peek_shell_token()
{
  skip_whitespace();
  return lex_shell_token();
}

Token *
Lexer::next_expression_token()
{
  skip_whitespace();
  Token *t = lex_expression_token();
  advance_past_last_peek();
  return t;
}

Token *
Lexer::next_shell_token()
{
  skip_whitespace();
  Token *t = lex_shell_token();
  advance_past_last_peek();
  return t;
}

std::string_view
Lexer::source() const
{
  return m_source;
}

const ArrayList<Word> &
Lexer::debug_words() const
{
  return m_debug_words;
}

BumpArena &
Lexer::arena() const
{
  return *m_arena;
}

void
Lexer::set_arena(BumpArena &arena)
{
  m_arena = &arena;
}

usize
Lexer::advance_past_last_peek()
{
  usize r = advance_forward(m_cached_offset);
  m_cached_offset = 0;

  /* Consuming the newline that ends a line with a pending heredoc is where the
     body is collected, since the body sits on the following lines. */
  if (m_last_shell_token_was_newline && !m_pending_heredocs.empty()) {
    m_last_shell_token_was_newline = false;
    collect_pending_heredocs();
  }

  return r;
}

const std::string *
Lexer::register_heredoc(std::string delimiter, bool strip_tabs)
{
  std::string *body = new std::string{};
  m_heredoc_bodies.push(body);
  m_pending_heredocs.push({std::move(delimiter), strip_tabs, body});
  return body;
}

void
Lexer::collect_pending_heredocs()
{
  for (HeredocPending &pending : m_pending_heredocs) {
    std::string collected{};
    for (;;) {
      if (m_cursor_position >= m_source.length()) break;

      usize line_start = m_cursor_position;
      usize i = line_start;
      while (i < m_source.length() && m_source[i] != '\n')
        i++;
      bool has_newline = (i < m_source.length());
      m_cursor_position = has_newline ? i + 1 : i;

      std::string line = m_source.substr(line_start, i - line_start);
      if (pending.strip_tabs) {
        usize tabs = 0;
        while (tabs < line.length() && line[tabs] == '\t')
          tabs++;
        line.erase(0, tabs);
      }

      if (line == pending.delimiter) break;
      collected += line;
      collected += '\n';
    }
    *pending.body = std::move(collected);
  }
  m_pending_heredocs.clear();
}

Token *
Lexer::lex_expression_token()
{
  if (char ch = chop_character(); ch != lexer::CEOF) {
    if (lexer::is_number(ch))
      return lex_number();
    else if (lexer::is_expression_sentinel(ch))
      return lex_sentinel();
    else if (lexer::is_part_of_identifier(ch))
      return lex_identifier();
    else
      throw ErrorWithLocation{
          {m_cursor_position, 1},
          "Unexpected character"
      };
  }

  return m_arena->create<tokens::EndOfFile>(
      SourceLocation{m_cursor_position, 1});
}

Token *
Lexer::lex_shell_token()
{
  Token *t{};
  if (char ch = chop_character(); ch != lexer::CEOF) {
    if (lexer::is_shell_sentinel(ch))
      t = lex_sentinel();
    else if (lexer::is_part_of_identifier(ch))
      t = lex_identifier();
    else
      throw ErrorWithLocation{
          {m_cursor_position, 1},
          "Unexpected character"
      };
  } else {
    t = m_arena->create<tokens::EndOfFile>(
        SourceLocation{m_cursor_position, 1});
  }

  m_last_shell_token_was_newline = (t->kind() == Token::Kind::Newline);
  return t;
}

void
Lexer::skip_whitespace()
{
  usize i = 0;
  for (;;) {
    while (lexer::is_whitespace(chop_character(i)))
      i++;
    /* A '#' at a token boundary begins a comment that runs to the end of the
       line. The newline is left in place so it still terminates the command,
       and a leading '#!' shebang is just the first such comment. */
    if (chop_character(i) == '#') {
      while (chop_character(i) != '\n' && chop_character(i) != lexer::CEOF)
        i++;
      continue;
    }
    break;
  }
  advance_forward(i);
}

usize
Lexer::advance_forward(usize offset)
{
  SHIT_ASSERT(m_cursor_position + offset <= m_source.length());
  m_cursor_position += offset;
  return offset;
}

char
Lexer::chop_character(usize offset)
{
  if (m_cursor_position + offset < m_source.length())
    return m_source[m_cursor_position + offset];

  return lexer::CEOF;
}

Token *
Lexer::lex_number()
{
  char ch;
  std::string n{};
  usize length = 0;

  while (lexer::is_number((ch = chop_character(length)))) {
    n += ch;
    length++;
  }

  Token *num = m_arena->create<tokens::Number>(
      SourceLocation{m_cursor_position, length}, n);
  m_cached_offset = length;

  return num;
}

Token *
Lexer::lex_identifier()
{
  Word word{};

  usize byte_count = 0, escaped_newline_count = 0,
        relative_last_quote_char_pos = 0;

  bool should_escape = false;

  Maybe<char> quote_char;

  /* Append a character to the open segment, starting a new one when the kind
     changes. A variable reference never merges, since each one carries its own
     name. */
  auto append_char = [&word](WordSegment::Kind kind, char ch) {
    if (!word.segments.empty() && word.segments.back().kind == kind &&
        kind != WordSegment::Kind::VariableReference)
    {
      word.segments.back().text += ch;
    } else {
      word.segments.push(WordSegment{kind, std::string{ch}, false});
    }
  };

  for (;;) {
    char ch = chop_character(byte_count);

    bool is_inside_quote_or_escape = quote_char.has_value() || should_escape;
    if (!lexer::is_part_of_identifier(ch) &&
        !(is_inside_quote_or_escape && ch != lexer::CEOF))
    {
      break;
    }

    if (should_escape) {
      /* The previous character was a backslash. A backslash before a newline
         continues the line and leaves None behind. */
      should_escape = false;
      if (ch == '\n')
        escaped_newline_count++;
      else
        append_char(WordSegment::Kind::LiteralText, ch);
      byte_count++;
      continue;
    }

    if (quote_char == '\'') {
      /* Single quotes take everything literally up to the closing quote. */
      if (ch == '\'')
        quote_char.reset();
      else
        append_char(WordSegment::Kind::LiteralText, ch);
      byte_count++;
      continue;
    }

    if (ch == '\\') {
      /* Inside double quotes a backslash only escapes $, `, ", \, and a
         newline. Before any other character it stays a literal backslash, so
         "\n" is a backslash and an n, not an escape. Outside double quotes a
         backslash escapes the next character. */
      if (quote_char == '"') {
        char escaped_next = chop_character(byte_count + 1);
        if (escaped_next == '$' || escaped_next == '`' || escaped_next == '"' ||
            escaped_next == '\\' || escaped_next == '\n')
        {
          should_escape = true;
        } else {
          append_char(WordSegment::Kind::DoubleQuotedText, '\\');
        }
        byte_count++;
        continue;
      }
      should_escape = true;
      byte_count++;
      continue;
    }

    bool is_in_double_quotes = quote_char == '"';

    if (is_in_double_quotes && ch == '"') {
      quote_char.reset();
      byte_count++;
      continue;
    }

    if (!quote_char && lexer::is_string_quote(ch)) {
      relative_last_quote_char_pos = byte_count;
      quote_char = ch;
      byte_count++;
      continue;
    }

    if (ch == '$') {
      byte_count++;
      char next = chop_character(byte_count);

      if (next == '(') {
        /* Command substitution. Scan to the matching close paren, honoring
           quotes and nesting so an inner ) does not end it early. */
        byte_count++;

        /* $(( starts arithmetic expansion. Scan to the matching )), allowing
           grouping parens inside. A subshell substitution is written with a
           space, $( (cmd) ). */
        if (chop_character(byte_count) == '(') {
          byte_count++;
          std::string arithmetic{};
          usize group_depth = 0;
          for (;;) {
            char c = chop_character(byte_count);
            if (c == lexer::CEOF) {
              throw ErrorWithLocationAndDetails{
                  {m_cursor_position,              byte_count},
                  "Unterminated arithmetic expansion",
                  {m_cursor_position + byte_count, 1         },
                  "expected )) here"
              };
            }
            if (c == '(') {
              group_depth++;
              arithmetic += c;
              byte_count++;
            } else if (c == ')' && group_depth > 0) {
              group_depth--;
              arithmetic += c;
              byte_count++;
            } else if (c == ')' && chop_character(byte_count + 1) == ')') {
              byte_count += 2;
              break;
            } else {
              arithmetic += c;
              byte_count++;
            }
          }
          word.segments.push(WordSegment{WordSegment::Kind::ArithmeticExpansion,
                                         std::move(arithmetic),
                                         is_in_double_quotes});
          continue;
        }

        std::string inner{};
        usize depth = 1;
        char quote = 0;
        for (;;) {
          char c = chop_character(byte_count);
          if (c == lexer::CEOF) {
            throw ErrorWithLocationAndDetails{
                {m_cursor_position,              byte_count},
                "Unterminated command substitution",
                {m_cursor_position + byte_count, 1         },
                "expected ) here"
            };
          }
          byte_count++;

          if (quote != 0) {
            if (c == quote) quote = 0;
            inner += c;
            continue;
          }
          if (c == '\\') {
            inner += c;
            char escaped = chop_character(byte_count);
            if (escaped != lexer::CEOF) {
              byte_count++;
              inner += escaped;
            }
            continue;
          }
          if (c == '\'' || c == '"') {
            quote = c;
            inner += c;
            continue;
          }
          if (c == '(') {
            depth++;
          } else if (c == ')') {
            depth--;
            if (depth == 0) break;
          }
          inner += c;
        }
        word.segments.push(WordSegment{WordSegment::Kind::CommandSubstitution,
                                       std::move(inner), is_in_double_quotes});
      } else if (next == '{') {
        byte_count++;
        std::string name{};
        for (;;) {
          char c = chop_character(byte_count);
          if (c == lexer::CEOF) {
            throw ErrorWithLocationAndDetails{
                {m_cursor_position + byte_count, 1},
                "Unterminated variable expansion",
                {m_cursor_position + byte_count, 1},
                "expected } here"
            };
          }
          byte_count++;
          if (c == '}') break;
          name += c;
        }
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       std::move(name), is_in_double_quotes});
      } else if (lexer::is_variable_name_start(next)) {
        std::string name{};
        while (lexer::is_variable_name(next = chop_character(byte_count))) {
          name += next;
          byte_count++;
        }
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       std::move(name), is_in_double_quotes});
      } else if (next == '?' || next == '@' || next == '*' || next == '#' ||
                 next == '$' || next == '!' || next == '-' ||
                 lexer::is_number(next))
      {
        byte_count++;
        word.segments.push(WordSegment{WordSegment::Kind::VariableReference,
                                       std::string{next}, is_in_double_quotes});
      } else {
        /* A dollar sign that names None stays a literal dollar sign. */
        append_char(is_in_double_quotes ? WordSegment::Kind::DoubleQuotedText
                                        : WordSegment::Kind::UnquotedText,
                    '$');
      }
      continue;
    }

    append_char(is_in_double_quotes ? WordSegment::Kind::DoubleQuotedText
                                    : WordSegment::Kind::UnquotedText,
                ch);
    byte_count++;
  }

  if (quote_char) {
    throw ErrorWithLocationAndDetails{
        {m_cursor_position + relative_last_quote_char_pos,
         SHIT_SUB_SAT(byte_count, relative_last_quote_char_pos)},
        "Unterminated string literal",
        {m_cursor_position + byte_count, 1},
        "expected " + std::string{*quote_char}
        + " here"
    };
  }

  if (should_escape) {
    throw ErrorWithLocationAndDetails{
        {m_cursor_position + byte_count - 1, 1},
        "Nothing to escape",
        {m_cursor_position + byte_count,     1},
        "expected a character here"
    };
  }

  usize actual_cursor_position = m_cursor_position + escaped_newline_count;

  if (m_should_collect_debug_words &&
      m_cursor_position != m_last_collected_word_position)
  {
    m_debug_words.push(word);
    m_last_collected_word_position = m_cursor_position;
  }

  Token *t{};

  if (auto assignment_split = word.get_assignment_split();
      assignment_split.has_value())
  {
    t = m_arena->create<tokens::Assignment>(
        SourceLocation{actual_cursor_position, byte_count},
        assignment_split->first, std::move(assignment_split->second));
  } else if (word.segments.size() == 1 &&
             word.segments[0].kind == WordSegment::Kind::UnquotedText)
  {
    /* A bare word may name a keyword. A quoted or escaped word never does, so
       only a single unquoted segment qualifies. */
    const std::string &word_text = word.segments[0].text;
    if (Maybe<Token::Kind> kw =
            KEYWORDS.find(StringView{word_text.data(), word_text.size()}))
    {
      switch (*kw) {
        KW_SWITCH_CASES();
      default:
        SHIT_UNREACHABLE("unhandled keyword of type %d", SHIT_ENUM(*kw));
      }
    }
  }

  if (t == nullptr) {
    t = m_arena->create<tokens::WordToken>(
        SourceLocation{actual_cursor_position, byte_count}, std::move(word));
  }

  m_cached_offset = byte_count;

  return t;
}

/* Only single-character operators are defined here. Further parsing is done in
 * related routines. */

/* The token kind a single operator character begins, or None when the
   character is not an operator. The switch keeps this allocation free and the
   compiler lowers it to a jump table. */
static Maybe<Token::Kind>
lookup_operator(char ch)
{
  switch (ch) {
  case ')':  return Token::Kind::RightParen;
  case '(':  return Token::Kind::LeftParen;
  case ']':  return Token::Kind::RightSquareBracket;
  case '[':  return Token::Kind::LeftSquareBracket;
  case '}':  return Token::Kind::RightBracket;
  case '{':  return Token::Kind::LeftBracket;
  case ';':  return Token::Kind::Semicolon;
  case '.':  return Token::Kind::Dot;
  case '\n': return Token::Kind::Newline;
  case '+':  return Token::Kind::Plus;
  case '-':  return Token::Kind::Minus;
  case '*':  return Token::Kind::Asterisk;
  case '/':  return Token::Kind::Slash;
  case '%':  return Token::Kind::Percent;
  case '~':  return Token::Kind::Tilde;
  case '^':  return Token::Kind::Cap;
  case '!':  return Token::Kind::ExclamationMark;
  case '&':  return Token::Kind::Ampersand;
  case '>':  return Token::Kind::Greater;
  case '<':  return Token::Kind::Less;
  case '|':  return Token::Kind::Pipe;
  case '=':  return Token::Kind::Equals;
  default:   return None;
  }
}

Token *
Lexer::lex_sentinel()
{
  char ch = chop_character();
  usize extra_length = 0;

  Token *tok{};

  /* clang-format off */
#define TOKEN_CASE_ONE(t)                                                      \
  case Token::Kind::t:                                                         \
    tok = new tokens::t{                                                       \
        {m_cursor_position, 1}                                                 \
    };                                                                         \
    break;

#define TOKEN_CASE_TWO(t, ch, t2)                                              \
  case Token::Kind::t: {                                                       \
    if (chop_character(1) == ch) {                                             \
      tok = new tokens::t2{                                                    \
          {m_cursor_position, 2}                                               \
      };                                                                       \
      extra_length++;                                                          \
    } else {                                                                   \
      tok = new tokens::t{                                                     \
          {m_cursor_position, 1}                                               \
      };                                                                       \
    }                                                                          \
  } break;

#define TOKEN_CASE_THREE(t, ch2, t2, ch3, t3)                                  \
  case Token::Kind::t: {                                                       \
    if (chop_character(1) == ch2) {                                            \
      tok = new tokens::t2{                                                    \
          {m_cursor_position, 2}                                               \
      };                                                                       \
      extra_length++;                                                          \
    } else if (chop_character(1) == ch3) {                                     \
      tok = new tokens::t3{                                                    \
          {m_cursor_position, 2}                                               \
      };                                                                       \
      extra_length++;                                                          \
    } else {                                                                   \
      tok = new tokens::t{                                                     \
          {m_cursor_position, 1}                                               \
      };                                                                       \
    }                                                                          \
  } break;
  /* clang-format on */

  if (Maybe<Token::Kind> op = lookup_operator(ch)) {
    switch (*op) {
      TOKEN_CASE_ONE(RightParen);
      TOKEN_CASE_ONE(LeftParen);
      TOKEN_CASE_ONE(RightBracket);
      TOKEN_CASE_ONE(LeftBracket);
      TOKEN_CASE_TWO(Semicolon, ';', DoubleSemicolon);
      TOKEN_CASE_ONE(Dot);
      TOKEN_CASE_ONE(Newline);
      TOKEN_CASE_ONE(Plus);
      TOKEN_CASE_ONE(Minus);
      TOKEN_CASE_ONE(Asterisk);
      TOKEN_CASE_ONE(Slash);
      TOKEN_CASE_ONE(Percent);
      TOKEN_CASE_ONE(Tilde);
      TOKEN_CASE_ONE(Cap);

      TOKEN_CASE_TWO(RightSquareBracket, ']', DoubleRightSquareBracket);
      TOKEN_CASE_TWO(LeftSquareBracket, '[', DoubleLeftSquareBracket);
      TOKEN_CASE_TWO(ExclamationMark, '=', ExclamationEquals);
      TOKEN_CASE_TWO(Ampersand, '&', DoubleAmpersand);
      TOKEN_CASE_TWO(Pipe, '|', DoublePipe);
      TOKEN_CASE_TWO(Equals, '=', DoubleEquals);

      TOKEN_CASE_THREE(Greater, '>', DoubleGreater, '=', GreaterEquals);
      TOKEN_CASE_THREE(Less, '<', DoubleLess, '=', LessEquals);

    default:
      SHIT_UNREACHABLE("unhandled operator of type %d", SHIT_ENUM(*op));
    }
  } else {
    String s{};
    s += "unknown operator '";
    s += ch;
    s += "'";
    throw ErrorWithLocation{
        {m_cursor_position, extra_length},
        s
    };
  }

  m_cached_offset = 1 + extra_length;

  return tok;
}

} /* namespace shit */
