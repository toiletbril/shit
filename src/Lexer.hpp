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

protected:
  std::string m_source{};
  usize       m_cursor_position{0};
  usize       m_cached_offset{0};

  Token *lex_next();
  Token *lex_number(usize token_start);
  Token *lex_identifier(usize token_start);
  Token *lex_string(usize token_start, uchar quote_char);
  Token *lex_operator_or_sentinel(usize token_start);
};
