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
  return !is_shell_sentinel(ch) && !is_whitespace(ch);
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

usize
Lexer::advance_past_last_peek()
{
  usize r = m_cached_offset;
  m_cursor_position += r;
  m_cached_offset = 0;
  return r;
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

void
Lexer::skip_whitespace()
{
  while (m_cursor_position < m_source.length() &&
         lexer::is_whitespace(m_source[m_cursor_position]))
  {
    m_cursor_position++;
  }
}

Token *
Lexer::lex_expression_token()
{
  if (m_cursor_position < m_source.length()) {
    char ch = m_source[m_cursor_position];

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
Lexer::lex_number()
{
  usize length = 0;

  while (m_cursor_position + length < m_source.length() &&
         lexer::is_number(m_source[m_cursor_position + length]))
  {
    length++;
  }

  Token *num = new TokenNumber{m_cursor_position,
                               m_source.substr(m_cursor_position, length)};
  m_cached_offset = length;

  return num;
}

Token *
Lexer::lex_identifier()
{
  usize length = 0;

  while (m_cursor_position + length < m_source.length() &&
         lexer::is_part_of_identifier(m_source[m_cursor_position + length]))
  {
    length++;
  }

  std::string ident_string = m_source.substr(m_cursor_position, length);

  std::string lower_ident_string;
  for (const char c : ident_string) {
    lower_ident_string += std::tolower(c);
  }

  Token *t{};

  /* An identifier may be a keyword. */
  if (auto kw = KEYWORDS.find(lower_ident_string); kw != KEYWORDS.end()) {
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
    t = new TokenIdentifier{m_cursor_position, ident_string};
  }

  m_cached_offset = length;

  return t;
}

Token *
Lexer::lex_shell_token()
{
  if (m_cursor_position < m_source.length()) {
    char ch = m_source[m_cursor_position];

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

Token *
Lexer::lex_string(char quote_char)
{
  /* Skip the first quote. */
  usize length = 1;

  while (m_cursor_position + length < m_source.length() &&
         m_source[m_cursor_position + length] != quote_char)
  {
    length++;
  }

  if (m_cursor_position + length >= m_source.length()) {
    throw ErrorWithLocationAndDetails{
        m_cursor_position, "Unterminated string literal",
        m_cursor_position + length,
        "Expected " + std::string{quote_char} + " here"};
  }

  /* Skip the first and last quote here too. */
  TokenString *str = new TokenString{
      m_cursor_position, m_source.substr(m_cursor_position + 1, length - 1)};

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
  char  ch = m_source[m_cursor_position];
  usize token_end = m_cursor_position + 1;

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
      if (token_end < m_source.length() && m_source[token_end] == ']') {
        t = new TokenDoubleRightSquareBracket{m_cursor_position};
        token_end++;
      } else {
        t = new TokenRightSquareBracket{m_cursor_position};
      }
    } break;

    case Token::Kind::LeftSquareBracket: {
      if (token_end < m_source.length() && m_source[token_end] == '[') {
        t = new TokenDoubleLeftSquareBracket{m_cursor_position};
        token_end++;
      } else {
        t = new TokenLeftSquareBracket{m_cursor_position};
      }
    } break;

    case Token::Kind::ExclamationMark: {
      if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenExclamationEquals{m_cursor_position};
        token_end++;
      } else {
        t = new TokenExclamationMark{m_cursor_position};
      }
    } break;

    case Token::Kind::Ampersand: {
      if (token_end < m_source.length() && m_source[token_end] == '&') {
        t = new TokenDoubleAmpersand{m_cursor_position};
        token_end++;
      } else {
        t = new TokenAmpersand{m_cursor_position};
      }
    } break;

    case Token::Kind::Greater: {
      if (token_end < m_source.length() && m_source[token_end] == '>') {
        t = new TokenDoubleGreater{m_cursor_position};
        token_end++;
      } else if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenGreaterEquals{m_cursor_position};
        token_end++;
      } else {
        t = new TokenGreater{m_cursor_position};
      }
    } break;

    case Token::Kind::Less: {
      if (token_end < m_source.length() && m_source[token_end] == '<') {
        t = new TokenDoubleLess{m_cursor_position};
        token_end++;
      } else if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenLessEquals{m_cursor_position};
        token_end++;
      } else {
        t = new TokenLess{m_cursor_position};
      }
    } break;

    case Token::Kind::Pipe: {
      if (token_end < m_source.length() && m_source[token_end] == '|') {
        t = new TokenDoublePipe{m_cursor_position};
        token_end++;
      } else {
        t = new TokenPipe{m_cursor_position};
      }
    } break;

    case Token::Kind::Equals: {
      if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenDoubleEquals{m_cursor_position};
        token_end++;
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

  m_cached_offset = token_end - m_cursor_position;

  return t;
}

} /* namespace shit */
