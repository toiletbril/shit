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

std::string merge_tokens_to_string(const std::vector<const Token *> &v);

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

i32 execute_context(ExecContext &&ec, bool is_async);

i32 execute_contexts_with_pipes(std::vector<ExecContext> &&ecs, bool is_async);

void string_replace(std::string &s, std::string_view to_replace,
                    std::string_view replace_with);

std::string lowercase_string(std::string_view s);

std::optional<std::filesystem::path> canonicalize_path(const std::string &path);

void initialize_path_map();

void clear_path_map();

/* Searches PATH for program binary. Returns absolute path to the program. */
std::optional<std::filesystem::path>
search_program_path(const std::string &program_name);

void set_current_directory(const std::filesystem::path &path);

std::filesystem::path get_current_directory();

bool glob_matches(std::string_view glob, std::string_view str,
                  usize source_position, const EscapeMap &em);

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] void quit(i32 code, bool should_goodbye = false);

} /* namespace utils */

} /* namespace shit */
