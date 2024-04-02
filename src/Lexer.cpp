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
  case '\t':
    return true;
  default:
    return false;
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
  case '!':
    return true;
  default:
    return false;
  };
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

Error
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
    else {
      std::string s;
      s += "Unexpected character";
      m_error = Error{token_start, s};
      m_cached_offset = 0;
      return nullptr;
    }
  }

  m_cached_offset = 0;
  return new EndOfFile{token_start};
}

Token *
Lexer::lex_number(usize token_start)
{
  usize token_end = token_start;

  while (token_end < m_source.length() && is_number(m_source[token_end]))
    token_end++;

  Number *num = new Number{
      token_start, m_source.substr(token_start, token_end - token_start)};
  m_cached_offset = token_end - m_cursor_position;

  return num;
}

Token *
Lexer::lex_operator(usize token_start)
{
  usize token_end = token_start + 1;
  uchar ch = m_source[token_start];

  Token *t{};

  if (ch == '+')
    t = new Plus{token_start};
  else if (ch == '-')
    t = new Minus{token_start};
  else if (ch == '*')
    t = new Asterisk{token_start};
  else if (ch == '/')
    t = new Slash{token_start};
  else if (ch == '%')
    t = new Percent{token_start};
  else if (ch == ')')
    t = new RightParen{token_start};
  else if (ch == '(')
    t = new LeftParen{token_start};
  else if (ch == '~')
    t = new Tilde{token_start};
  else if (ch == '^')
    t = new Cap{token_start};
  else if (ch == '!') {
    if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new ExclamationEquals{token_start};
      token_end++;
    } else {
      t = new ExclamationMark{token_start};
    }
  } else if (ch == '&') {
    if (token_end < m_source.length() && m_source[token_end] == '&') {
      t = new DoubleAmpersand{token_start};
      token_end++;
    } else {
      t = new Ampersand{token_start};
    }
  } else if (ch == '>') {
    if (token_end < m_source.length() && m_source[token_end] == '>') {
      t = new DoubleGreater{token_start};
      token_end++;
    } else if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new GreaterEquals{token_start};
      token_end++;
    } else {
      t = new Greater{token_start};
    }
  } else if (ch == '<') {
    if (token_end < m_source.length() && m_source[token_end] == '<') {
      t = new DoubleLess{token_start};
      token_end++;
    } else if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new LessEquals{token_start};
      token_end++;
    } else {
      t = new Less{token_start};
    }
  } else if (ch == '|') {
    if (token_end < m_source.length() && m_source[token_end] == '|') {
      t = new DoublePipe{token_start};
      token_end++;
    } else {
      t = new Pipe{token_start};
    }
  } else if (ch == '=') {
    if (token_end < m_source.length() && m_source[token_end] == '=') {
      t = new DoubleEquals{token_start};
      token_end++;
    } else {
      t = new Equals{token_start};
    }
  } else {
    std::string s;
    s += "Unknown operator '";
    s += static_cast<char>(ch);
    s += "'";
    m_error = Error{token_start, s};
    t = nullptr;
  }

  m_cached_offset = token_end - m_cursor_position;
  return t;
}
