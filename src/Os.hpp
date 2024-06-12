#pragma once

#include "Common.hpp"
#include "OsCommon.hpp"
#include "Utils.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shit {

namespace os {

struct Pipe
{
  descriptor stdin_write{SHIT_INVALID_FD};
  descriptor stdin_read{SHIT_INVALID_FD};
  descriptor stdout_write{SHIT_INVALID_FD};
  descriptor stdout_read{SHIT_INVALID_FD};
};

std::optional<Pipe> make_pipe();

OsArgs
make_os_args(const std::string &program, const std::vector<std::string> &args);


std::string
last_system_error_message();

i32 wait_and_monitor_process(process p);

extern const std::vector<std::string> OMITTED_SUFFIXES;

usize write_fd(os::descriptor fd, void *buf, u8 size);

usize read_fd(os::descriptor fd, void *buf, u8 size);

bool close_fd(os::descriptor);

std::optional<std::string> get_environment_variable(const std::string &key);

bool is_child_process();

usize sanitize_program_name(std::string &program_name);

std::optional<std::string> get_current_user();

std::optional<std::filesystem::path> get_home_directory();

void set_default_signal_handlers();

void reset_signal_handlers();

process execute_program(const utils::ExecContext &ec);

} /* namespace os */

} /* namespace shit */
