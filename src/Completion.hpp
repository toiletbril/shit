#pragma once

#include "Common.hpp"
#include "Eval.hpp"
#include "Path.hpp"
#include "String.hpp"
#include "StringMap.hpp"
#include "StringView.hpp"

namespace shit {

namespace completion {

struct completion_result
{
  ArrayList<String> candidates;
  /* Keyed by the candidate text so it survives the candidate sort. Empty for a
     filesystem or command-name completion. */
  StringMap<String> descriptions{heap_allocator()};
  String longest_common_prefix;
  usize token_start;
  usize token_end;
  /* Argument position completes against the filesystem instead. */
  bool is_command_position;
};

fn complete(StringView line, usize cursor, EvalContext &context,
            const Path &base_directory, bool for_listing = false) throws
    -> completion_result;

fn complete_command_names(StringView token, bool token_is_glob,
                          EvalContext &context) throws -> ArrayList<String>;

/* The spans come back sorted by start and non-overlapping. */
struct highlight_span
{
  usize start;
  usize end;
  StringView sgr;
};

fn highlight_line(StringView line, EvalContext &context) throws
    -> ArrayList<highlight_span>;

/* The verdicts are cached per word and the cache drops when PATH changes. */
fn command_word_resolves(StringView line, EvalContext &context) throws -> bool;

} /* namespace completion */

} /* namespace shit */
