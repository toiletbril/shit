#pragma once

#include "Common.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"

#include <memory>
#include <string>
#include <string_view>

struct Lexer
{
  Lexer(std::string source);
  ~Lexer();

  [[nodiscard]] Token *peek_token();
  usize                advance_past_peek();
  [[nodiscard]] Token *next_token();
  std::string_view     source() const;
  Error                error();

protected:
  std::string m_source{};
  usize       m_cursor_position{0};
  usize       m_cached_offset{0};
  Error       m_error{};

  Token *lex_next();
  Token *lex_number(usize token_start);
  Token *lex_operator(usize token_start);
};
