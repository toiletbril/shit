#include "Common.hpp"
#include "string"
#include "vector"

#include <filesystem>
#include <optional>

namespace shit {

namespace utils {

std::string last_system_error_message();

/* Path is the program path to execute, expanded from program. Program is
 * non-altered first arg. */
struct ExecContext
{
  const std::filesystem::path    m_path;
  const std::string              m_program;
  const std::vector<std::string> m_args;
};

i32 execute_program(const ExecContext &&ec);

i32 execute_program_sequence_with_pipes(const std::vector<ExecContext> &ecs);

std::optional<std::string> get_environment_variable(const std::string &key);

/* Make launching programs more convenient, e.g strip out .exe for Windows. */
bool sanitize_program_name(std::string &program_name);

std::optional<std::string> simple_shell_expand(const std::string &path);

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
[[noreturn]] void quit(i32 code);

std::optional<std::string> get_current_user();

std::optional<std::filesystem::path> get_home_directory();

void set_default_signal_handlers();

} /* namespace utils */

} /* namespace shit */
