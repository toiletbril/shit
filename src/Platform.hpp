#pragma once

#define POSIX 0b1
#define WIN32 0b10
#define COSMO 0b100

/* clang-format off */
#if defined __linux__ || defined BSD || defined __APPLE__ ||                   \
    defined __COSMOPOLITAN__
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

namespace shit {

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

/* The pipe drain in a command substitution reads its end on this thread so
   output larger than the pipe buffer cannot deadlock the writer. */
struct thread
{
#if SHIT_PLATFORM_IS WIN32
  HANDLE handle{nullptr};
#elif SHIT_PLATFORM_IS POSIX
  pthread_t handle{};
#endif
};

fn start_thread(void (*entry)(opaque *), opaque *context) wontthrow
    -> Maybe<thread>;

fn join_thread(thread t) wontthrow -> void;

enum class file_open_mode : u8
{
  Truncate,          /* >  create or truncate for writing */
  TruncateNoClobber, /* >  under noclobber, fail if the file exists */
  Append,            /* >> create or append for writing */
  Read,              /* <  open an existing file for reading */
  ReadWrite,         /* <> create or open for reading and writing */
};

/* Open path for the given mode and return its descriptor, or None on error
   with the reason left in last_system_error_message. */
fn open_file_descriptor(StringView path, file_open_mode mode) throws
    -> Maybe<descriptor>;

/* Write content to an anonymous temporary file and return a descriptor
   positioned at its start, for feeding a heredoc body to a command's input. */
fn write_to_temp_file(StringView content) throws -> Maybe<descriptor>;

/* Tracks the temporary files a process substitution leaves for the consuming
   command to read by path, and deletes them once that command finishes. On a
   platform that leaves no temp file, such as POSIX, it holds nothing. */
class TempFileSet
{
public:
  fn track(Path path) throws -> void;
  /* The number of tracked files, a mark a command takes at entry so its own
     cleanup deletes only the files tracked after it. */
  mustuse fn count() const wontthrow -> usize;
  /* Delete every file tracked at or after the mark on a best-effort basis, and
     keep one still held open by a reader for a later retry. */
  fn cleanup_from(usize mark) wontthrow -> void;

private:
#if SHIT_PLATFORM_IS WIN32
  ArrayList<Path> m_paths{heap_allocator()};
#endif
};

#if SHIT_PLATFORM_IS WIN32
/* Run a <(cmd) process substitution on a platform with no fork by spawning a
   fresh shell whose standard output is captured into a temporary file, and
   return that file's path for the consuming command to read. Windows only. */
fn run_substitution_to_temp(StringView source, bool bash_compatible) throws
    -> Maybe<String>;
#endif

/* Read the file-creation mask without changing it, and set a new one. The umask
   builtin reads and writes the process mask through these. */
fn get_file_creation_mask() wontthrow -> u32;
fn set_file_creation_mask(u32 mask) wontthrow -> void;

fn make_os_args(const ArrayList<String> &args) throws -> os_args;

fn last_system_error_message() throws -> String;

fn wait_and_monitor_process(process p, bool *was_stopped = nullptr) throws
    -> i32;

/* Wait for a process and reap it without the job-control reporting of
   wait_and_monitor_process, returning its exit status or 128 plus the signal.
   A process substitution helper reaps this way, since its signal is expected.
 */
fn reap_process_quietly(process p) throws -> i32;

/* The live state of a process, polled without blocking for the job table.
   Unchanged means the poll reported no new transition, so the caller keeps the
   state it already recorded rather than overwriting a consumed stop. */
enum class process_state : u8
{
  Running,
  Exited,
  Stopped,
  Unchanged,
};

/* Check a process without blocking. Returns Stopped or Exited on a new
   transition with the status placed in status_out, Running on a resume, and
   Unchanged when nothing new is reported since the last poll. */
fn poll_process(process p, i32 &status_out) wontthrow -> process_state;

/* Send a signal to a process by its numeric signal, for the kill builtin and
   for fg and bg to resume a stopped job with SIGCONT. Returns false on
   failure. */
fn signal_process(process p, i32 signal_number) wontthrow -> bool;

/* Resolve a signal name such as TERM, SIGTERM, or KILL to its number, or
   None when the name is not known. */
fn signal_number_from_name(StringView name) throws -> Maybe<i32>;

/* Resolve a signal number such as 2 or 15 to its bare upper-case name such as
   INT or TERM, or None when the number names no known signal. */
fn signal_name_from_number(i32 number) throws -> Maybe<String>;

/* The signal names signal_number_from_name accepts, for the completion engine
   to offer after kill -. */
fn signal_names() throws -> const ArrayList<StringView> &;

/* Turn a numeric process id into the process handle the os layer uses. On POSIX
   the id is the handle. On Windows a handle is opened for it, which may be the
   invalid handle when the process is gone or not permitted. */
fn process_from_pid(i64 pid) wontthrow -> process;

/* One live process the shitbox pkill, killall, and ps utilities read, its
   numeric id, the basename of its command, the owner uid, and the full command
   line. The command line is empty for a process that exposes none. */
struct process_entry
{
  i64 pid{0};
  String name{};
  u32 owner_id{0};
  String command_line{};
  /* The BSD aux columns, filled only when resource stats are requested.
     virtual_kib and resident_kib are kibibytes, state is the single status
     letter R/S/D/Z/T, and cpu_ticks is the user plus system time in ticks. */
  u64 virtual_kib{0};
  u64 resident_kib{0};
  char state{'?'};
  u64 cpu_ticks{0};
};

/* Every process the current user can see, for the shitbox pkill and killall
   utilities to match a name against. Empty on a platform with no process
   listing. The resource stats are read only when asked. */
fn enumerate_processes(bool include_resource_stats = false) throws
    -> ArrayList<process_entry>;

/* The filesystem mutations the shitbox coreutils run. Each returns false on
   failure with the reason left in last_system_error_message. make_directory
   takes the permission bits the umask still narrows. */
fn make_directory(StringView path, u32 mode) wontthrow -> bool;
/* Set the permission bits of an existing path to the exact mode, the chmod the
   mkdir -m path runs. A platform without POSIX mode bits ignores the mode. */
fn set_file_mode(StringView path, u32 mode) wontthrow -> bool;
/* Set the access and the modification times of an existing file to the current
   time, the touch utility's update path for a file that already exists. */
fn touch_file_times(StringView path) wontthrow -> bool;
fn remove_directory(StringView path) wontthrow -> bool;
fn remove_file(StringView path) wontthrow -> bool;
fn rename_path(StringView from, StringView to) wontthrow -> bool;
fn create_symlink(StringView target, StringView link_path) wontthrow -> bool;
/* The target a symlink points at, the raw link text rather than the resolved
   path, so cp can recreate the link as it stands. */
fn read_symlink(StringView path) wontthrow -> Maybe<String>;

/* The absolute path of the running binary, the symlink target the shitbox
   --assimilate install points each utility name at. */
fn current_executable_path() wontthrow -> Maybe<String>;

/* The metadata one lstat returns, for the shitbox ls long format. stat_path
   fills it and returns false when the path cannot be read. The mode carries the
   type and permission bits in the POSIX st_mode layout, and a symlink reports
   its own status. */
struct file_status
{
  u32 mode{0};
  u64 link_count{0};
  u32 owner_id{0};
  u32 group_id{0};
  u64 size{0};
  i64 modification_time{0};
  u64 blocks{0};
};

fn stat_path(StringView path, file_status &status) wontthrow -> bool;

/* The ten-character permission string for a mode, such as drwxr-xr-x. The
   setuid, setgid, and sticky bits render as s, s, and t in the execute
   slots. */
fn format_mode_string(u32 mode) throws -> String;

/* The single type letter for a mode, d for a directory, l for a symlink, c, b,
   p, or s for the device, fifo, and socket types, and - for a regular file. */
fn file_type_letter(u32 mode) wontthrow -> char;

/* The user name for a numeric uid and the group name for a numeric gid, read
   directly from /etc/passwd and /etc/group, so the static build stays free of
   getpwuid and getgrgid. */
fn uid_to_username(u32 uid) throws -> Maybe<String>;
fn gid_to_groupname(u32 gid) throws -> Maybe<String>;

/* Sleep for the given seconds, for the shitbox sleep utility. A fractional
   value sleeps the whole and the fractional part together. */
fn sleep_for_seconds(double seconds) wontthrow -> void;

extern const ArrayList<String> OMITTED_SUFFIXES;

fn write_fd(os::descriptor fd, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>;
fn write_to_numbered_fd(i64 fd_number, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>;
fn read_fd(os::descriptor fd, opaque *buf, usize size) wontthrow
    -> Maybe<usize>;

fn close_fd(os::descriptor fd) wontthrow -> bool;

/* Point the process standard output at target and return a handle to the
   previous output, so a command substitution can capture everything written. */
fn redirect_stdout(os::descriptor target) wontthrow -> os::descriptor;
fn restore_stdout(os::descriptor saved) wontthrow -> void;

/* The backup of a shell descriptor taken before a redirection points it
   elsewhere, put back when the child finishes. */
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
  bool is_dup2_ok{true};
  /* On Windows, the handle this redirection installed in the standard-handle
     slot, so restore closes that exact handle rather than whatever the slot
     holds at restore time. Unused on POSIX, which restores by dup2. */
  descriptor replacement{SHIT_INVALID_FD};
};

/* Save shell_fd, then point it at target. The returned backup feeds
   restore_descriptor. The target descriptor is left for the caller to close. */
fn save_and_replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow
    -> saved_descriptor;
/* Back up shell_fd without replacing it, so a run that may move the descriptor
   underneath can be put back with restore_descriptor. */
fn save_descriptor(i32 shell_fd) wontthrow -> saved_descriptor;
/* Put shell_fd back the way save_and_replace_descriptor found it, closing the
   backup. */
fn restore_descriptor(const saved_descriptor &saved) wontthrow -> void;

/* The live descriptor currently behind a shell descriptor number, for a
   duplication like 2>&1 that points one shell descriptor at another. */
fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor;

/* The descriptor a user-facing fd number names, for read -u and mapfile -u. On
   POSIX the number is the descriptor, and on Windows it maps to the C runtime
   handle the way the -t test does. */
fn descriptor_from_fd_number(i64 fd_number) wontthrow -> os::descriptor;

/* Point shell_fd at target permanently, the way exec with redirections and no
   command does. Unlike save_and_replace_descriptor it takes no backup. Returns
   false when the dup2 fails. The target descriptor is left for the caller to
   close. */
fn replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow -> bool;

/* Close shell_fd permanently, the way exec N>&- does. Returns false when the
   descriptor was not open. */
fn close_shell_fd(i32 shell_fd) wontthrow -> bool;

fn get_environment_variable(StringView key) throws -> Maybe<String>;
fn set_environment_variable(StringView key, StringView value) throws -> void;
fn unset_environment_variable(StringView key) throws -> void;

/* The names of every variable in the process environment, the keys left of the
   first '=' in each entry. */
fn environment_names() throws -> ArrayList<String>;

fn is_child_process() wontthrow -> bool;

/* The process id of the shell itself, for $$. */
fn get_shell_process_id() wontthrow -> i64;

/* The parent process id, and the real and effective user ids, the source of the
   bash PPID, UID, and EUID variables. */
fn get_parent_process_id() wontthrow -> i64;
fn get_real_user_id() wontthrow -> i64;
fn get_effective_user_id() wontthrow -> i64;
fn get_real_group_id() wontthrow -> i64;

/* The configured child-process limit, the source of CHILD_MAX. */
fn child_max() wontthrow -> i64;

/* The machine hardware name from uname, the source of HOSTTYPE and the stem of
   MACHTYPE. */
fn machine_type() throws -> String;

/* The OSTYPE name bash compiles in, so the dynamic variable reads the
   platform a config branches on. */
inline fn ostype_name() wontthrow -> StringView
{
#if defined(_WIN32)
  return "msys";
#elif defined(__APPLE__)
  return "darwin";
#else
  return "linux-gnu";
#endif
}

/* Whether the shell runs with an effective user or group id that differs from
   its real one, the setuid or setgid case. The shell skips its startup config
   files then, so a file an attacker controls cannot run with the raised
   privileges. */
fn is_running_setuid() wontthrow -> bool;

/* Reopen the controlling terminal onto fd 0, the recovery for a script run that
   left stdin pointing away from the tty. */
fn reopen_terminal_as_stdin() wontthrow -> bool;

/* The numeric process id of a spawned process, for $!. */
fn process_id_of(process p) wontthrow -> i64;

fn is_stdin_a_tty() wontthrow -> bool;
fn is_stdout_a_tty() wontthrow -> bool;
fn is_stderr_a_tty() wontthrow -> bool;
fn is_fd_a_tty(descriptor fd) wontthrow -> bool;

/* Clear the close-on-exec flag so the descriptor survives an exec and a spawned
   command inherits it, for a process substitution that keeps a pipe end open
   for the command to reach through /dev/fd. */
fn make_fd_inheritable(descriptor fd) wontthrow -> void;

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

/* Install the shell signal handlers. The interactive shell blocks the
   terminal-generated signals so a Ctrl-C, Ctrl-Z, or hangup at the prompt does
   not take it down, while a non-interactive script leaves those at their
   default. SIGINT routes to the polled handler in both modes. */
fn set_default_signal_handlers(bool is_interactive) throws -> void;

fn reset_signal_handlers() throws -> void;

/* Set to one by the SIGINT handler and polled by the evaluator, so a Ctrl-C
   aborts the running command. The main loop clears it before each interactive
   command. */
extern volatile sig_atomic_t INTERRUPT_REQUESTED;

/* Raised by the SIGCHLD handler when a child changes state, read and cleared
   by the prompt's wake hook so set -b reports a finished job immediately. */
extern volatile sig_atomic_t CHILD_STATE_CHANGED;

/* Set to one whenever any trapped signal arrives, so the evaluator's hot poll
   is a single read. The drain at the command boundary clears it as it consumes
   the per-signal flags. */
extern volatile sig_atomic_t SIGNAL_PENDING;

/* Install the shell's async-safe handler for a signal a trap names, so its
   arrival sets a pending flag the evaluator drains. A signal the startup
   blocked is unblocked here. */
fn set_trap_handler(i32 signal_number) throws -> void;

/* Install the ignore disposition for a signal, for a trap with an empty action
   such as trap "" INT, so the signal is discarded. */
fn set_trap_ignore(i32 signal_number) throws -> void;

/* Restore a signal's default disposition when its trap is removed. SIGINT
   returns to the shell's interrupt handler, every other signal to SIG_DFL. */
fn clear_trap_handler(i32 signal_number) throws -> void;

/* Return the next trapped signal whose flag is set and clear it, or zero when
   none remain. The evaluator calls it in a loop to run each pending trap. */
fn take_pending_signal() wontthrow -> i32;

/* A monotonic clock reading in nanoseconds, immune to wall-clock jumps. POSIX
   reads CLOCK_MONOTONIC and Windows reads QueryPerformanceCounter. */
fn monotonic_nanos() wontthrow -> u64;

/* The wall clock in microseconds since the Unix epoch, the source of the bash
   EPOCHREALTIME variable. */
fn realtime_microseconds() wontthrow -> u64;

/* Format a time through strftime in the local zone, the source of the printf
   %(fmt)T conversion. A negative epoch renders the current time, the bash -1
   and -2 forms. */
fn format_local_time(StringView format, i64 epoch) throws -> String;

/* The terminal's column and row count, the source of COLUMNS and LINES. False
   when there is no terminal or the size is unknown, leaving the outputs
   untouched. */
fn terminal_size(u32 &columns, u32 &rows) wontthrow -> bool;

/* The user and system seconds this process's children have consumed so far,
   read from RUSAGE_CHILDREN. Windows has no equivalent and reports zero. */
fn children_cpu_seconds(double &user_seconds, double &system_seconds) wontthrow
    -> void;

/* The Linux hardware performance counters a measured run collects. The counts
   are valid only when measured_result::has_perf is true. */
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
   resident set, and exit_status is its decoded wait status. The perf counts are
   filled only when has_perf is true. */
struct measured_result
{
  u64 wall_nanos{0};
  u64 peak_rss_bytes{0};
  i64 exit_status{0};
  bool has_perf{false};
  perf_counts perf{};
};

/* Run argv as a child process, wait for it, and return the measurement, or None
   when the child could not be spawned. On Linux the hardware perf counters are
   collected when perf_event_open is permitted. */
fn run_measured(const ArrayList<String> &argv, bool suppress_output) throws
    -> Maybe<measured_result>;

/* allow_script_fallback lets a single foreground command report an ENOEXEC file
   to the caller through ExecFormatError. new_process_group puts the child in
   its own process group so it can own the controlling terminal. */
fn execute_program(ExecContext &&ec, bool allow_script_fallback = false,
                   bool new_process_group = false) throws -> process;

/* Whether standard input is the shell's controlling terminal, so a foreground
   command may be handed the terminal's process group. */
fn shell_has_controlling_terminal() wontthrow -> bool;

/* The fully resolved path with every symlink followed, so completion can read
   the spec and manpage of what a symlinked command really runs. On Windows it
   returns the input unchanged. */
fn canonical_path(const Path &path) wontthrow -> Maybe<Path>;

/* The paths a shell glob pattern matches, for the shitbox make $(wildcard ...)
   function. The matches are transient, so the caller passes the arena they live
   in. A pattern that matches nothing yields an empty list. On Windows the
   wildcard applies to the last path component the way FindFirstFile expands it.
 */
fn glob_matches(StringView pattern, Allocator allocator) throws
    -> ArrayList<String>;

/* Whether the directory is safe to run a binary from for its --help text, so it
   is owned by root or the current user and is not writable by group or other,
   rejecting a world-writable directory such as /tmp. On Windows it returns
   false. */
fn directory_is_trusted_for_exec(const Path &directory) wontthrow -> bool;

/* Run argv as a child, capturing its standard output and standard error, and
   return that text. The capture is bounded by timeout_nanos, and a child still
   running at the deadline is killed and None is returned, so a slow or hung
   --help never freezes the prompt. */
fn capture_program_output(const ArrayList<String> &argv,
                          u64 timeout_nanos) wontthrow -> Maybe<String>;

/* Hand the controlling terminal to the given process's group, ignoring SIGTTOU
   across the change. A no-op without a controlling terminal. */
fn give_controlling_terminal_to(process p) wontthrow -> void;
fn reclaim_controlling_terminal() wontthrow -> void;

/* Fork a child for a compound command used as a pipeline stage. In the child it
   places the pipe ends onto the standard descriptors, resets the signal
   handlers, and returns zero so the caller evaluates the tree and exits. */
fn fork_compound_stage(Maybe<descriptor> in_fd, Maybe<descriptor> out_fd,
                       Maybe<descriptor> err_fd) throws -> process;

/* Fork a child for a job in its own process group. In the child it resets the
   signal handlers, joins a new group, and returns zero so the caller runs the
   job and exits. The parent joins the child to the group and returns its pid.
 */
fn fork_job_process() throws -> process;

#if SHIT_PLATFORM_IS WIN32
/* Run a compound pipeline stage on a platform with no fork by spawning a fresh
   shell that re-parses the stage's source, with the pipe ends wired as its
   standard input and output. Windows only. */
fn spawn_subshell_stage(StringView source, Maybe<descriptor> in_fd,
                        Maybe<descriptor> out_fd, bool bash_compatible) throws
    -> Maybe<process>;
#endif

/* Terminate the current process at once with status, skipping the normal
   unwinding. A forked pipeline-stage child calls this so it never runs the
   parent's cleanup inside the duplicated process. */
[[noreturn]] fn exit_process_immediately(i32 status) wontthrow -> void;

/* Replace the current shell process with the program, applying its
   redirections, the way exec does. It does not fork, so on success it never
   returns. It throws on failure. */
[[noreturn]] fn replace_process(ExecContext &&ec) throws -> void;

/* Apply an exec context's redirections to the shell's own descriptors, for an
   exec with redirections and no command. */
fn redirect_self(const ExecContext &ec) throws -> void;

} // namespace os

} // namespace shit
