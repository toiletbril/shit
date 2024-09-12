#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "Eval.hpp"
#include "Os.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shit {

namespace utils {

/* Lower-level execution context. Path is the program path to execute, expanded
 * from program. Program is non-altered first arg. */
struct ExecContext
{
  static ExecContext make_from(SourceLocation                  location,
                               const std::vector<std::string> &args);

  std::optional<os::descriptor> in_fd{std::nullopt};
  std::optional<os::descriptor> out_fd{std::nullopt};

  bool is_builtin() const;

  const std::vector<std::string> &args() const;
  const std::string              &program() const;
  const SourceLocation           &source_location() const;

  void close_fds();
  void print_to_stdout(const std::string &s) const;

  const std::filesystem::path &program_path() const;
  const Builtin::Kind         &builtin_kind() const;

private:
  /* clang-format off */
  ExecContext(SourceLocation location,
              std::variant<shit::Builtin::Kind, std::filesystem::path> &&kind,
              const std::vector<std::string> &args);
  /* clang-format on */

  std::variant<shit::Builtin::Kind, std::filesystem::path> m_kind;

  SourceLocation           m_location;
  std::vector<std::string> m_args;
};

template<class T>
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

bool glob_matches(std::string_view glob, std::string_view str);

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] void quit(i32 code, bool should_goodbye = false);

} /* namespace utils */

} /* namespace shit */
