#include "Lexer.hpp"

#include "Common.hpp"
#include "Tokens.hpp"

#include <cstring>
#include <memory>
#include <string>
#include <string_view>

static FORCEINLINE bool
is_whitespace(uchar ch)
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

static FORCEINLINE bool
is_number(uchar ch)
{
  return ch >= '0' && ch <= '9';
}

static FORCEINLINE bool
is_significant_sentinel(uchar ch)
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

static FORCEINLINE bool
is_string_quote(uchar ch)
{
  switch (ch) {
  case '"':
  case '\'':
  case '`': return true;
  default: return false;
  }
}

static FORCEINLINE bool
is_char(uchar ch)
{
  return (ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122);
}

static FORCEINLINE bool
is_identifier_char(uchar ch)
{
  if (is_char(ch) || is_number(ch))
    return true;
  switch (ch) {
  case '/':
  case '.':
  case '~':
  case '-':
  case '+':
  case '_': return true;
  default: return false;
  }
}

Lexer::Lexer(std::string source) : m_source(source), m_cursor_position(0) {}

Lexer::~Lexer() = default;

Token *
Lexer::peek_token()
{
  return lex_token();
}

usize
Lexer::advance_past_peek()
{
  usize r = m_cached_offset;
  m_cursor_position += r;
  m_cached_offset = 0;
  return r;
}

Token *
Lexer::next_token()
{
  Token *t = lex_token();
  advance_past_peek();
  return t;
}

Token *
Lexer::next_identifier()
{
  skip_whitespace();
  Token *t = lex_identifier(m_cursor_position);
  advance_past_peek();
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
    if (!is_whitespace(m_source[m_cursor_position]))
      return;
    m_cursor_position++;
  }
}

Token *
Lexer::lex_token()
{
  skip_whitespace();
  usize token_start = m_cursor_position;

  if (m_cursor_position < m_source.length()) {
    uchar ch = m_source[m_cursor_position];
    if (is_number(ch))
      return lex_number(token_start);
    else if (is_significant_sentinel(ch))
      return lex_operator_or_sentinel(token_start);
    else if (is_string_quote(ch))
      return lex_string(token_start + 1, ch);
    else if (is_char(ch)) /* Identifier can't start with a number. */
      return lex_identifier(token_start);
    else {
      std::string s;
      s += "Unexpected character";
      throw ErrorWithLocation{token_start, s};
    }
  }

  return new TokenEndOfFile{token_start};
}

Token *
Lexer::lex_number(usize token_start)
{
  usize token_end = token_start;

  while (token_end < m_source.length() && is_number(m_source[token_end]))
    token_end++;

  Token *num = new TokenNumber{
      token_start, m_source.substr(token_start, token_end - token_start)};
  m_cached_offset = token_end - m_cursor_position;

  return num;
}

static const std::unordered_map<std::string, TokenType> keywords = {
    {"if",   TokenType::If  },
    {"then", TokenType::Then},
    {"else", TokenType::Else},
    {"fi",   TokenType::Fi  },
};

Token *
Lexer::lex_identifier(usize token_start)
{
  usize token_end = token_start;

  while (token_end < m_source.length() &&
         is_identifier_char(m_source[token_end]))
    token_end++;

  std::string ident_string =
      m_source.substr(token_start, token_end - token_start);

  std::string lower_ident_string;
  for (const uchar c : ident_string)
    lower_ident_string += std::tolower(c);

  Token *t;

  /* An identifier may be a keyword. */
  if (auto kw = keywords.find(lower_ident_string); kw != keywords.end()) {
    switch (kw->second) {
    case TokenType::If: t = new TokenIf{token_start}; break;
    case TokenType::Then: t = new TokenThen{token_start}; break;
    case TokenType::Else: t = new TokenElse{token_start}; break;
    case TokenType::Fi: t = new TokenFi{token_start}; break;
    default: TRACELN("Unhandled keyword type: %d", kw->second); UNREACHABLE();
    }
  } else {
    t = new TokenIdentifier{token_start, ident_string};
  }

  m_cached_offset = token_end - m_cursor_position;

  return t;
}

Token *
Lexer::lex_identifier_until_whitespace(usize token_start)
{
  return lex_identifier(token_start);
}

Token *
Lexer::lex_string(usize token_start, uchar quote_char)
{
  usize token_end = token_start;

  while (m_source[token_end] != quote_char) {
    token_end++;
    if (token_end > m_source.length()) {
      throw ErrorWithLocation{token_start - 1, "Unterminated string literal"};
    }
  }

  Token *str = new TokenString{
      token_start, m_source.substr(token_start, token_end - token_start)};
  m_cached_offset = token_end - m_cursor_position + 1;

  return str;
}

Token *
Lexer::lex_operator_or_sentinel(usize token_start)
{
  usize token_end = token_start + 1;
  uchar ch = static_cast<uchar>(m_source[token_start]);

  Token *t{};

  if (ch == '+')
    t = new TokenPlus{token_start};
  else if (ch == '-')
    t = new TokenMinus{token_start};
  else if (ch == '*')
    t = new TokenAsterisk{token_start};
  else if (ch == '/')
    t = new TokenSlash{token_start};
  else if (ch == '%')
    t = new TokenPercent{token_start};
  else if (ch == ')')
    t = new TokenRightParen{token_start};
  else if (ch == '(')
    t = new TokenLeftParen{token_start};
  else if (ch == ';')
    t = new TokenSemicolon{token_start};
  else if (ch == '~')
    t = new TokenTilde{token_start};
  else if (ch == '^')
    t = new TokenCap{token_start};
  else if (ch == '.')
    t = new TokenDot{token_start};
  else if (ch == '!') {
    if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new TokenExclamationEquals{token_start};
      token_end++;
    } else {
      t = new TokenExclamationMark{token_start};
    }
  } else if (ch == '&') {
    if (token_end < m_source.length() && m_source[token_end] == '&') {
      t = new TokenDoubleAmpersand{token_start};
      token_end++;
    } else {
      t = new TokenAmpersand{token_start};
    }
  } else if (ch == '>') {
    if (token_end < m_source.length() && m_source[token_end] == '>') {
      t = new TokenDoubleGreater{token_start};
      token_end++;
    } else if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new TokenGreaterEquals{token_start};
      token_end++;
    } else {
      t = new TokenGreater{token_start};
    }
  } else if (ch == '<') {
    if (token_end < m_source.length() && m_source[token_end] == '<') {
      t = new TokenDoubleLess{token_start};
      token_end++;
    } else if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new TokenLessEquals{token_start};
      token_end++;
    } else {
      t = new TokenLess{token_start};
    }
  } else if (ch == '|') {
    if (token_end < m_source.length() && m_source[token_end] == '|') {
      t = new TokenDoublePipe{token_start};
      token_end++;
    } else {
      t = new TokenPipe{token_start};
    }
  } else if (ch == '=') {
    if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new TokenDoubleEquals{token_start};
      token_end++;
    } else {
      t = new TokenEquals{token_start};
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
