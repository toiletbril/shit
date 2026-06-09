#pragma once

#include "Common.hpp"
#include "Eval.hpp"
#include "Path.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

namespace completion {

/* The result of completing the token under the cursor. The candidates are the
   full replacement tokens, already sorted and deduped, and
   longest_common_prefix is the longest prefix every candidate shares. The line
   editor inserts the common prefix on the first TAB and lists the candidates on
   a second TAB. */
struct completion_result
{
  ArrayList<String> candidates;
  String longest_common_prefix;
  /* The byte offset in the input line where the token under the cursor begins,
     so the caller knows which span the replacement covers. */
  usize token_start;
  /* The byte offset just past the token under the cursor, the cursor position
     itself, so the caller replaces from token_start to token_end. */
  usize token_end;
  /* The token under the cursor was in command position, the first word of a
     command. Argument position completes against the filesystem instead. */
  bool is_command_position;
};

/* Complete the token under the cursor in the given line. The cursor is a byte
   offset into the line and marks the end of the token under it. The context
   supplies the function, alias, and PATH names for command-position
   completion. The base_directory roots a relative filesystem completion, so a
   test can point it at a fixed directory rather than the live working
   directory. The candidates carry the whole replacement token, with a trailing
   slash on a directory and the glob matches when the token holds a live glob
   metacharacter. */
fn complete(StringView line, usize cursor, EvalContext &context,
            const Path &base_directory) throws -> completion_result;

/* The first word of the line and whether it names a runnable command. The byte
   span runs from word_start to word_end, the command word the interactive
   highlighter colors. should_color is true when the word is a plain command
   name that resolves to nothing, a builtin, a function, an alias, a PATH
   executable, or, when it holds a slash, an existing executable path. A leading
   assignment, a reserved word, an opening parenthesis, or an empty line leave
   should_color false so the line stays plain. */
struct highlight_result
{
  usize word_start;
  usize word_end;
  bool should_color;
};

/* Decide whether the first word of the line should be colored as an
   unresolvable command. The context supplies the function, alias, and builtin
   sets and PATH drives the executable search, the same resolution the command
   evaluator performs. */
fn highlight_first_word(StringView line, EvalContext &context) throws
    -> highlight_result;

} /* namespace completion */

} /* namespace shit */
