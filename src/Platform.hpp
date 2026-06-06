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

namespace shit {

/* ExecContext is defined in Eval.hpp. The os functions take it by reference, so
   a forward declaration keeps this header below Eval in the include graph. */
class ExecContext;

namespace os {

#if SHIT_PLATFORM_IS WIN32
constexpr char PATH_DELIMITER = ';';

using process = HANDLE;
using descriptor = HANDLE;
using os_args = String;

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

using ext_index = usize;

struct Pipe
{
  descriptor in{SHIT_INVALID_FD};
  descriptor out{SHIT_INVALID_FD};
};

fn make_pipe() wontthrow -> Maybe<Pipe>;

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
fn open_file_descriptor(StringView path, FileOpenMode mode) throws
    -> Maybe<descriptor>;

/* Write content to an anonymous temporary file and return a descriptor
   positioned at its start, for feeding a heredoc body to a command's input. */
fn write_to_temp_file(StringView content) throws -> Maybe<descriptor>;

/* Read the file-creation mask without changing it, and set a new one. The umask
   builtin reads and writes the process mask through these. */
fn get_file_creation_mask() wontthrow -> u32;
fn set_file_creation_mask(u32 mask) wontthrow -> void;

fn make_os_args(const ArrayList<String> &args) throws -> os_args;

fn last_system_error_message() throws -> String;

fn wait_and_monitor_process(process p) throws -> i32;

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
fn poll_process(process p, i32 &status_out) wontthrow -> ProcessState;

/* Send a signal to a process by its numeric signal, for the kill builtin and
   for fg and bg to resume a stopped job with SIGCONT. Returns false on
   failure. */
fn signal_process(process p, i32 signal_number) wontthrow -> bool;

/* Resolve a signal name such as TERM, SIGTERM, or KILL to its number, or
   None when the name is not known. */
fn signal_number_from_name(StringView name) throws -> Maybe<i32>;

/* Turn a numeric process id into the process handle the os layer uses. On POSIX
   the id is the handle. On Windows a handle is opened for it, which may be the
   invalid handle when the process is gone or not permitted. */
fn process_from_pid(i64 pid) wontthrow -> process;

extern const ArrayList<String> OMITTED_SUFFIXES;

fn write_fd(os::descriptor fd, const void *buf, usize size) wontthrow
    -> Maybe<usize>;
fn read_fd(os::descriptor fd, void *buf, usize size) wontthrow -> Maybe<usize>;

fn close_fd(os::descriptor fd) wontthrow -> bool;

/* Point the process standard output at target and return a handle to the
   previous output, so a command substitution can capture everything written.
   restore_stdout puts the previous output back. */
fn redirect_stdout(os::descriptor target) wontthrow -> os::descriptor;
fn restore_stdout(os::descriptor saved) wontthrow -> void;

fn get_environment_variable(StringView key) throws -> Maybe<String>;
fn set_environment_variable(StringView key, StringView value) throws -> void;
fn unset_environment_variable(StringView key) throws -> void;

fn is_child_process() wontthrow -> bool;

/* The process id of the shell itself, for $$. */
fn get_shell_process_id() wontthrow -> i64;

/* The numeric process id of a spawned process, for $!. */
fn process_id_of(process p) wontthrow -> i64;

fn is_stdin_a_tty() wontthrow -> bool;
fn is_stdout_a_tty() wontthrow -> bool;

fn erase_extension_and_get_its_index(String &program_name) throws -> ext_index;

fn get_current_user() throws -> Maybe<String>;

fn get_home_directory() throws -> Maybe<Path>;

fn set_default_signal_handlers() throws -> void;

fn reset_signal_handlers() throws -> void;

/* Set to one by the SIGINT handler and polled by the evaluator, so a Ctrl-C
   aborts the running command, such as a loop that would otherwise spin forever.
   The main loop clears it before each interactive command. */
extern volatile sig_atomic_t INTERRUPT_REQUESTED;

fn execute_program(ExecContext &&ec) throws -> process;

/* Replace the current shell process with the program, applying its
   redirections, the way exec does. It does not fork, so on success it never
   returns. It throws on failure. */
[[noreturn]] fn replace_process(ExecContext &&ec) throws -> void;

/* Apply an exec context's redirections to the shell's own descriptors, for an
   exec with redirections and no command. */
fn redirect_self(const ExecContext &ec) throws -> void;

} /* namespace os */

} /* namespace shit */
