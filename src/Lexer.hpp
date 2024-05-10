#pragma once

#include "Common.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Tokens.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace shit {

struct Lexer
{
  Lexer(std::string source);
  ~Lexer();

  [[nodiscard]] Token *peek_expression_token();
  [[nodiscard]] Token *peek_shell_token();
  usize                advance_past_last_peek();
  [[nodiscard]] Token *next_expression_token();
  [[nodiscard]] Token *next_shell_token();
  std::string_view     source() const;

protected:
  std::string m_source{};
  usize       m_cursor_position{0};
  usize       m_cached_offset{0};

  void skip_whitespace();

  Token *lex_expression_token();
  Token *lex_shell_token();

  Token *chop_number(usize token_start);
  Token *chop_identifier(usize token_start);
  Token *chop_string(usize token_start, char quote_char);
  Token *chop_expression_sentinel(usize token_start);
  Token *chop_shell_sentinel(usize token_start);
};

} /* namespace shit */
