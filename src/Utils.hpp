#include "Common.hpp"
#include "string"
#include "string_view"
#include "vector"

#include <filesystem>
#include <optional>

namespace shit {

namespace utils {

std::string last_system_error_message();

/* Path is the program path to execute, expanded from program. Program is
 * non-altered first arg. */
i32 execute_program_by_path(const std::filesystem::path    &path,
                            std::string_view                program,
                            const std::vector<std::string> &args);

std::optional<std::string> get_environment_variable(std::string_view key);

/* Make launching programs more convenient, e.g strip out .exe for Windows. */
std::string_view sanitize_program_name(std::string_view program_name);

std::optional<std::string> expand_path(const std::string_view &path);

/* Normalizes the path. */
std::optional<std::filesystem::path>
canonicalize_path(const std::string_view &path);

/* Searches PATH for program binary. Returns absolute path to the program. */
std::optional<std::filesystem::path>
search_program_path(std::string_view program_name);

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
