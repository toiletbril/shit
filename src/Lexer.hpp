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
bool is_expandable_char(char ch);

} /* namespace lexer */

/* Dumb note: Main idea is that none of the routines except
 * advance_past_last_peek(), skip_whitespace() and advance_forward() move
 * internal cursor. */
struct Lexer
{
  Lexer(std::string source);
  ~Lexer();

  [[nodiscard]] Token *peek_expression_token();
  [[nodiscard]] Token *peek_shell_token();
  [[nodiscard]] Token *next_expression_token();
  [[nodiscard]] Token *next_shell_token();

  std::string_view source() const;
  usize            advance_past_last_peek();

protected:
  std::string m_source{};
  usize       m_cursor_position{0};
  usize       m_cached_offset{0};

  Token *lex_expression_token();
  Token *lex_shell_token();

  void  skip_whitespace();
  usize advance_forward(usize offset);
  char  chop_character(usize offset = 0);

  Token *lex_number();
  Token *lex_identifier();
  Token *lex_sentinel();
};

} /* namespace shit */
