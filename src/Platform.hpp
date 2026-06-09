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
#include <pthread.h>
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
#define SHIT_STDERR GetStdHandle(STD_ERROR_HANDLE)

#elif SHIT_PLATFORM_IS POSIX
constexpr char PATH_DELIMITER = ':';

using process = pid_t;
using descriptor = int;
using os_args = ArrayList<const char *>;

#define SHIT_INVALID_FD      -1
#define SHIT_INVALID_PROCESS -1

#define SHIT_STDIN  STDIN_FILENO
#define SHIT_STDOUT STDOUT_FILENO
#define SHIT_STDERR STDERR_FILENO
#endif

using ext_index = usize;

struct Pipe
{
  descriptor in{SHIT_INVALID_FD};
  descriptor out{SHIT_INVALID_FD};
};

fn make_pipe() wontthrow -> Maybe<Pipe>;

/* A handle to one running os thread. The pipe drain in a command substitution
   reads its end on this thread so output larger than the pipe buffer cannot
   deadlock the writer. */
struct thread
{
#if SHIT_PLATFORM_IS WIN32
  HANDLE handle{nullptr};
#elif SHIT_PLATFORM_IS POSIX
  pthread_t handle{};
#endif
};

/* Start a thread that runs entry with context, or None when the os cannot
   create it. The entry runs to completion and the thread keeps running until
   join_thread returns. */
fn start_thread(void (*entry)(void *), void *context) wontthrow
    -> Maybe<thread>;

/* Wait for a started thread to finish and release its handle. */
fn join_thread(thread t) wontthrow -> void;

/* How a redirection target file is opened. */
enum class file_open_mode : u8
{
  Truncate,          /* >  create or truncate for writing */
  TruncateNoClobber, /* >  under noclobber, fail if the file exists */
  Append,            /* >> create or append for writing */
  Read,              /* <  open an existing file for reading */
};

/* Open path for the given mode and return its descriptor, or None on error
   with the reason left in last_system_error_message. */
fn open_file_descriptor(StringView path, file_open_mode mode) throws
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
enum class process_state : u8
{
  Running,
  Exited,
  Stopped,
};

/* Check a process without blocking. Returns Running while it is alive, Exited
   with the status placed in status_out once it ends, and Stopped while it is
   suspended. */
fn poll_process(process p, i32 &status_out) wontthrow -> process_state;

/* Send a signal to a process by its numeric signal, for the kill builtin and
   for fg and bg to resume a stopped job with SIGCONT. Returns false on
   failure. */
fn signal_process(process p, i32 signal_number) wontthrow -> bool;

/* Resolve a signal name such as TERM, SIGTERM, or KILL to its number, or
   None when the name is not known. */
fn signal_number_from_name(StringView name) throws -> Maybe<i32>;

/* Resolve a signal number such as 2 or 15 to its bare upper-case name such as
   INT or TERM, or None when the number names no known signal. The trap builtin
   uses it so a trap set or listed by number reports the same name a trap set by
   name reports. */
fn signal_name_from_number(i32 number) throws -> Maybe<String>;

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

/* The backup of a shell descriptor taken before a redirection points it
   elsewhere. A compound command runs in the shell process, so a trailing
   redirect must save the shell's own descriptor, point it at the target, and
   put it back when the child finishes. */
struct saved_descriptor
{
  /* The shell-level descriptor number that was redirected, such as 0, 1, 2, or
     a higher number on POSIX. */
  i32 shell_fd{-1};
  /* A copy of the original descriptor, valid only when was_open is true. */
  descriptor saved{SHIT_INVALID_FD};
  /* False when shell_fd was not open before the redirection, so restore closes
     it instead of duplicating the backup back. */
  bool was_open{false};
  /* False when the dup2 onto shell_fd failed, as when the target descriptor was
     closed in a duplication like >&5 with fd 5 closed. The caller throws a
     located error and the restore puts shell_fd back unchanged. */
  bool dup2_ok{true};
};

/* Save shell_fd, then point it at target. The returned backup feeds
   restore_descriptor. The target descriptor is left for the caller to close. */
fn save_and_replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow
    -> saved_descriptor;
/* Put shell_fd back the way save_and_replace_descriptor found it, closing the
   backup. */
fn restore_descriptor(const saved_descriptor &saved) wontthrow -> void;

/* The live descriptor currently behind a shell descriptor number, for a
   duplication like 2>&1 that points one shell descriptor at another. */
fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor;

/* Point shell_fd at target permanently, the way exec with redirections and no
   command does. Unlike save_and_replace_descriptor it takes no backup, so the
   descriptor stays pointed at target for every later command until exec changes
   or closes it again. Returns false when the dup2 fails, as from a duplication
   onto a closed descriptor. The target descriptor is left for the caller to
   close, since it was a temporary file or a copy of another descriptor. */
fn replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow -> bool;

/* Close shell_fd permanently, the way exec N>&- does. Returns false when the
   descriptor was not open. */
fn close_shell_fd(i32 shell_fd) wontthrow -> bool;

fn get_environment_variable(StringView key) throws -> Maybe<String>;
fn set_environment_variable(StringView key, StringView value) throws -> void;
fn unset_environment_variable(StringView key) throws -> void;

/* The names of every variable in the process environment, the keys left of the
   first '=' in each entry. Variable completion offers these alongside the shell
   variable names. */
fn environment_names() throws -> ArrayList<String>;

fn is_child_process() wontthrow -> bool;

/* The process id of the shell itself, for $$. */
fn get_shell_process_id() wontthrow -> i64;

/* The numeric process id of a spawned process, for $!. */
fn process_id_of(process p) wontthrow -> i64;

fn is_stdin_a_tty() wontthrow -> bool;
fn is_stdout_a_tty() wontthrow -> bool;
fn is_stderr_a_tty() wontthrow -> bool;

fn erase_extension_and_get_its_index(String &program_name) throws -> ext_index;

fn get_current_user() throws -> Maybe<String>;

fn get_hostname() throws -> Maybe<String>;

fn get_home_directory() throws -> Maybe<Path>;

/* The home directory of a named user, for ~user expansion. None when no such
   user exists or the system has no user database. */
fn get_home_for_user(StringView username) throws -> Maybe<Path>;

/* Every user name the system knows, for ~user completion. Empty when the
   database cannot be read or the platform has none. */
fn enumerate_users() throws -> ArrayList<String>;

fn set_default_signal_handlers() throws -> void;

fn reset_signal_handlers() throws -> void;

/* Set to one by the SIGINT handler and polled by the evaluator, so a Ctrl-C
   aborts the running command, such as a loop that would otherwise spin forever.
   The main loop clears it before each interactive command. */
extern volatile sig_atomic_t INTERRUPT_REQUESTED;

/* A monotonic clock reading in nanoseconds, immune to wall-clock jumps, for
   measuring an elapsed interval. POSIX reads CLOCK_MONOTONIC and Windows reads
   QueryPerformanceCounter scaled to nanoseconds. */
fn monotonic_nanos() wontthrow -> u64;

/* The Linux hardware performance counters a measured run collects. The counts
   are valid only when measured_result::has_perf is true, which happens on Linux
   when perf_event_open succeeded. Every other platform leaves has_perf false.
 */
struct perf_counts
{
  u64 cpu_cycles{0};
  u64 instructions{0};
  u64 cache_references{0};
  u64 cache_misses{0};
  u64 branch_misses{0};
};

/* The result of running a command as a measured child. wall_nanos is the
   monotonic elapsed time around the child, peak_rss_bytes is the high-water
   resident set the child reached, and exit_status is its wait status decoded
   the way the shell reports one. The perf counts are filled only when has_perf
   is true. */
struct measured_result
{
  u64 wall_nanos{0};
  u64 peak_rss_bytes{0};
  i64 exit_status{0};
  bool has_perf{false};
  perf_counts perf{};
};

/* Run argv as a child process, wait for it, and return the measurement, or None
   when the child could not be spawned. When suppress_output is true the child's
   standard output and standard error are pointed at the null device so a
   benchmark loop stays quiet. The child keeps the shell's standard input. On
   Linux the hardware perf counters are collected when perf_event_open is
   permitted, and a non-Linux platform or a denied perf_event_open returns the
   wall time and peak resident set alone. */
fn run_measured(const ArrayList<String> &argv, bool suppress_output) throws
    -> Maybe<measured_result>;

fn execute_program(ExecContext &&ec) throws -> process;

/* Fork a child for a compound command used as a pipeline stage. In the child it
   places the pipe ends onto the standard descriptors, resets the signal
   handlers the way an exec'd child does, and returns zero so the caller
   evaluates the compound command's tree and exits. In the parent it returns the
   child process. The fds are the pipe ends already chosen for this stage, each
   None when the stage keeps the inherited descriptor. */
fn fork_compound_stage(Maybe<descriptor> in_fd, Maybe<descriptor> out_fd,
                       Maybe<descriptor> err_fd) throws -> process;

/* Terminate the current process at once with status, skipping the normal
   unwinding. A forked pipeline-stage child calls this after it evaluates its
   command so it never runs the parent's cleanup or unwinds back into the shared
   evaluator inside the duplicated process. */
[[noreturn]] fn exit_process_immediately(i32 status) wontthrow -> void;

/* Replace the current shell process with the program, applying its
   redirections, the way exec does. It does not fork, so on success it never
   returns. It throws on failure. */
[[noreturn]] fn replace_process(ExecContext &&ec) throws -> void;

/* Apply an exec context's redirections to the shell's own descriptors, for an
   exec with redirections and no command. */
fn redirect_self(const ExecContext &ec) throws -> void;

} /* namespace os */

} /* namespace shit */
