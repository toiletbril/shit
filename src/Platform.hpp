#pragma once

#define POSIX 0b1
#define WIN32 0b10
#define COSMO 0b100

/* clang-format off */
/* Currently, Linux, Windows and Cosmopolitan builds are supported. */
#if defined __linux__ || defined BSD || defined __APPLE__ ||                   \
    defined __COSMOPOLITAN__
#include <cerrno>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined __COSMOPOLITAN__
#include <cosmo.h>
#define SHIT_SUPPORT_VECTOR (COSMO | POSIX)
#else
#define SHIT_SUPPORT_VECTOR (POSIX)
#endif
#elif defined _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SHIT_SUPPORT_VECTOR (WIN32)
#endif
/* clang-format on */

#define SHIT_PLATFORM_IS   (SHIT_SUPPORT_VECTOR) &
#define SHIT_PLATFORM_ISNT (~SHIT_SUPPORT_VECTOR) &

#include "ArrayList.hpp"
#include "Common.hpp"
#include "Maybe.hpp"
#include "Path.hpp"
#include "String.hpp"

#include <csignal>
#include <string>

namespace shit {

/* ExecContext is defined in Eval.hpp. The os functions take it by reference, so
   a forward declaration keeps this header below Eval in the include graph. */
struct ExecContext;

namespace os {

#if SHIT_PLATFORM_IS WIN32
constexpr char PATH_DELIMITER = ';';

using process = HANDLE;
using descriptor = HANDLE;
using os_args = std::string;

#define SHIT_INVALID_FD      INVALID_HANDLE_VALUE
#define SHIT_INVALID_PROCESS INVALID_HANDLE_VALUE

#define SHIT_STDIN  GetStdHandle(STD_INPUT_HANDLE)
#define SHIT_STDOUT GetStdHandle(STD_OUTPUT_HANDLE)

#elif SHIT_PLATFORM_IS POSIX
constexpr char PATH_DELIMITER = ':';

using process = pid_t;
using descriptor = int;
using os_args = ArrayList<const char *>;

#define SHIT_INVALID_FD      -1
#define SHIT_INVALID_PROCESS -1

#define SHIT_STDIN  STDIN_FILENO
#define SHIT_STDOUT STDOUT_FILENO
#endif

using ExtIndex = usize;

struct Pipe
{
  descriptor in{SHIT_INVALID_FD};
  descriptor out{SHIT_INVALID_FD};
};

fn make_pipe() -> Maybe<Pipe>;

/* How a redirection target file is opened. */
enum class FileOpenMode : u8
{
  Truncate,          /* >  create or truncate for writing */
  TruncateNoClobber, /* >  under noclobber, fail if the file exists */
  Append,            /* >> create or append for writing */
  Read,              /* <  open an existing file for reading */
};

/* Open path for the given mode and return its descriptor, or None on error
   with the reason left in last_system_error_message. */
fn open_file_descriptor(StringView path, FileOpenMode mode)
    -> Maybe<descriptor>;

/* Write content to an anonymous temporary file and return a descriptor
   positioned at its start, for feeding a heredoc body to a command's input. */
fn write_to_temp_file(StringView content) -> Maybe<descriptor>;

/* Read the file-creation mask without changing it, and set a new one. The umask
   builtin reads and writes the process mask through these. */
fn get_file_creation_mask() -> u32;
fn set_file_creation_mask(u32 mask) -> void;

fn make_os_args(const ArrayList<String> &args) -> os_args;

fn last_system_error_message() -> String;

fn wait_and_monitor_process(process p) -> i32;

/* The live state of a process, polled without blocking for the job table. */
enum class ProcessState : u8
{
  Running,
  Exited,
  Stopped,
};

/* Check a process without blocking. Returns Running while it is alive, Exited
   with the status placed in status_out once it ends, and Stopped while it is
   suspended. */
fn poll_process(process p, i32 &status_out) -> ProcessState;

/* Send a signal to a process by its numeric signal, for the kill builtin and
   for fg and bg to resume a stopped job with SIGCONT. Returns false on
   failure. */
fn signal_process(process p, i32 signal_number) -> bool;

/* Resolve a signal name such as TERM, SIGTERM, or KILL to its number, or
   None when the name is not known. */
fn signal_number_from_name(StringView name) -> Maybe<i32>;

/* Turn a numeric process id into the process handle the os layer uses. On POSIX
   the id is the handle. On Windows a handle is opened for it, which may be the
   invalid handle when the process is gone or not permitted. */
fn process_from_pid(i64 pid) -> process;

extern const ArrayList<String> OMITTED_SUFFIXES;

fn write_fd(os::descriptor fd, const void *buf, usize size) -> Maybe<usize>;
fn read_fd(os::descriptor fd, void *buf, usize size) -> Maybe<usize>;

fn close_fd(os::descriptor fd) -> bool;

/* Point the process standard output at target and return a handle to the
   previous output, so a command substitution can capture everything written.
   restore_stdout puts the previous output back. */
fn redirect_stdout(os::descriptor target) -> os::descriptor;
fn restore_stdout(os::descriptor saved) -> void;

fn get_environment_variable(StringView key) -> Maybe<String>;
fn set_environment_variable(StringView key, StringView value) -> void;
fn unset_environment_variable(StringView key) -> void;

fn is_child_process() -> bool;

/* The process id of the shell itself, for $$. */
fn get_shell_process_id() -> i64;

/* The numeric process id of a spawned process, for $!. */
fn process_id_of(process p) -> i64;

fn is_stdin_a_tty() -> bool;
fn is_stdout_a_tty() -> bool;

fn erase_extension_and_get_its_index(std::string &program_name) -> ExtIndex;

fn get_current_user() -> Maybe<String>;

fn get_home_directory() -> Maybe<Path>;

fn set_default_signal_handlers() -> void;

fn reset_signal_handlers() -> void;

/* Set to one by the SIGINT handler and polled by the evaluator, so a Ctrl-C
   aborts the running command, such as a loop that would otherwise spin forever.
   The main loop clears it before each interactive command. */
extern volatile sig_atomic_t INTERRUPT_REQUESTED;

fn execute_program(ExecContext &&ec) -> process;

/* Replace the current shell process with the program, applying its
   redirections, the way exec does. It does not fork, so on success it never
   returns. It throws on failure. */
[[noreturn]] fn replace_process(ExecContext &&ec) -> void;

/* Apply an exec context's redirections to the shell's own descriptors, for an
   exec with redirections and no command. */
fn redirect_self(const ExecContext &ec) -> void;

} /* namespace os */

} /* namespace shit */
