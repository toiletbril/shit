#pragma once

#include "Common.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <tuple>

struct Lexer
{
  Lexer(std::string source);

  ~Lexer();

  std::unique_ptr<Token> peek_token();

  std::unique_ptr<Token> next_token();

  std::string_view source() const;

  ErrorBase *error();

protected:
  std::string m_source{};
  usize       m_cursor_position{0};
  Token      *m_cached_token{};
  usize       m_cached_offset{0};
  ErrorBase  *m_error{};

  std::tuple<Token *, usize> lex_next();
  std::tuple<Token *, usize> lex_number(usize token_start);
  std::tuple<Token *, usize> lex_operator(usize token_start);
};
