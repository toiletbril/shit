#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "Errors.hpp"
#include "Os.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shit {

namespace utils {

/* Path is the program path to execute, expanded from program. Program is
 * non-altered first arg. */
struct ExecContext
{
  static ExecContext make(const std::string              &program,
                          const std::vector<std::string> &args, usize location);

  std::optional<os::descriptor> in{std::nullopt};
  std::optional<os::descriptor> out{std::nullopt};

  usize                           location() const;
  const std::string              &program() const;
  const std::vector<std::string> &args() const;

  void                         close_fds();
  bool                         is_builtin() const;
  const std::filesystem::path &program_path() const;
  const Builtin::Kind         &builtin_kind() const;

private:
  /* clang-format off */
  ExecContext(usize location, const std::string &program,
              const std::vector<std::string> &args,
              std::variant<shit::Builtin::Kind, std::filesystem::path> &&kind);
  /* clang-format on */

  usize                    m_location;
  std::string              m_program;
  std::vector<std::string> m_args;

  std::variant<shit::Builtin::Kind, std::filesystem::path> m_kind;
};

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

i32 execute_context(ExecContext &&ec);

i32 execute_contexts_with_pipes(std::vector<ExecContext> &&ecs);

std::optional<std::filesystem::path> canonicalize_path(const std::string &path);

std::optional<std::string> simple_shell_expand(const std::string &path);

std::vector<std::string>
simple_shell_expand_args(const std::vector<std::string> &args);

void initialize_path_map();

void clear_path_map();

/* Searches PATH for program binary. Returns absolute path to the program. */
std::optional<std::filesystem::path>
search_program_path(const std::string &program_name);

void set_current_directory(const std::filesystem::path &path);

std::filesystem::path get_current_directory();

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] void quit(i32 code, bool should_goodbye = false);

} /* namespace utils */

} /* namespace shit */
