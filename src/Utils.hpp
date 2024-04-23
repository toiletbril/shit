#include "Common.hpp"
#include "Errors.hpp"
#include "string"
#include "string_view"
#include "vector"

#include <filesystem>
#include <optional>

namespace shit {

namespace utils {

constexpr usize MAX_PARENTHESES_DEPTH = 64;

std::string last_system_error_message();

i32 execute_program_by_path(const std::filesystem::path    &path,
                            const std::vector<std::string> &args);

std::optional<std::string> get_environment_variable(std::string_view key);

/* Removes .exe and other garbage to search path. */
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

std::filesystem::path current_directory();

[[noreturn]] void quit(i32 code);

std::optional<std::string> get_current_user();

std::optional<std::filesystem::path> get_home_directory();

} /* namespace utils */

} /* namespace shit */
