#pragma once

#include "Common.hpp"
#include "Os.hpp"
#include "Utils.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace shit {

namespace os {

using ExtIndex = usize;

struct Pipe
{
  descriptor in{SHIT_INVALID_FD};
  descriptor out{SHIT_INVALID_FD};
};

std::optional<Pipe> make_pipe();

os_args make_os_args(const std::vector<std::string> &args);

std::string last_system_error_message();

i32 wait_and_monitor_process(process p);

extern const std::vector<std::string> OMITTED_SUFFIXES;

std::optional<usize> write_fd(os::descriptor fd, const void *buf, usize size);
std::optional<usize> read_fd(os::descriptor fd, void *buf, usize size);

bool close_fd(os::descriptor fd);

/* Point the process standard output at target and return a handle to the
   previous output, so a command substitution can capture everything written.
   restore_stdout puts the previous output back. */
os::descriptor redirect_stdout(os::descriptor target);
void restore_stdout(os::descriptor saved);

std::optional<std::string> get_environment_variable(const std::string &key);
void set_environment_variable(const std::string &key, const std::string &value);
void unset_environment_variable(const std::string &key);

bool is_child_process();

/* The process id of the shell itself, for $$. */
i64 get_shell_process_id();

/* The numeric process id of a spawned process, for $!. */
i64 process_id_of(process p);

bool is_stdin_a_tty();
bool is_stdout_a_tty();

ExtIndex erase_extension_and_get_its_index(std::string &program_name);

std::optional<std::string> get_current_user();

std::optional<std::filesystem::path> get_home_directory();

void set_default_signal_handlers();

void reset_signal_handlers();

process execute_program(ExecContext &&ec);

} /* namespace os */

} /* namespace shit */
