#pragma once

#include "Common.hpp"
#include "Os.hpp"
#include "Utils.hpp"
#include "Maybe.hpp"

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

Maybe<Pipe> make_pipe();

/* How a redirection target file is opened. */
enum class FileOpenMode : u8
{
  Truncate, /* >  create or truncate for writing */
  Append,   /* >> create or append for writing */
  Read,     /* <  open an existing file for reading */
};

/* Open path for the given mode and return its descriptor, or nothing on error
   with the reason left in last_system_error_message. */
Maybe<descriptor> open_file_descriptor(const std::string &path,
                                       FileOpenMode mode);

/* Write content to an anonymous temporary file and return a descriptor
   positioned at its start, for feeding a heredoc body to a command's input. */
Maybe<descriptor> write_to_temp_file(const std::string &content);

/* Read the file-creation mask without changing it, and set a new one. The umask
   builtin reads and writes the process mask through these. */
u32 get_file_creation_mask();
void set_file_creation_mask(u32 mask);

os_args make_os_args(const std::vector<std::string> &args);

std::string last_system_error_message();

i32 wait_and_monitor_process(process p);

extern const std::vector<std::string> OMITTED_SUFFIXES;

Maybe<usize> write_fd(os::descriptor fd, const void *buf, usize size);
Maybe<usize> read_fd(os::descriptor fd, void *buf, usize size);

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

Maybe<std::string> get_current_user();

Maybe<std::filesystem::path> get_home_directory();

void set_default_signal_handlers();

void reset_signal_handlers();

process execute_program(ExecContext &&ec);

} /* namespace os */

} /* namespace shit */
