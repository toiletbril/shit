#pragma once

#include "Common.hpp"
#include "Tokens.hpp"

#include <string>
#include <string_view>

namespace shit {

namespace lexer {

bool is_whitespace(char ch);

bool is_number(char ch);

bool is_expression_sentinel(char ch);

bool is_shell_sentinel(char ch);

bool is_part_of_identifier(char ch);

bool is_string_quote(char ch);

} /* namespace lexer */

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
  Token *chop_sentinel(usize token_start);
};

} /* namespace shit */
