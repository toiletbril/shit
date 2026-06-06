#pragma once

#include "Common.hpp"
#include "Containers.hpp"
#include "String.hpp"
#include "StringView.hpp"
#include "Tokens.hpp"

#include <string>

namespace shit {

struct BumpArena;

/* A heredoc whose body is collected when the line that introduced it ends. The
   body buffer has a stable address, so a parsed redirection points at it and
   reads it once the lexer fills it. The body stays a std::string because the
   parsed Redirection field that points at it is a std::string pointer the Eval
   layer reads. */
struct HeredocPending
{
  String delimiter;
  bool strip_tabs;
  std::string *body;
};

namespace lexer {

fn is_whitespace(char ch) -> bool;
fn is_number(char ch) -> bool;
fn is_expression_sentinel(char ch) -> bool;
fn is_shell_sentinel(char ch) -> bool;
fn is_part_of_identifier(char ch) -> bool;
fn is_string_quote(char ch) -> bool;
fn is_expandable_char(char ch) -> bool;
fn is_variable_name_start(char ch) -> bool;
fn is_variable_name(char ch) -> bool;

} /* namespace lexer */

/* Dumb note: Main idea is that none of the routines except
 * advance_past_last_peek(), skip_whitespace() and advance_forward() move
 * internal cursor. */
struct Lexer
{
  Lexer(String source, BumpArena &arena,
        bool should_collect_debug_words = false,
        Maybe<StringView> filename = None);
  ~Lexer();

  /* The lexer owns the heap-allocated heredoc bodies, so a copy would double
     free them. Moving transfers the pointer array and leaves the source empty,
     and the copy is deleted so an accidental copy fails to compile. */
  Lexer(Lexer &&) = default;
  Lexer &operator=(Lexer &&) = default;
  Lexer(const Lexer &) = delete;
  Lexer &operator=(const Lexer &) = delete;

  [[nodiscard]] fn peek_expression_token() -> Token *;
  [[nodiscard]] fn peek_shell_token() -> Token *;
  [[nodiscard]] fn next_expression_token() -> Token *;
  [[nodiscard]] fn next_shell_token() -> Token *;

  fn source() const -> StringView;
  fn debug_words() const -> const ArrayList<Word> &;
  fn arena() const -> BumpArena &;
  /* Redirect node allocation to another arena, so a function body can be parsed
     into the persistent function arena and restored afterward. */
  fn set_arena(BumpArena &arena) -> void;
  fn advance_past_last_peek() -> usize;

  /* Reserve a heredoc body for the given delimiter, returning the stable buffer
     the lexer fills when the current line ends. The buffer is a std::string
     because the parsed Redirection field that points at it is read as one by
     the Eval layer. */
  fn register_heredoc(StringView delimiter, bool strip_tabs)
      -> const std::string *;

protected:
  /* Stamp a location in the source being lexed with this lexer's filename, so
     every token and error the lexer makes points a caret at the named file. */
  fn here(usize position, usize length) const -> SourceLocation
  {
    return SourceLocation{position, length, m_filename};
  }

  String m_source{};
  BumpArena *m_arena;
  /* The name of the file this source came from, or None for an unnamed source
     such as an interactive line. It travels into every SourceLocation the lexer
     stamps. */
  Maybe<StringView> m_filename{};
  usize m_cursor_position{0};
  usize m_cached_offset{0};

  /* The lexer keeps a copy of every word it produces only when the segment
     dump is requested, so the common path stays allocation free. A word is
     recorded by its start position, so peeking the same token twice, which the
     parser does to look ahead, records it only once. */
  bool m_should_collect_debug_words{false};
  ArrayList<Word> m_debug_words{heap_allocator()};
  usize m_last_collected_word_position{static_cast<usize>(-1)};

  /* Heredoc bodies are filled once the line ends, so the last shell token kind
     is tracked to detect that the consumed token was a newline. */
  bool m_last_shell_token_was_newline{false};
  /* Each body is heap-allocated so its address is stable. A parsed redirection
     holds a pointer into one, and the pointer must stay valid while the array
     of pointers grows. The lexer frees them in its destructor. */
  ArrayList<std::string *> m_heredoc_bodies{heap_allocator()};
  ArrayList<HeredocPending> m_pending_heredocs{heap_allocator()};
  fn collect_pending_heredocs() -> void;

  fn lex_expression_token() -> Token *;
  fn lex_shell_token() -> Token *;

  fn skip_whitespace() -> void;
  fn advance_forward(usize offset) -> usize;
  fn chop_character(usize offset = 0) -> char;

  fn lex_number() -> Token *;
  fn lex_identifier() -> Token *;
  fn lex_sentinel() -> Token *;
};

} /* namespace shit */
