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

template <class T>
std::string
merge_args_to_string(const std::vector<T> &v)
{
  std::string r{};
  for (const std::string &s : v) {
    r += s;
    if (s != v.back()) {
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

/* Read one line from standard input, without the trailing newline. Returns
   nothing at end of input with no bytes read. */
Maybe<std::string> read_line_from_standard_input();

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
