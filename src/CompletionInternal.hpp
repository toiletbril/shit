#pragma once

#include "Completion.hpp"
#include "Containers.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

namespace completion {

/* One listed child, its name and whether it is a directory. The directory flag
   is resolved once when the listing is read, from the dirent type the read
   already knew, so the per-keystroke completion never stats an entry to learn
   whether to append a trailing slash. */
struct cached_directory_entry
{
  String name{};
  bool is_directory{false};
};

static pure forceinline fn is_blank(char byte) throws -> bool
{
  return byte == ' ' || byte == '\t';
}

static pure forceinline fn skip_blanks(StringView text, usize from) throws
    -> usize
{
  while (from < text.length && is_blank(text[from]))
    from++;
  return from;
}

static pure forceinline fn trim_blanks(StringView text) throws -> StringView
{
  let const start = skip_blanks(text, 0);
  let end = text.length;
  while (end > start && is_blank(text[end - 1]))
    end--;
  return text.substring_of_length(start, end - start);
}

/* Primitives defined in Completion.cpp and reached from the cascade stages and
   the highlighter. */
fn read_directory_cached(const Path &directory) throws
    -> const ArrayList<cached_directory_entry> *;
fn environment_path_changed(String &cached_path) throws -> bool;
fn command_word_of(StringView line) wontthrow -> StringView;
pure fn token_has_glob_metacharacter(StringView token) wontthrow -> bool;
fn resolve_completion_alias(StringView command, EvalContext &context) throws
    -> String;
fn resolve_completion_command(StringView command, EvalContext &context) throws
    -> String;
fn split_completion_words(StringView line, usize cursor, usize &cword) throws
    -> ArrayList<String>;

/* Defined in CompletionManpage.cpp. */
fn second_word_of(StringView line) wontthrow -> Maybe<StringView>;
fn complete_from_man_subcommands(StringView line, StringView token,
                                 usize token_start, bool for_listing,
                                 EvalContext &context) throws
    -> Maybe<ArrayList<String>>;
fn complete_from_manpage(StringView line, StringView token, bool for_listing,
                         EvalContext &context,
                         StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>;
fn complete_from_help(StringView line, StringView token, usize token_start,
                      bool for_listing, EvalContext &context,
                      StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>;
fn complete_from_help_subcommands(StringView line, StringView token,
                                  usize token_start, bool for_listing,
                                  EvalContext &context,
                                  StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>;

/* Defined in CompletionScan.cpp. */
fn complete_from_build_tools(StringView line, StringView token,
                             usize token_start, bool for_listing,
                             EvalContext &context) throws
    -> Maybe<ArrayList<String>>;
fn complete_from_builtin_flags(StringView line, StringView token,
                               usize token_start, EvalContext &context) throws
    -> Maybe<ArrayList<String>>;
fn complete_from_spec(StringView line, StringView token, usize cursor,
                      bool for_listing, EvalContext &context,
                      StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>;
fn command_substitution_body_start(StringView line, usize cursor) throws
    -> usize;

} // namespace completion

} // namespace shit
