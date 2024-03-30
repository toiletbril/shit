#pragma once

#include "common.hpp"
#include "error.hpp"
#include "expr.hpp"
#include "token.hpp"
#include "types.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

static forceinline bool
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

static forceinline bool
is_number(uchar ch)
{
  return ch >= '0' && ch <= '9';
}

static forceinline bool
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
    return true;
  default:
    return false;
  };
}

struct Lexer
{
  Lexer(std::string source) : m_source(source), m_cursor_position(0) {}
  ~Lexer() {}

  std::unique_ptr<Token>
  peek()
  {
    auto [t, offset] = lex_next();
    if (m_error)
      return nullptr;
    m_cached_token  = t;
    m_cached_offset = offset;
    return std::unique_ptr<Token>{t};
  }

  std::unique_ptr<Token>
  next_token()
  {
    if (false && m_cached_token) {
      Token *t = m_cached_token;
      m_cursor_position += m_cached_offset;

      m_cached_token  = nullptr;
      m_cached_offset = 0;

      return std::unique_ptr<Token>{t};
    }
    auto [t, offset] = lex_next();
    if (m_error)
      return nullptr;
    m_cursor_position += offset;
    return std::unique_ptr<Token>{t};
  }

  std::string_view
  source() const
  {
    return m_source;
  }

  Error *
  error()
  {
    return m_error;
  }

protected:
  std::string m_source{};
  usize       m_cursor_position{0};

  Token *m_cached_token{};
  usize  m_cached_offset{0};

  Error *m_error{};

  std::tuple<Token *, usize>
  lex_next()
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
        s += "Unknown symbol '";
        s += static_cast<char>(ch);
        s += "'";
        m_error = new ParserError{token_start, m_source, s};
        return {nullptr, 0};
      }
    }
    EndOfFile *eof = new EndOfFile{token_start};
    return {eof, token_start};
  }

  std::tuple<Token *, usize>
  lex_number(usize token_start)
  {
    usize token_end = token_start;

    while (token_end < m_source.length() && is_number(m_source[token_end]))
      token_end++;

    Number *num = new Number{
        token_start, m_source.substr(token_start, token_end - token_start)};
    return {num, token_end - m_cursor_position};
  }

  std::tuple<Token *, usize>
  lex_operator(usize token_start)
  {
    usize token_end = token_start + 1;
    uchar ch        = m_source[token_start];

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
    else if (ch == '&') {
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
    } else {
      std::string s;
      s += "Unknown operator '";
      s += static_cast<char>(ch);
      s += "'";
      m_error = new ParserError{token_start, m_source, s};
      return {nullptr, 0};
    }

    return {t, token_end - m_cursor_position};
  }
};
