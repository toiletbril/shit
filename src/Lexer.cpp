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
is_operator(uchar ch)
{
  switch (ch) {
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case ')':
  case '(':
  case '~':
  case '&':
  case '|':
  case '>':
  case '<':
  case '^':
  case '=':
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
is_idetifier_char(uchar ch)
{
  return !is_whitespace(ch) && !is_operator(ch) &&
         ((ch >= 65 && ch <= 90) || (ch >= 97 && ch <= 122));
}

Lexer::Lexer(std::string source) : m_source(source), m_cursor_position(0) {}

Lexer::~Lexer() = default;

Token *
Lexer::peek_token()
{
  Token *t = lex_next();
  if (m_error)
    return nullptr;
  return t;
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
  Token *t = lex_next();
  if (m_error)
    return nullptr;
  advance_past_peek();
  return t;
}

std::string_view
Lexer::source() const
{
  return m_source;
}

ErrorWithLocation
Lexer::error()
{
  return m_error;
}

Token *
Lexer::lex_next()
{
  usize token_start = m_cursor_position;

  while (token_start < m_source.length()) {
    uchar ch = m_source[token_start];
    if (is_whitespace(ch)) {
      token_start++;
      continue;
    }

    if (is_number(ch))
      return lex_number(token_start);
    else if (is_operator(ch))
      return lex_operator(token_start);
    else if (is_string_quote(ch))
      return lex_string(token_start + 1, ch);
    else if (is_idetifier_char(ch))
      return lex_identifier(token_start);
    else {
      std::string s;
      s += "Unexpected character";
      m_error = ErrorWithLocation{token_start, s};
      m_cached_offset = 0;
      return nullptr;
    }
  }

  m_cached_offset = 0;

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

Token *
Lexer::lex_identifier(usize token_start)
{
  usize token_end = token_start;

  while (token_end < m_source.length() &&
         is_idetifier_char(m_source[token_end]))
    token_end++;

  Token *ident = new TokenIdentifier{
      token_start, m_source.substr(token_start, token_end - token_start)};
  m_cached_offset = token_end - m_cursor_position;

  return ident;
}

Token *
Lexer::lex_string(usize token_start, uchar quote_char)
{
  usize token_end = token_start;

  while (m_source[token_end] != quote_char) {
    token_end++;
    if (token_end > m_source.length()) {
      m_error =
          ErrorWithLocation{token_start - 1, "Unterminated string literal"};
      return nullptr;
    }
  }

  Token *str = new TokenString{
      token_start, m_source.substr(token_start, token_end - token_start)};
  m_cached_offset = token_end - m_cursor_position + 1;

  return str;
}

Token *
Lexer::lex_operator(usize token_start)
{
  usize token_end = token_start + 1;
  uchar ch = m_source[token_start];

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
  else if (ch == '~')
    t = new TokenTilde{token_start};
  else if (ch == '^')
    t = new TokenCap{token_start};
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
    m_error = ErrorWithLocation{token_start, s};
    t = nullptr;
  }

  m_cached_offset = token_end - m_cursor_position;

  return t;
}
