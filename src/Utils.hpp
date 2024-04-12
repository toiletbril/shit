#include "Common.hpp"
#include "Errors.hpp"
#include "string"
#include "string_view"
#include "vector"

#include <filesystem>
#include <optional>

constexpr usize SHIT_MAX_PARENTHESES_DEPTH = 64;

i32 shit_exec(usize location, const std::filesystem::path &path,
              const std::vector<std::string> &args);

std::optional<std::string> shit_get_env(std::string_view key);

/* Removes .exe and other garbage to search path. */
std::string_view shit_sanitize_program_name(std::string_view program_name);

std::optional<std::filesystem::path>
shit_search_path_env(std::string_view program_name);

bool shit_process_is_child();

void shit_current_directory_set(const std::filesystem::path &path);

std::filesystem::path shit_current_directory();

[[noreturn]] void shit_exit(i32 code);
