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

/* Parse a whole view as a signed integer, the StringView-native replacement for
   std::stoll. Each base has its own function so the digit loop carries no base
   branch and the compiler keeps the divisor a constant. Leading and trailing
   ASCII whitespace and one optional sign are allowed, the value saturates to
   the i64 range on overflow, and any other content yields an Error. The
   hexadecimal form also accepts a leading 0x. */
fn parse_decimal_integer(StringView text) throws -> ErrorOr<i64>;

/* Format a signed integer as decimal into a fresh String, the StringView-native
   replacement for std::to_string. The unsigned form is for ids and sizes that
   exceed the i64 range. */
fn int_to_text(i64 value) throws -> String;
fn uint_to_text(u64 value) throws -> String;
fn parse_octal_integer(StringView text) throws -> ErrorOr<i64>;
fn parse_hexadecimal_integer(StringView text) throws -> ErrorOr<i64>;

fn canonicalize_path(StringView path) throws -> Maybe<Path>;

/* Read a whole file into a string through the os descriptor layer, so no
   iostream file stream is pulled in. Returns None when the open fails. */
fn read_entire_file(StringView path) throws -> Maybe<String>;

/* Read everything still available on standard input into a string. */
fn read_entire_standard_input() throws -> String;

/* Read one line from a descriptor, without the trailing newline. Returns
   None at end of input with no bytes read. The read builtin passes the
   command's input descriptor so a redirection or a heredoc is honored. */
fn read_line_from_fd(os::descriptor fd) throws -> Maybe<String>;

fn initialize_path_map() throws -> void;

fn clear_path_map() throws -> void;

/* Searches PATH for program binary. Returns absolute paths to the program. */
fn search_program_path(StringView program_name) throws -> ArrayList<Path>;

fn glob_matches(StringView glob, StringView str,
                const ArrayList<bool> &glob_active, usize mask_offset) throws
    -> bool;

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] fn quit(i32 code, bool should_goodbye = false) throws -> void;

} /* namespace utils */

} /* namespace shit */
