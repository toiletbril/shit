#pragma once

#include "Common.hpp"
#include "Tokens.hpp"

#include <list>
#include <string>
#include <string_view>
#include <vector>

namespace shit {

struct BumpArena;

/* A heredoc whose body is collected when the line that introduced it ends. The
   body buffer has a stable address, so a parsed redirection points at it and
   reads it once the lexer fills it. */
struct HeredocPending
{
  std::string delimiter;
  bool strip_tabs;
  std::string *body;
};

namespace lexer {

bool is_whitespace(char ch);
bool is_number(char ch);
bool is_expression_sentinel(char ch);
bool is_shell_sentinel(char ch);
bool is_part_of_identifier(char ch);
bool is_string_quote(char ch);
bool is_expandable_char(char ch);
bool is_variable_name_start(char ch);
bool is_variable_name(char ch);

} /* namespace lexer */

/* Dumb note: Main idea is that none of the routines except
 * advance_past_last_peek(), skip_whitespace() and advance_forward() move
 * internal cursor. */
struct Lexer
{
  Lexer(std::string source, BumpArena &arena,
        bool should_collect_debug_words = false);
  ~Lexer();

  [[nodiscard]] Token *peek_expression_token();
  [[nodiscard]] Token *peek_shell_token();
  [[nodiscard]] Token *next_expression_token();
  [[nodiscard]] Token *next_shell_token();

  std::string_view source() const;
  const std::vector<Word> &debug_words() const;
  BumpArena &arena() const;
  /* Redirect node allocation to another arena, so a function body can be parsed
     into the persistent function arena and restored afterward. */
  void set_arena(BumpArena &arena);
  usize advance_past_last_peek();

  /* Reserve a heredoc body for the given delimiter, returning the stable buffer
     the lexer fills when the current line ends. */
  const std::string *register_heredoc(std::string delimiter, bool strip_tabs);

protected:
  std::string m_source{};
  BumpArena *m_arena;
  usize m_cursor_position{0};
  usize m_cached_offset{0};

  /* The lexer keeps a copy of every word it produces only when the segment
     dump is requested, so the common path stays allocation free. A word is
     recorded by its start position, so peeking the same token twice, which the
     parser does to look ahead, records it only once. */
  bool m_should_collect_debug_words{false};
  std::vector<Word> m_debug_words{};
  usize m_last_collected_word_position{static_cast<usize>(-1)};

  /* Heredoc bodies are filled once the line ends, so the last shell token kind
     is tracked to detect that the consumed token was a newline. */
  bool m_last_shell_token_was_newline{false};
  std::list<std::string> m_heredoc_bodies{};
  std::vector<HeredocPending> m_pending_heredocs{};
  void collect_pending_heredocs();

  Token *lex_expression_token();
  Token *lex_shell_token();

  void skip_whitespace();
  usize advance_forward(usize offset);
  char chop_character(usize offset = 0);

  Token *lex_number();
  Token *lex_identifier();
  Token *lex_sentinel();
};

} /* namespace shit */
