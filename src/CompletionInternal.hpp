#pragma once

#include "Allocator.hpp"
#include "Arena.hpp"
#include "Completion.hpp"
#include "Containers.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

namespace completion {

extern BumpArena COMPLETION_ARENA;

inline fn completion_allocator() wontthrow -> Allocator
{
  return bump_allocator(COMPLETION_ARENA);
}

struct cached_directory_entry
{
  String name{heap_allocator()};
  bool is_directory{false};
};

static pure forceinline fn is_blank(char byte) wontthrow -> bool
{
  return byte == ' ' || byte == '\t';
}

static pure forceinline fn skip_blanks(StringView text, usize from) wontthrow
    -> usize
{
  while (from < text.length && is_blank(text[from]))
    from++;
  return from;
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
fn complete_from_process_arguments(StringView line, StringView token,
                                   bool for_listing) throws
    -> Maybe<ArrayList<String>>;
fn complete_from_tools_with_targets(StringView line, StringView token,
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
