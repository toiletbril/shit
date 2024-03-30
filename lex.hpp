#pragma once
#include "common.hpp"
#include "error.hpp"
#include "expr.hpp"
#include "types.hpp"

#include <algorithm>
#include <iostream>
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
  return ch >= '%' && ch <= '-';
}

enum class TokenType
{
  Invalid,
  EndOfFile,

  Number,
  Plus,
  Minus,
  Asterisk,
  Slash,
  Percent,
  RightParen,
  LeftParen,
};

struct Token;
static std::string token_to_str(Token *t);

struct Token
{
  Token(usize location) : m_source_position(location){};

  virtual TokenType type() = 0;

  std::string
  to_string()
  {
    return token_to_str(this);
  }

  usize
  location()
  {
    return m_source_position;
  }

  bool
  is_binary()
  {
    switch (type()) {
    case TokenType::Number:
    case TokenType::Invalid:
    case TokenType::EndOfFile:
      return false;
    default:
      return true;
    }
  }

protected:
  usize m_source_position{};
};

struct TokenOperator : public Token
{
  TokenOperator(usize source_position) : Token(source_position) {}
  virtual u8          precendence()                                      = 0;
  virtual Expression *construct_binary_expression(Expression *lhs,
                                                  Expression *rhs) const = 0;
};

struct Number : public Token
{
  Number(usize source_position, std::string_view sv)
      : Token(source_position), m_source_string(sv)
  {
  }

  TokenType
  type()
  {
    return TokenType::Number;
  }

  std::string_view
  source_string()
  {
    return m_source_string;
  }

protected:
  std::string m_source_string;
};

struct Plus : public TokenOperator
{
  Plus(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type()
  {
    return TokenType::Plus;
  }

  u8
  precendence()
  {
    return 2;
  }

  Expression *
  construct_binary_expression(Expression *lhs, Expression *rhs) const
  {
    return new Add{lhs, rhs};
  }
};

struct Minus : public TokenOperator
{
  Minus(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type()
  {
    return TokenType::Minus;
  }

  u8
  precendence()
  {
    return 2;
  }

  Expression *
  construct_binary_expression(Expression *lhs, Expression *rhs) const
  {
    return new Subtract{lhs, rhs};
  }
};

struct Slash : public TokenOperator
{
  Slash(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type()
  {
    return TokenType::Slash;
  }

  u8
  precendence()
  {
    return 3;
  }

  Expression *
  construct_binary_expression(Expression *lhs, Expression *rhs) const
  {
    return new Divide{lhs, rhs};
  }
};

struct Asterisk : public TokenOperator
{
  Asterisk(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type()
  {
    return TokenType::Asterisk;
  }

  u8
  precendence()
  {
    return 3;
  }

  Expression *
  construct_binary_expression(Expression *lhs, Expression *rhs) const
  {
    return new Multiply{lhs, rhs};
  }
};

struct Percent : public TokenOperator
{
  Percent(usize source_position) : TokenOperator(source_position) {}

  TokenType
  type()
  {
    return TokenType::Percent;
  }

  u8
  precendence()
  {
    return 1;
  }

  Expression *
  construct_binary_expression(Expression *lhs, Expression *rhs) const
  {
    return new Module{lhs, rhs};
  }
};

struct LeftParen : public Token
{
  LeftParen(usize source_position) : Token(source_position) {}

  TokenType
  type()
  {
    return TokenType::LeftParen;
  }
};

struct RightParen : public Token
{
  RightParen(usize source_position) : Token(source_position) {}

  TokenType
  type()
  {
    return TokenType::RightParen;
  }
};

struct EndOfFile : public Token
{
  EndOfFile(usize source_position) : Token(source_position) {}

  TokenType
  type()
  {
    return TokenType::EndOfFile;
  }

  u8
  precendence()
  {
    return 0;
  }
};

static std::string
token_to_str(Token *t)
{
  switch (t->type()) {
  case TokenType::Invalid:
    return "invalid";
  case TokenType::EndOfFile:
    return "end of file";
  case TokenType::Number: {
    std::string s = "number ";
    s += static_cast<Number *>(t)->source_string();
    return s;
  }
  case TokenType::Plus:
    return "plus";
  case TokenType::Minus:
    return "minus";
  case TokenType::Asterisk:
    return "asterisk";
  case TokenType::Slash:
    return "slash";
  case TokenType::Percent:
    return "percent";
  case TokenType::RightParen:
    return "right paren";
  case TokenType::LeftParen:
    return "left paren";
  default:
    __builtin_unreachable();
  }
}

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
    m_cursor_position += offset;
    std::cout << "lexed a token: " << t->to_string() << std::endl;
    return t;
  }

  std::string_view
  source()
  {
    return m_source;
  }

protected:
  std::string m_source{};
  usize       m_cursor_position{0};

  std::tuple<Token *, usize>
  lex_next() const
  {
    std::cout << "current position: " << std::to_string(m_cursor_position) << std::endl;
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
      else
        throw LexerError{token_start, m_source,
                         "Unknown token '" + std::to_string(ch) + "'"};
    }
    EndOfFile *eof = new EndOfFile{token_start};
    return {eof, token_start};
  }

  std::tuple<Token *, usize>
  lex_number(usize token_start) const
  {
    usize token_end = token_start;

    while (token_end < m_source.length() && is_number(m_source[token_end]))
      token_end++;

    Number *num = new Number{
        token_start, m_source.substr(token_start, token_end - token_start)};
    return {num, token_end - m_cursor_position};
  }

  std::tuple<Token *, usize>
  lex_operator(usize token_start) const
  {
    usize token_end = token_start;

    while (token_end < m_source.length() && is_operator(m_source[token_end]))
      token_end++;

    std::string buffer = m_source.substr(token_start, token_end - token_start);

    Token *t;
    if (buffer == "+")
      t = new Plus{token_start};
    else if (buffer == "-")
      t = new Minus{token_start};
    else if (buffer == "*")
      t = new Asterisk{token_start};
    else if (buffer == "/")
      t = new Slash{token_start};
    else if (buffer == "%")
      t = new Percent{token_start};
    else if (buffer == ")")
      t = new RightParen{token_start};
    else if (buffer == ")")
      t = new LeftParen{token_start};
    else
      throw LexerError{token_start, m_source,
                       "Unknown operator '" + buffer + "'"};

    return {t, token_end - m_cursor_position};
  }
};
