#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Eval.hpp"
#include "NameValueArg.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Tokens.hpp"

namespace shit {

namespace utils {

fn merge_tokens_to_string(const ArrayList<const Token *> &tokens) throws
    -> String;

inline fn merge_args_to_string(const ArrayList<String> &args) throws -> String
{
  let result = String{heap_allocator()};
  for (usize i = 0; i < args.count(); i++) {
    result.append(args[i].view());
    if (i + 1 < args.count()) {
      result.push(' ');
    }
  }
  return result;
}

fn expand_leading_tilde_path(StringView name) throws -> Maybe<String>;

/* Returns false when the value has no control byte, so the caller applies its
   own non-control quoting. */
fn append_ansi_c_quote_if_needed(String &out, StringView arg) throws -> bool;

fn decode_ansi_c_escapes(String &out, StringView body) throws -> void;

fn execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async) throws
    -> i32;

fn execute_contexts_with_pipes(ArrayList<ExecContext> &&ecs, EvalContext &cxt,
                               bool is_async) throws -> i32;

pure fn strip_sig_prefix(StringView name) wontthrow -> StringView;

pure forceinline fn ascii_to_lower(char ch) wontthrow -> char
{
  if (ch >= 'A' && ch <= 'Z') return static_cast<char>(ch - 'A' + 'a');
  return ch;
}

fn split_lines(StringView text) throws -> ArrayList<StringView>;

fn format_unix_timestamp(i64 unix_time, const char *format) throws -> String;

/* It matches dash's set, not the lexer's, so a shell-specific token such as
   time is excluded. */
pure fn is_posix_reserved_word(StringView word) wontthrow -> bool;

/* The value saturates to the i64 range on overflow, and any other content
   yields an Error. */
fn parse_decimal_i64(StringView text, bool *out_of_range = nullptr) throws
    -> ErrorOr<i64>;
fn parse_decimal_u64(StringView text) throws -> ErrorOr<u64>;

fn parse_decimal_f64(const String &text) throws -> ErrorOr<f64>;

fn format_f64(f64 value, Allocator allocator) throws -> String;

fn parse_timeout_seconds_to_nanos(StringView text) throws -> ErrorOr<i64>;

/* The caller's buffer must hold at least twenty-one bytes. */
fn int_to_text_into(i64 value, char *buffer, usize buffer_size) wontthrow
    -> StringView;

fn format_minutes_seconds(double seconds) throws -> String;

fn format_time_report_posix(double real_seconds, double user_seconds,
                            double system_seconds) throws -> String;
fn format_time_report_pretty(double real_seconds, double user_seconds,
                             double system_seconds, u64 peak_rss_bytes) throws
    -> String;

/* The bash conversions are honored, %%, a literal percent, %[p][l]R, %[p][l]U,
   and %[p][l]S for the real, user, and system seconds, and %P for the cpu busy
   percent, where p is a precision from zero to six and l selects the minutes
   form. */
fn format_time_report_custom(StringView format, double real_seconds,
                             double user_seconds, double system_seconds) throws
    -> String;

/* The 1-based line number the byte at position falls on. The newline table is
   cached on the source pointer and length, holding one source at a time. */
fn line_number_at(StringView source, usize position) throws -> usize;

/* Dropped when the host frees a retained source, so a later source at the same
   address with the same length does not read a stale table. */
fn invalidate_line_number_cache() wontthrow -> void;
fn parse_integer_in_base(StringView text, int_base base,
                         bool *out_of_range = nullptr) throws -> ErrorOr<i64>;
fn parse_integer_in_base_u64(StringView text, int_base base) throws
    -> ErrorOr<u64>;

fn suggest_command(StringView name, const ArrayList<String> &local_names) throws
    -> Maybe<String>;

/* The current git branch read from .git/HEAD without forking git, walking up
   from the working directory to the filesystem root. Empty outside a
   repository. A detached HEAD reads as the short commit hash. */
fn current_git_branch() throws -> String;

fn read_entire_standard_input() throws -> String;

/* Returns None at end of input with no bytes read. The delimiter defaults to a
   newline, and read -d passes the first byte of its argument, or a NUL for an
   empty argument. */
fn read_line_from_fd(os::descriptor fd, bool &was_delimiter_terminated,
                     char delimiter = '\n', u64 deadline_nanos = 0,
                     bool *was_timed_out = nullptr,
                     Allocator allocator = heap_allocator()) throws
    -> Maybe<String>;

fn read_directory_cached(const Path &directory,
                         bool should_invalidate_path_cache = true) throws
    -> const ArrayList<Path::directory_child> *;
fn directory_entry_kind(const Path &directory,
                        const Path::directory_child &entry) throws
    -> Path::entry_kind;

fn initialize_path_map() throws -> void;

fn clear_path_map() throws -> void;

fn invalidate_path_cache() throws -> void;

/* None restores the search to the process environment's PATH. */
fn set_path_for_resolution(Maybe<String> path) throws -> void;

/* The first resolved location is cached under the name until the cache is
   invalidated. With find_all the search skips the cache and returns every
   match, for which -a. */
fn search_program_path(StringView program_name, bool find_all = false) throws
    -> ArrayList<Path>;

fn path_command_names() throws -> const ArrayList<String> &;

fn path_command_name_has_prefix(StringView prefix) throws -> bool;

enum class program_path_status : u8
{
  Missing,
  Blocked,
  Runnable,
};

fn get_program_path_status(StringView name) throws -> program_path_status;

/* glob_active reads which bytes act as metacharacters. With extglob set the
   bash extended-glob groups ?(..), *(..), +(..), @(..), and !(..) are
   recognized, otherwise they are plain bytes. */
fn glob_matches(StringView glob, StringView str, const Bitset &glob_active,
                usize mask_offset, bool extglob = false) throws -> bool;

fn set_quit_context(const EvalContext *context) wontthrow -> void;

[[noreturn]] fn quit(i32 code, bool should_goodbye = false) throws -> void;

} /* namespace utils */

} /* namespace shit */
