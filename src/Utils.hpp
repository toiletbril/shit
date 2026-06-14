#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Eval.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Tokens.hpp"

namespace shit {

namespace utils {

/* Sort a list in place into ascending order by the element operator<, the
   in-house replacement for std::sort. The glob expansion depends on byte
   ascending order, so the comparison stays the element less-than and nothing
   reorders equal elements past each other. The implementation is an insertion
   sort, since the lists a shell sorts are short. */
template <class T>
fn sort_ascending(ArrayList<T> &list) throws -> void
{
  for (usize i = 1; i < list.count(); i++) {
    usize j = i;
    while (j > 0 && list[j] < list[j - 1]) {
      T temporary = steal(list[j]);
      list[j] = steal(list[j - 1]);
      list[j - 1] = steal(temporary);
      j--;
    }
  }
}

fn merge_tokens_to_string(const ArrayList<const Token *> &v) throws -> String;

/* Join the argument list into a single space-separated string. The container is
   the ArrayList<String> the exec-argv path now carries, so each element is
   appended through its byte view. */
inline fn merge_args_to_string(const ArrayList<String> &v) throws -> String
{
  String r{};
  for (usize i = 0; i < v.count(); i++) {
    r.append(v[i].view());
    if (i + 1 < v.count()) {
      r.push(' ');
    }
  }
  return r;
}

/* The index of the first matching suffix in the omitted-extension list, or
   NOT_FOUND_INDEX when none match. */
inline constexpr usize NOT_FOUND_INDEX = static_cast<usize>(-1);

fn find_pos_in_vec(const ArrayList<String> &suffixes,
                   StringView wanted) wontthrow -> usize;

fn execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async) throws
    -> i32;

fn execute_contexts_with_pipes(ArrayList<ExecContext> &&ecs, EvalContext &cxt,
                               bool is_async) throws -> i32;

fn string_replace(String &s, StringView to_replace,
                  StringView replace_with) throws -> void;

fn lowercase_string(StringView s) throws -> String;

/* Whether the word is one of the POSIX shell reserved words, the set the type
   and command builtins report as a shell keyword. It matches dash's set rather
   than the lexer's, so a shell-specific token such as time is excluded. */
pure fn is_posix_reserved_word(StringView word) wontthrow -> bool;

/* Parse a whole view as a signed integer, the StringView-native replacement for
   std::stoll. Each base has its own function so the digit loop carries no base
   branch and the compiler keeps the divisor a constant. Leading and trailing
   ASCII whitespace and one optional sign are allowed, the value saturates to
   the i64 range on overflow, and any other content yields an Error. The
   hexadecimal form also accepts a leading 0x. */
fn parse_decimal_integer(StringView text) throws -> ErrorOr<i64>;

/* An argument split at its first '=', for the assignment builtins. The value is
   absent when no '=' is present, so a bare name reads differently from name=.
 */
struct name_value_arg
{
  StringView name;
  Maybe<StringView> value;
};
pure fn split_name_value_arg(StringView arg) wontthrow -> name_value_arg;

/* Format a signed integer as decimal into a fresh String, the StringView-native
   replacement for std::to_string. The unsigned form is for ids and sizes that
   exceed the i64 range. */
/* The default allocator lives on the forward declaration in ErrorOr.hpp, so it
   is not repeated here. */
fn int_to_text(i64 value, Allocator allocator) throws -> String;
fn uint_to_text(u64 value, Allocator allocator = heap_allocator()) throws
    -> String;

/* Write the decimal text of value into the caller's buffer, which must hold at
   least twenty-one bytes, and return a view of the written span. No allocation
   happens, so a hot conversion such as an arithmetic assignment whose result
   the variable store copies for itself never touches the heap. */
fn int_to_text_into(i64 value, char *buffer, usize buffer_size) wontthrow
    -> StringView;

/* Format a count of seconds as the whole minutes and fractional seconds form
   the time and times builtins print, such as 0m0.123s. The seconds carry three
   fractional digits and the minutes are whole. */
fn format_minutes_seconds(double seconds) throws -> String;

/* The two reports the time keyword prints. The POSIX form matches bash time -p,
   each label and the plain seconds with two decimals on its own line. The
   pretty form is the default, an aligned block of the wall time, the user and
   system cpu, and the cpu busy percent, for a single timed run. */
fn format_time_report_posix(double real_seconds, double user_seconds,
                            double system_seconds) throws -> String;
fn format_time_report_pretty(double real_seconds, double user_seconds,
                             double system_seconds) throws -> String;

/* The 1-based line number the byte at position falls on in source, counting the
   newlines strictly before it. The lookup is a binary search over a newline
   offset table cached on the source pointer and length, so a script that reads
   $LINENO on almost every line stays O(log n) per read rather than O(n) over
   the prefix. The cache holds one source at a time, which fits the access
   pattern where a long script reads its own LINENO repeatedly. */
fn line_number_at(StringView source, usize position) throws -> usize;

/* Drop the cached newline table that line_number_at keeps. The host calls this
   when it frees a retained source, so a later source allocated at the same
   address with the same length does not read a stale table. */
fn invalidate_line_number_cache() wontthrow -> void;
fn parse_octal_integer(StringView text) throws -> ErrorOr<i64>;
fn parse_hexadecimal_integer(StringView text) throws -> ErrorOr<i64>;

fn canonicalize_path(StringView path) throws -> Maybe<Path>;

/* The command name closest to name among the local names passed in, the
   builtins, and the PATH programs, within a couple of edits counting an
   adjacent transposition as one, for a did-you-mean hint on a command that was
   not found. None when nothing is close enough. */
fn suggest_command(StringView name, const ArrayList<String> &local_names) throws
    -> Maybe<String>;

/* Read a whole file into a string through the os descriptor layer, so no
   iostream file stream is pulled in. Returns None when the open fails. */
fn read_entire_file(StringView path) throws -> Maybe<String>;

/* The current git branch read from .git/HEAD without forking git, walking up
   from the working directory to the filesystem root. Empty outside a
   repository. A detached HEAD reads as the short commit hash. Shared by the
   \G and \g prompt segments and the SHIT_GIT_BRANCH dynamic variable. */
fn current_git_branch() throws -> String;

/* The shell a script's shebang names, for the mimicry feature, or None when the
   resolved program is not a script shit can emulate. Only the first line is
   read. A sh or dash interpreter maps to POSIX mode, bash to bash mode, and
   shit to the default mode, including the /usr/bin/env form. */
fn detect_mimic_shell(const Path &program) throws -> Maybe<mimic_mood>;

/* Read everything still available on standard input into a string. */
fn read_entire_standard_input() throws -> String;

/* Read one line from a descriptor, without the trailing newline. Returns
   None at end of input with no bytes read. The read builtin passes the
   command's input descriptor so a redirection or a heredoc is honored.
   was_delimiter_terminated reports whether the delimiter ended the line, false
   when end of input ended it, so the read builtin returns a non-zero status for
   an unterminated final line. The delimiter defaults to a newline, and read -d
   passes the first byte of its argument, or a NUL byte for an empty argument so
   the input is slurped whole. */
fn read_line_from_fd(os::descriptor fd, bool &was_delimiter_terminated,
                     char delimiter = '\n') throws -> Maybe<String>;

fn initialize_path_map() throws -> void;

fn clear_path_map() throws -> void;

/* Mark every cached PATH resolution stale so the next lookup re-resolves from
   the filesystem. A cd, a PATH assignment, and hash -r call this, the way dash
   sets its rehash flag, so a hit never stats on the common path yet a shadowing
   directory or a reassigned PATH still wins on the next use. */
fn invalidate_path_cache() throws -> void;

/* Point program resolution at a PATH value, the shell variable store's PATH
   rather than the process environment, so a plain PATH=... assignment that
   never exports still drives the search the way dash resolves against the
   assigned value. None restores the search to the process environment's PATH.
   The cache is marked stale so the next lookup re-resolves against the new
   value. */
fn set_path_for_resolution(Maybe<String> path) throws -> void;

/* Searches PATH for program binary. Returns absolute paths to the program. The
   first resolved location is cached under the name and a later hit returns it
   without a stat until the cache is invalidated. With find_all the search skips
   the cache, scans every PATH directory, and returns every match, for which -a.
 */
fn search_program_path(StringView program_name, bool find_all = false) throws
    -> ArrayList<Path>;

/* Match str against glob, reading glob_active for which bytes act as
   metacharacters. With extglob set the bash extended-glob groups ?(..), *(..),
   +(..), @(..), and !(..) are recognized, otherwise they are plain bytes. */
fn glob_matches(StringView glob, StringView str,
                const ArrayList<bool> &glob_active, usize mask_offset,
                bool extglob = false) throws -> bool;

/* Hand quit the one context it reads the interactive state and the memory
   report flag from, set once from Main when the context exists. quit gates the
   goodbye message on the interactive state so it appears only at a prompt and
   never for a script, a -c, or a subshell, and gates the memory report on the
   --show-memory flag the context carries. */
fn set_quit_context(const EvalContext *context) wontthrow -> void;

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] fn quit(i32 code, bool should_goodbye = false) throws -> void;

} /* namespace utils */

} /* namespace shit */
