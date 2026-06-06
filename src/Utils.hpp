#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
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

std::string merge_tokens_to_string(const ArrayList<const Token *> &v);

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

void string_replace(std::string &s, std::string_view to_replace,
                    std::string_view replace_with);

std::string lowercase_string(std::string_view s);

Maybe<std::filesystem::path> canonicalize_path(const std::string &path);

/* Read a whole file into a string through the os descriptor layer, so no
   iostream file stream is pulled in. Returns nothing when the open fails. */
Maybe<std::string> read_entire_file(const std::string &path);

/* Read everything still available on standard input into a string. */
std::string read_entire_standard_input();

/* Read one line from a descriptor, without the trailing newline. Returns
   nothing at end of input with no bytes read. The read builtin passes the
   command's input descriptor so a redirection or a heredoc is honored. */
Maybe<std::string> read_line_from_fd(os::descriptor fd);

void initialize_path_map();

void clear_path_map();

/* Searches PATH for program binary. Returns absolute paths to the program. */
ArrayList<std::filesystem::path>
search_program_path(const std::string &program_name);

void set_current_directory(const std::filesystem::path &path);

std::filesystem::path get_current_directory();

bool glob_matches(std::string_view glob, std::string_view str,
                  const ArrayList<bool> &glob_active, usize mask_offset);

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] void quit(i32 code, bool should_goodbye = false);

} /* namespace utils */

} /* namespace shit */
