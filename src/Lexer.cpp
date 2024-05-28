#include "Lexer.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Tokens.hpp"

#include <string>

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
  return (ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122);
}

} /* namespace lexer */

Lexer::Lexer(std::string source) : m_source(source), m_cursor_position(0) {}

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
  while (m_cursor_position < m_source.length()) {
    if (!lexer::is_whitespace(m_source[m_cursor_position]))
      return;
    m_cursor_position++;
  }
}

Token *
Lexer::lex_expression_token()
{
  usize token_start = m_cursor_position;

  if (m_cursor_position < m_source.length()) {
    char ch = m_source[m_cursor_position];

    if (lexer::is_number(ch))
      return chop_number(token_start);
    else if (lexer::is_expression_sentinel(ch))
      return chop_expression_sentinel(token_start);
    else if (lexer::is_string_quote(ch))
      return chop_string(token_start + 1, ch);
    else if (lexer::is_ascii_char(ch))
      return chop_identifier(token_start);
    else {
      std::string s;
      s += "Unexpected character";
      throw ErrorWithLocation{token_start, s};
    }
  }

  return new TokenEndOfFile{token_start};
}

Token *
Lexer::chop_number(usize token_start)
{
  usize token_end = token_start;

  while (token_end < m_source.length() && lexer::is_number(m_source[token_end]))
    token_end++;

  Token *num = new TokenNumber{
      token_start, m_source.substr(token_start, token_end - token_start)};
  m_cached_offset = token_end - m_cursor_position;

  return num;
}

Token *
Lexer::chop_identifier(usize token_start)
{
  usize token_end = token_start;

  while (token_end < m_source.length() &&
         lexer::is_part_of_identifier(m_source[token_end]))
    token_end++;

  std::string ident_string =
      m_source.substr(token_start, token_end - token_start);

  std::string lower_ident_string;
  for (const char c : ident_string)
    lower_ident_string += std::tolower(c);

  Token *t{};

  /* An identifier may be a keyword. */
  if (auto kw = KEYWORDS.find(lower_ident_string); kw != KEYWORDS.end()) {
    switch (kw->second) {
    case Token::Kind::If: t = new TokenIf{token_start}; break;
    case Token::Kind::Then: t = new TokenThen{token_start}; break;
    case Token::Kind::Else: t = new TokenElse{token_start}; break;
    case Token::Kind::Fi: t = new TokenFi{token_start}; break;
    default: SHIT_UNREACHABLE("Unhandled keyword of type %d", kw->second);
    }
  } else {
    t = new TokenIdentifier{token_start, ident_string};
  }

  m_cached_offset = token_end - m_cursor_position;

  return t;
}

Token *
Lexer::lex_shell_token()
{
  usize token_start = m_cursor_position;

  if (m_cursor_position < m_source.length()) {
    char ch = m_source[m_cursor_position];

    if (lexer::is_string_quote(ch))
      return chop_string(token_start + 1, ch);
    else if (lexer::is_shell_sentinel(ch))
      return chop_shell_sentinel(token_start);
    else if (lexer::is_part_of_identifier(ch))
      return chop_identifier(token_start);
    else {
      std::string s;
      s += "Unexpected character";
      throw ErrorWithLocation{token_start, s};
    }
  }

  return new TokenEndOfFile{token_start};
}

Token *
Lexer::chop_shell_sentinel(usize token_start)
{
  throw ErrorWithLocation{token_start, "Not implemented"};
}

Token *
Lexer::chop_string(usize token_start, char quote_char)
{
  usize token_end = token_start;

  while (m_source[token_end] != quote_char) {
    token_end++;
    if (token_end > m_source.length()) {
      throw ErrorWithLocation{token_start - 1, "Unterminated string literal"};
    }
  }

  TokenString *str = new TokenString{
      token_start, m_source.substr(token_start, token_end - token_start)};
  m_cached_offset = token_end - m_cursor_position + 1;

  return str;
}

static const std::unordered_map<uchar, Token::Kind> OPERATORS = {
  /* Sentinels */
    {')', Token::Kind::RightParen     },
    {'(', Token::Kind::LeftParen      },
    {';', Token::Kind::Semicolon      },
    {'.', Token::Kind::Dot            },

 /* Operators */
    {'+', Token::Kind::Plus           },
    {'-', Token::Kind::Minus          },
    {'*', Token::Kind::Asterisk       },
    {'/', Token::Kind::Slash          },
    {'%', Token::Kind::Percent        },
    {'~', Token::Kind::Tilde          },
    {'^', Token::Kind::Cap            },
    {'!', Token::Kind::ExclamationMark},
    {'&', Token::Kind::Ampersand      },
    {'>', Token::Kind::Greater        },
    {'<', Token::Kind::Less           },
    {'|', Token::Kind::Pipe           },
    {'=', Token::Kind::Equals         },
};

Token *
Lexer::chop_expression_sentinel(usize token_start)
{
  usize token_end = token_start + 1;
  uchar ch = static_cast<uchar>(m_source[token_start]);

  Token *t{};

  if (auto op = OPERATORS.find(ch); op != OPERATORS.end()) {
    switch (op->second) {
      /* clang-format off */
    case Token::Kind::RightParen: t = new TokenRightParen{token_start}; break;
    case Token::Kind::LeftParen:  t = new TokenLeftParen{token_start}; break;
    case Token::Kind::Semicolon:  t = new TokenSemicolon{token_start}; break;
    case Token::Kind::Dot:        t = new TokenDot{token_start}; break;

    case Token::Kind::Plus:       t = new TokenPlus{token_start}; break;
    case Token::Kind::Minus:      t = new TokenMinus{token_start}; break;
    case Token::Kind::Asterisk:   t = new TokenAsterisk{token_start}; break;
    case Token::Kind::Slash:      t = new TokenSlash{token_start}; break;
    case Token::Kind::Percent:    t = new TokenPercent{token_start}; break;
    case Token::Kind::Tilde:      t = new TokenTilde{token_start}; break;
    case Token::Kind::Cap:        t = new TokenCap{token_start}; break;
      /* clang-format on */

    case Token::Kind::ExclamationMark: {
      if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenExclamationEquals{token_start};
        token_end++;
      } else {
        t = new TokenExclamationMark{token_start};
      }
    } break;

    case Token::Kind::Ampersand: {
      if (token_end < m_source.length() && m_source[token_end] == '&') {
        t = new TokenDoubleAmpersand{token_start};
        token_end++;
      } else {
        t = new TokenAmpersand{token_start};
      }
    } break;

    case Token::Kind::Greater: {
      if (token_end < m_source.length() && m_source[token_end] == '>') {
        t = new TokenDoubleGreater{token_start};
        token_end++;
      } else if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenGreaterEquals{token_start};
        token_end++;
      } else {
        t = new TokenGreater{token_start};
      }
    } break;

    case Token::Kind::Less: {
      if (token_end < m_source.length() && m_source[token_end] == '<') {
        t = new TokenDoubleLess{token_start};
        token_end++;
      } else if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenLessEquals{token_start};
        token_end++;
      } else {
        t = new TokenLess{token_start};
      }
    } break;

    case Token::Kind::Pipe: {
      if (token_end < m_source.length() && m_source[token_end] == '|') {
        t = new TokenDoublePipe{token_start};
        token_end++;
      } else {
        t = new TokenPipe{token_start};
      }
    } break;

    case Token::Kind::Equals: {
      if (token_end < m_source.length() && m_source[token_end] == '=') {
        t = new TokenDoubleEquals{token_start};
        token_end++;
      } else {
        t = new TokenEquals{token_start};
      }
    } break;

    default: SHIT_UNREACHABLE("Unhandled operator of type %d", op->second);
    }
  } else {
    std::string s;
    s += "Unknown operator '";
    s += static_cast<char>(ch);
    s += "'";
    throw ErrorWithLocation{token_start, s};
  }

  m_cached_offset = token_end - m_cursor_position;

  return t;
}

} /* namespace shit */
