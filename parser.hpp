#pragma once

#include "common.hpp"
#include "types.hpp"

#include <algorithm>
#include <string>
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
  return ch >= '%' && ch <= '-';
}

enum class TokenType
{
  Number,
  Plus,
  Minus,
  Asterisk,
  Slash,
  Percent,
  RightParen,
  LeftParen,
  EndOfFile,
};

struct Token
{
  Token(TokenType type) : m_type(type) {}

  TokenType
  type()
  {
    return m_type;
  };

protected:
  TokenType m_type;
};

struct Number : public Token
{
  Number(std::string_view sv) : Token(TokenType::Number), m_source_string(sv) {}

  std::string_view
  source_string()
  {
    return m_source_string;
  }

private:
  std::string_view m_source_string;
};

struct Plus : public Token
{
  Plus() : Token(TokenType::Plus) {}
};

struct Minus : public Token
{
  Minus() : Token(TokenType::Minus) {}
};

struct Slash : public Token
{
  Slash() : Token(TokenType::Slash) {}
};

struct Asterisk : public Token
{
  Asterisk() : Token(TokenType::Asterisk) {}
};

struct Percent : public Token
{
  Percent() : Token(TokenType::Percent) {}
};

struct LeftParen : public Token
{
  LeftParen() : Token(TokenType::LeftParen) {}
};

struct RightParen : public Token
{
  RightParen() : Token(TokenType::RightParen) {}
};

struct EndOfFile : public Token
{
  EndOfFile() : Token(TokenType::RightParen) {}
};

struct Lexer
{
  Lexer(std::string source) : m_source(source), m_cursor_position(0) {}

  Token *
  peek() const
  {
    auto [t, _] = lex_next();
    return t;
  }

  Token *
  next_token()
  {
    auto [t, offset] = lex_next();
    this->m_cursor_position += offset;
    return t;
  }

private:
  std::string m_source;
  usize       m_cursor_position{0};

  std::tuple<Token *, usize>
  lex_next() const
  {
    while (m_cursor_position < m_source.length()) {
      uchar ch = m_source[m_cursor_position];
      if (is_whitespace(ch))
        continue;
      else if (is_number(ch))
        return lex_number();
      else if (is_operator(ch))
        return lex_operator();
      else
        throw "Unknown symbol!";
    }
    EndOfFile *eof = new EndOfFile{};
    return {eof, m_cursor_position};
  }

  std::tuple<Token *, usize>
  lex_number() const
  {
    std::string buffer{};
    usize       cl = m_cursor_position;

    while (cl < m_source.length() && is_number(m_source[cl]))
      cl++;

    auto *n = new Number{m_source.substr(m_cursor_position, cl)};
    return {n, cl - m_cursor_position};
  }

  std::tuple<Token *, usize>
  lex_operator() const
  {
    std::string buffer{};
    usize       cl = m_cursor_position;

    while (cl < m_source.length() && is_operator(m_source[cl]))
      cl++;

    Token *t;
    if (buffer == "+")
      t = new Plus{};
    else if (buffer == "-")
      t = new Minus{};
    else if (buffer == "*")
      t = new Asterisk{};
    else if (buffer == "/")
      t = new Slash{};
    else if (buffer == "%")
      t = new Percent{};
    else if (buffer == ")")
      t = new RightParen{};
    else if (buffer == ")")
      t = new LeftParen{};
    else
      throw "Unknown operator!";

    return {t, cl - m_cursor_position};
  }
};
