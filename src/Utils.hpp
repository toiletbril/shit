#pragma once

#include "Builtin.hpp"
#include "Common.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shit {

namespace utils {

std::string last_system_error_message();

/* Path is the program path to execute, expanded from program. Program is
 * non-altered first arg. */
struct ExecContext
{
  std::variant<shit::Builtin::Kind, std::filesystem::path> kind;

  std::string              program;
  std::vector<std::string> args;
  usize                    location;

  std::optional<SHIT_FD> in{std::nullopt};
  std::optional<SHIT_FD> out{std::nullopt};
};

ExecContext make_exec_context(const std::string              &program,
                              const std::vector<std::string> &args,
                              usize                           location);

i32 execute_context(const ExecContext &&ec);

i32 execute_contexts_with_pipes(std::vector<ExecContext> &ecs);

usize write_fd(SHIT_FD fd, void *buf, u8 size);
usize read_fd(SHIT_FD fd, void *buf, u8 size);

std::optional<std::string> get_environment_variable(const std::string &key);

std::optional<std::string> simple_shell_expand(const std::string &path);

std::vector<std::string>
simple_shell_expand_args(const std::vector<std::string> &args);

/* Normalizes the path. */
std::optional<std::filesystem::path> canonicalize_path(const std::string &path);

void initialize_path_map();

void clear_path_map();

/* Searches PATH for program binary. Returns absolute path to the program. */
std::optional<std::filesystem::path>
search_program_path(const std::string &program_name);

bool is_child_process();

void set_current_directory(const std::filesystem::path &path);

std::filesystem::path get_current_directory();

/* Do a cleanup if necessary, then call exit(code). */
[[noreturn]] void quit(i32 code, bool should_goodbye = false);

std::optional<std::string> get_current_user();

std::optional<std::filesystem::path> get_home_directory();

void set_default_signal_handlers();

} /* namespace utils */

} /* namespace shit */
