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

Maybe<Pipe> make_pipe();

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
Maybe<descriptor> open_file_descriptor(StringView path, FileOpenMode mode);

/* Write content to an anonymous temporary file and return a descriptor
   positioned at its start, for feeding a heredoc body to a command's input. */
Maybe<descriptor> write_to_temp_file(StringView content);

/* Read the file-creation mask without changing it, and set a new one. The umask
   builtin reads and writes the process mask through these. */
u32 get_file_creation_mask();
void set_file_creation_mask(u32 mask);

os_args make_os_args(const ArrayList<String> &args);

String last_system_error_message();

i32 wait_and_monitor_process(process p);

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
ProcessState poll_process(process p, i32 &status_out);

/* Send a signal to a process by its numeric signal, for the kill builtin and
   for fg and bg to resume a stopped job with SIGCONT. Returns false on
   failure. */
bool signal_process(process p, i32 signal_number);

/* Resolve a signal name such as TERM, SIGTERM, or KILL to its number, or
   None when the name is not known. */
Maybe<i32> signal_number_from_name(StringView name);

/* Turn a numeric process id into the process handle the os layer uses. On POSIX
   the id is the handle. On Windows a handle is opened for it, which may be the
   invalid handle when the process is gone or not permitted. */
process process_from_pid(i64 pid);

extern const ArrayList<String> OMITTED_SUFFIXES;

Maybe<usize> write_fd(os::descriptor fd, const void *buf, usize size);
Maybe<usize> read_fd(os::descriptor fd, void *buf, usize size);

bool close_fd(os::descriptor fd);

/* Point the process standard output at target and return a handle to the
   previous output, so a command substitution can capture everything written.
   restore_stdout puts the previous output back. */
os::descriptor redirect_stdout(os::descriptor target);
void restore_stdout(os::descriptor saved);

Maybe<String> get_environment_variable(StringView key);
void set_environment_variable(StringView key, StringView value);
void unset_environment_variable(StringView key);

bool is_child_process();

/* The process id of the shell itself, for $$. */
i64 get_shell_process_id();

/* The numeric process id of a spawned process, for $!. */
i64 process_id_of(process p);

bool is_stdin_a_tty();
bool is_stdout_a_tty();

ExtIndex erase_extension_and_get_its_index(std::string &program_name);

Maybe<String> get_current_user();

Maybe<Path> get_home_directory();

void set_default_signal_handlers();

void reset_signal_handlers();

process execute_program(ExecContext &&ec);

/* Replace the current shell process with the program, applying its
   redirections, the way exec does. It does not fork, so on success it never
   returns. It throws on failure. */
[[noreturn]] void replace_process(ExecContext &&ec);

/* Apply an exec context's redirections to the shell's own descriptors, for an
   exec with redirections and no command. */
void redirect_self(const ExecContext &ec);

} /* namespace os */

} /* namespace shit */
