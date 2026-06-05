#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "ErrorOr.hpp"
#include "Eval.hpp"
#include "Os.hpp"
#include "Tokens.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shit {

namespace utils {

String merge_tokens_to_string(const ArrayList<const Token *> &v);

/* Join the argument list into a single space-separated string. The container is
   the ArrayList<String> the exec-argv path now carries, so each element is
   appended through its byte view. */
inline std::string
merge_args_to_string(const ArrayList<String> &v)
{
  std::string r{};
  for (usize i = 0; i < v.size(); i++) {
    const String &s = v[i];
    r.append(s.c_str(), s.size());
    if (i + 1 < v.size()) {
      r += ' ';
    }
  }
  return r;
}

template <class T>
usize
find_pos_in_vec(const std::vector<T> &v, const T &p)
{
  for (usize i = 0; i < v.size(); i++) {
    if (v[i] == p) {
      return i;
    }
  }
  return std::string::npos;
}

i32 execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async);

i32 execute_contexts_with_pipes(std::vector<ExecContext> &&ecs,
                                EvalContext &cxt, bool is_async);

void string_replace(std::string &s, StringView to_replace,
                    StringView replace_with);

String lowercase_string(StringView s);

/* Parse a whole view as a signed integer, the StringView-native replacement for
   std::stoll. Each base has its own function so the digit loop carries no base
   branch and the compiler keeps the divisor a constant. Leading and trailing
   ASCII whitespace and one optional sign are allowed, the value saturates to the
   i64 range on overflow, and any other content yields an Error. The hexadecimal
   form also accepts a leading 0x. */
ErrorOr<i64> parse_decimal_integer(StringView text);
ErrorOr<i64> parse_octal_integer(StringView text);
ErrorOr<i64> parse_hexadecimal_integer(StringView text);

Maybe<std::filesystem::path> canonicalize_path(const std::string &path);

/* Read a whole file into a string through the os descriptor layer, so no
   iostream file stream is pulled in. Returns None when the open fails. */
Maybe<std::string> read_entire_file(const std::string &path);

/* Read everything still available on standard input into a string. */
std::string read_entire_standard_input();

/* Read one line from a descriptor, without the trailing newline. Returns
   None at end of input with no bytes read. The read builtin passes the
   command's input descriptor so a redirection or a heredoc is honored. */
Maybe<std::string> read_line_from_fd(os::descriptor fd);

void initialize_path_map();

void clear_path_map();

/* Searches PATH for program binary. Returns absolute paths to the program. */
ArrayList<std::filesystem::path>
search_program_path(const std::string &program_name);

void set_current_directory(const std::filesystem::path &path);

std::filesystem::path get_current_directory();

bool glob_matches(StringView glob, StringView str,
                  const ArrayList<bool> &glob_active, usize mask_offset);

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] void quit(i32 code, bool should_goodbye = false);

} /* namespace utils */

} /* namespace shit */
