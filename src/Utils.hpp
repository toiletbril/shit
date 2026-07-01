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

/* The index of the first suffix equal to wanted, or None when none match. */
fn find_pos_in_vec(const ArrayList<String> &suffixes,
                   StringView wanted) wontthrow -> Maybe<usize>;

/* Expand a leading ~ or ~user prefix in a path to the home directory, the
   lightweight form the analysis prepass and the highlighter share. None when
   the path has no leading tilde or the named user has no home. */
fn expand_leading_tilde_path(StringView name) throws -> Maybe<String>;

/* Quote an empty or control-byte-carrying value into the '' or $'...' ANSI-C
   form the way printf %q and ${var@Q} share. Returns false when the value has
   no control byte, so the caller applies its own non-control quoting. */
fn append_ansi_c_quote_if_needed(String &out, StringView arg) throws -> bool;

fn execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async) throws
    -> i32;

fn execute_contexts_with_pipes(ArrayList<ExecContext> &&ecs, EvalContext &cxt,
                               bool is_async) throws -> i32;

/* The name with a leading SIG dropped, the spelling the signal tables key on.
   A name without the prefix is returned unchanged. */
pure fn strip_sig_prefix(StringView name) wontthrow -> StringView;

/* Split text into its newline-delimited lines, each line without its trailing
   newline. A trailing newline yields a final empty line. */
fn split_lines(StringView text) throws -> ArrayList<StringView>;

/* Format a Unix timestamp as local time through strftime. Empty when the time
   cannot be broken down. */
fn format_unix_timestamp(i64 unix_time, const char *format) throws -> String;

/* Whether the word is one of the POSIX shell reserved words. It matches dash's
   set rather than the lexer's, so a shell-specific token such as time is
   excluded. */
pure fn is_posix_reserved_word(StringView word) wontthrow -> bool;

/* Parse a whole view as a signed integer. Each base has its own function so the
   digit loop carries no base branch. Leading and trailing ASCII whitespace and
   one optional sign are allowed, the value saturates to the i64 range on
   overflow, and any other content yields an Error. */
fn parse_decimal_integer(StringView text) throws -> ErrorOr<i64>;

fn parse_timeout_seconds_to_nanos(StringView text) throws -> ErrorOr<i64>;

/* Write the decimal text of value into the caller's buffer, which must hold at
   least twenty-one bytes, and return a view of the written span. No allocation
   happens, so a hot conversion never touches the heap. */
fn int_to_text_into(i64 value, char *buffer, usize buffer_size) wontthrow
    -> StringView;

/* Format a count of seconds as the whole minutes and fractional seconds form
   the time and times builtins print, such as 0m0.123s. */
fn format_minutes_seconds(double seconds) throws -> String;

/* The two reports the time keyword prints. The POSIX form matches bash time -p.
   The pretty form is an aligned block of the wall time, the user and system
   cpu, the cpu busy percent, and the peak resident set when peak_rss_bytes is
   non-zero. */
fn format_time_report_posix(double real_seconds, double user_seconds,
                            double system_seconds) throws -> String;
fn format_time_report_pretty(double real_seconds, double user_seconds,
                             double system_seconds, u64 peak_rss_bytes) throws
    -> String;

/* The report a set TIMEFORMAT renders. The bash conversions are honored, %%, a
   literal percent, %[p][l]R, %[p][l]U, and %[p][l]S for the real, user, and
   system seconds, and %P for the cpu busy percent, where p is a precision from
   zero to six and l selects the minutes form. A trailing newline is appended.
 */
fn format_time_report_custom(StringView format, double real_seconds,
                             double user_seconds, double system_seconds) throws
    -> String;

/* The 1-based line number the byte at position falls on in source. The lookup
   is a binary search over a newline offset table cached on the source pointer
   and length, so a script that reads $LINENO on almost every line stays
   O(log n) per read. The cache holds one source at a time. */
fn line_number_at(StringView source, usize position) throws -> usize;

/* Drop the cached newline table that line_number_at keeps, when the host frees
   a retained source, so a later source at the same address with the same length
   does not read a stale table. */
fn invalidate_line_number_cache() wontthrow -> void;
fn parse_octal_integer(StringView text) throws -> ErrorOr<i64>;
fn parse_hexadecimal_integer(StringView text) throws -> ErrorOr<i64>;
fn parse_integer_in_base(StringView text, int_base base) throws -> ErrorOr<i64>;

/* The command name closest to name among the local names passed in, the
   builtins, and the PATH programs, within a couple of edits, for a did-you-mean
   hint. None when nothing is close enough. */
fn suggest_command(StringView name, const ArrayList<String> &local_names) throws
    -> Maybe<String>;

/* The current git branch read from .git/HEAD without forking git, walking up
   from the working directory to the filesystem root. Empty outside a
   repository. A detached HEAD reads as the short commit hash. */
fn current_git_branch() throws -> String;

/* Read everything still available on standard input into a string. */
fn read_entire_standard_input() throws -> String;

/* Read one line from a descriptor, without the trailing newline. Returns None
   at end of input with no bytes read. was_delimiter_terminated reports whether
   the delimiter ended the line. The delimiter defaults to a newline, and read
   -d passes the first byte of its argument, or a NUL for an empty argument. */
fn read_line_from_fd(os::descriptor fd, bool &was_delimiter_terminated,
                     char delimiter = '\n', i64 timeout_nanos = -1,
                     bool *was_timed_out = nullptr) throws -> Maybe<String>;

fn initialize_path_map() throws -> void;

fn clear_path_map() throws -> void;

/* Mark every cached PATH resolution stale so the next lookup re-resolves from
   the filesystem. A cd, a PATH assignment, and hash -r call this. */
fn invalidate_path_cache() throws -> void;

/* Point program resolution at a PATH value, the shell variable store's PATH
   rather than the process environment, so a plain PATH=... assignment that
   never exports still drives the search. None restores the search to the
   process environment's PATH. */
fn set_path_for_resolution(Maybe<String> path) throws -> void;

/* Searches PATH for program binary. The first resolved location is cached under
   the name and a later hit returns it without a stat until the cache is
   invalidated. With find_all the search skips the cache and returns every
   match, for which -a. */
fn search_program_path(StringView program_name, bool find_all = false) throws
    -> ArrayList<Path>;

fn path_command_names() throws -> const ArrayList<String> &;

/* Match str against glob, reading glob_active for which bytes act as
   metacharacters. With extglob set the bash extended-glob groups ?(..), *(..),
   +(..), @(..), and !(..) are recognized, otherwise they are plain bytes. */
fn glob_matches(StringView glob, StringView str,
                const ArrayList<bool> &glob_active, usize mask_offset,
                bool extglob = false) throws -> bool;

/* Hand quit the one context it reads the interactive state and the memory
   report flag from. quit gates the goodbye message on the interactive state and
   gates the memory report on the --show-memory flag the context carries. */
fn set_quit_context(const EvalContext *context) wontthrow -> void;

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] fn quit(i32 code, bool should_goodbye = false) throws -> void;

} // namespace utils

} // namespace shit
