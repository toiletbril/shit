#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
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
  std::variant<shit::Builtin::Kind, std::filesystem::path> kind;

  std::string              program;
  std::vector<std::string> args;
  usize                    location;

  std::optional<os::descriptor> in{std::nullopt};
  std::optional<os::descriptor> out{std::nullopt};
};

ExecContext make_exec_context(const std::string              &program,
                              const std::vector<std::string> &args,
                              usize                           location);

i32 execute_context(const ExecContext &&ec);

i32 execute_contexts_with_pipes(std::vector<ExecContext> &ecs);

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
