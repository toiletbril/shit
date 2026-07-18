#pragma once

#include "Common.hpp"
#include "Eval.hpp"
#include "Path.hpp"
#include "String.hpp"
#include "StringMap.hpp"
#include "StringView.hpp"

namespace shit {

namespace completion {

enum class completion_mode : u8
{
  Ghost,
  Listing,
};

enum class command_match_mode : u8
{
  Prefix,
  Glob,
};

struct completion_result
{
  ArrayList<String> candidates;
  /* Keyed by the candidate text so it survives the candidate sort. Empty for a
     filesystem or command-name completion. */
  StringMap<String> descriptions{heap_allocator()};
  String longest_common_prefix;
  usize candidate_count;
  usize source_candidate_scan_count;
  usize materialized_candidate_count;
  usize token_start;
  usize token_end;
  /* Argument position completes against the filesystem instead. */
  bool is_command_position;
};

fn complete(StringView line, usize cursor, EvalContext &context,
            const Path &base_directory,
            completion_mode mode = completion_mode::Ghost) throws
    -> completion_result;

fn complete_command_names(StringView token, command_match_mode match_mode,
                          EvalContext &context) throws -> ArrayList<String>;
fn complete_filesystem_names(StringView token, EvalContext &context,
                             const Path &base_directory) throws
    -> ArrayList<String>;

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
