#include "Lexer.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Tokens.hpp"

#include <string>

/* Glob identifiers. */

namespace shit {

namespace lexer {

bool
is_whitespace(char ch)
{
  switch (ch) {
  case ' ':
  case '\v':
  case '\n':
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

bool
is_shell_sentinel(char ch)
{
  switch (ch) {
  case '|':
  case '&':
  case ';':
  case '>': return true;
  default: return false;
  };
}

bool
is_part_of_identifier(char ch)
{
  return !is_shell_sentinel(ch) && !is_whitespace(ch) && ch != EOF;
}

bool
is_string_quote(char ch)
{
  switch (ch) {
  case '"':
  case '\'':
  case '`': return true;
  default: return false;
  }
}

bool
is_ascii_char(char ch)
{
  return (ch >= 'A' && ch <= 'A') || (ch >= 'a' && ch <= 'z');
}

} /* namespace lexer */

Lexer::Lexer(std::string source) : m_source(std::move(source)) {}

Lexer::~Lexer() = default;

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

usize
Lexer::advance_past_last_peek()
{
  usize r = advance_forward(m_cached_offset);
  m_cached_offset = 0;
  return r;
}

Token *
Lexer::lex_expression_token()
{
  if (char ch = chop_character(); ch != EOF) {
    if (lexer::is_number(ch)) {
      return lex_number();
    } else if (lexer::is_expression_sentinel(ch)) {
      return lex_sentinel();
    } else if (lexer::is_string_quote(ch)) {
      return lex_string(ch);
    } else if (lexer::is_ascii_char(ch)) {
      return lex_identifier();
    } else {
      throw ErrorWithLocation{m_cursor_position, "Unexpected character"};
    }
  }

  return new TokenEndOfFile{m_cursor_position};
}

Token *
Lexer::lex_shell_token()
{
  if (char ch = chop_character(); ch != EOF) {
    if (lexer::is_string_quote(ch)) {
      return lex_string(ch);
    } else if (lexer::is_shell_sentinel(ch)) {
      return lex_sentinel();
    } else if (lexer::is_part_of_identifier(ch)) {
      return lex_identifier();
    } else {
      throw ErrorWithLocation{m_cursor_position, "Unexpected character"};
    }
  }

  return new TokenEndOfFile{m_cursor_position};
}

void
Lexer::skip_whitespace()
{
  usize i = 0;
  while (lexer::is_whitespace(chop_character(i++)))
    ;
  advance_forward((i > 0) ? i - 1 : 0);
}

usize
Lexer::advance_forward(usize offset)
{
  usize overflow = offset;

  /* Do we have something in expand buffer? Move it's cursor. */
  if (m_expand_buffer_cursor_position < m_expand_buffer.length()) {
    usize until_end =
        m_expand_buffer.length() - m_expand_buffer_cursor_position;

    /* Cached offset is greater than expand buffer's length? Then we need to
     * move the actual cursor too. */
    if (offset < until_end) {
      m_expand_buffer_cursor_position += offset;
      overflow = 0;
    } else {
      m_expand_buffer_cursor_position += until_end;
      overflow = offset - until_end;
    }
  }

  SHIT_ASSERT(m_cursor_position + overflow <= m_source.length());
  m_cursor_position += overflow;

  return offset;
}

char
Lexer::chop_character(usize offset)
{
  if (m_expand_buffer_cursor_position + offset < m_expand_buffer.length()) {
    return m_expand_buffer[m_expand_buffer_cursor_position + offset];
  } else if (m_cursor_position + offset - m_expand_buffer_cursor_position <
             m_source.length())
  {
    return m_source[m_cursor_position + offset];
  }

  return EOF;
}

Token *
Lexer::lex_number()
{
  char        ch;
  std::string n;
  usize       length = 0;

  while (lexer::is_number((ch = chop_character(length)))) {
    n += ch;
    length++;
  }

  Token *num = new TokenNumber{m_cursor_position, n};
  m_cached_offset = length;

  return num;
}

Token *
Lexer::lex_identifier()
{
  char        ch;
  std::string ident;
  usize       length = 0;

  while (lexer::is_part_of_identifier((ch = chop_character(length)))) {
    ident += ch;
    length++;
  }

  std::string lower_ident;
  for (const char c : ident) {
    lower_ident += std::tolower(c);
  }

  Token *t{};

  /* An identifier may be a keyword. */
  if (auto kw = KEYWORDS.find(lower_ident); kw != KEYWORDS.end()) {
    switch (kw->second) {
      /* clang-format off */
    case Token::Kind::If:   t = new TokenIf{m_cursor_position}; break;
    case Token::Kind::Then: t = new TokenThen{m_cursor_position}; break;
    case Token::Kind::Else: t = new TokenElse{m_cursor_position}; break;
    case Token::Kind::Fi:   t = new TokenFi{m_cursor_position}; break;
      /* clang-format on */
    default: SHIT_UNREACHABLE("Unhandled keyword of type %d", E(kw->second));
    }
  } else {
    t = new TokenIdentifier{m_cursor_position, ident};
  }

  m_cached_offset = length;

  return t;
}

Token *
Lexer::lex_string(char quote_char)
{
  char        ch;
  std::string str_str;
  /* Skip the first quote. */
  usize length = 1;

  while ((ch = chop_character(length)) != quote_char) {
    str_str += ch;
    length++;
  }

  if (m_cursor_position + length >= m_source.length()) {
    throw ErrorWithLocationAndDetails{
        m_cursor_position, "Unterminated string literal",
        m_cursor_position + length,
        "Expected " + std::string{quote_char} + " here"};
  }

  /* Skip the first and last quote here too. */
  TokenString *str = new TokenString{m_cursor_position, str_str};

  /* Account for the quote char. */
  m_cached_offset = length + 1;

  return str;
}

/* Only single-character operators are defined here. Further parsing is done in
 * related routines. */
static const std::unordered_map<char, Token::Kind> OPERATORS = {
    /* Sentinels */
    {')', Token::Kind::RightParen        },
    {'(', Token::Kind::LeftParen         },
    {']', Token::Kind::RightSquareBracket},
    {'[', Token::Kind::LeftSquareBracket },
    {'}', Token::Kind::RightBracket      },
    {'{', Token::Kind::LeftBracket       },

    {';', Token::Kind::Semicolon         },
    {'.', Token::Kind::Dot               },
    {'$', Token::Kind::Dollar            },

    /* Operators */
    {'+', Token::Kind::Plus              },
    {'-', Token::Kind::Minus             },
    {'*', Token::Kind::Asterisk          },
    {'/', Token::Kind::Slash             },
    {'%', Token::Kind::Percent           },
    {'~', Token::Kind::Tilde             },
    {'^', Token::Kind::Cap               },
    {'!', Token::Kind::ExclamationMark   },
    {'&', Token::Kind::Ampersand         },
    {'>', Token::Kind::Greater           },
    {'<', Token::Kind::Less              },
    {'|', Token::Kind::Pipe              },
    {'=', Token::Kind::Equals            },
};

Token *
Lexer::lex_sentinel()
{
  char  ch = chop_character();
  usize extra_length = 0;

  Token *t{};

  if (auto op = OPERATORS.find(ch); op != OPERATORS.end()) {
    switch (op->second) {
      /* clang-format off */
    case Token::Kind::RightParen:   t = new TokenRightParen{m_cursor_position}; break;
    case Token::Kind::LeftParen:    t = new TokenLeftParen{m_cursor_position}; break;
    case Token::Kind::RightBracket: t = new TokenRightBracket{m_cursor_position}; break;
    case Token::Kind::LeftBracket:  t = new TokenLeftBracket{m_cursor_position}; break;

    case Token::Kind::Semicolon:    t = new TokenSemicolon{m_cursor_position}; break;
    case Token::Kind::Dot:          t = new TokenDot{m_cursor_position}; break;
    case Token::Kind::Dollar:       t = new TokenDollar{m_cursor_position}; break;

    case Token::Kind::Plus:         t = new TokenPlus{m_cursor_position}; break;
    case Token::Kind::Minus:        t = new TokenMinus{m_cursor_position}; break;
    case Token::Kind::Asterisk:     t = new TokenAsterisk{m_cursor_position}; break;
    case Token::Kind::Slash:        t = new TokenSlash{m_cursor_position}; break;
    case Token::Kind::Percent:      t = new TokenPercent{m_cursor_position}; break;
    case Token::Kind::Tilde:        t = new TokenTilde{m_cursor_position}; break;
    case Token::Kind::Cap:          t = new TokenCap{m_cursor_position}; break;
      /* clang-format on */

    case Token::Kind::RightSquareBracket: {
      if (chop_character(1) == ']') {
        t = new TokenDoubleRightSquareBracket{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenRightSquareBracket{m_cursor_position};
      }
    } break;

    case Token::Kind::LeftSquareBracket: {
      if (chop_character(1) == '[') {
        t = new TokenDoubleLeftSquareBracket{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenLeftSquareBracket{m_cursor_position};
      }
    } break;

    case Token::Kind::ExclamationMark: {
      if (chop_character(1) == '=') {
        t = new TokenExclamationEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenExclamationMark{m_cursor_position};
      }
    } break;

    case Token::Kind::Ampersand: {
      if (chop_character(1) == '&') {
        t = new TokenDoubleAmpersand{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenAmpersand{m_cursor_position};
      }
    } break;

    case Token::Kind::Greater: {
      if (chop_character(1) == '>') {
        t = new TokenDoubleGreater{m_cursor_position};
        extra_length++;
      } else if (chop_character(1) == '=') {
        t = new TokenGreaterEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenGreater{m_cursor_position};
      }
    } break;

    case Token::Kind::Less: {
      if (chop_character(1) == '<') {
        t = new TokenDoubleLess{m_cursor_position};
        extra_length++;
      } else if (chop_character(1) == '=') {
        t = new TokenLessEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenLess{m_cursor_position};
      }
    } break;

    case Token::Kind::Pipe: {
      if (chop_character(1) == '|') {
        t = new TokenDoublePipe{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenPipe{m_cursor_position};
      }
    } break;

    case Token::Kind::Equals: {
      if (chop_character(1) == '=') {
        t = new TokenDoubleEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new TokenEquals{m_cursor_position};
      }
    } break;

    default: SHIT_UNREACHABLE("Unhandled operator of type %d", E(op->second));
    }
  } else {
    std::string s;
    s += "Unknown operator '";
    s += ch;
    s += "'";
    throw ErrorWithLocation{m_cursor_position, s};
  }

  m_cached_offset = 1 + extra_length;

  return t;
}

} /* namespace shit */
