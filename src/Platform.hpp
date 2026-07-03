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

fn open_file_descriptor(StringView path, file_open_mode mode) throws
    -> Maybe<descriptor>;

fn write_to_temp_file(StringView content) throws -> Maybe<descriptor>;

/* On a platform that leaves no temp file, such as POSIX, it holds nothing. */
class TempFileSet
{
public:
  fn track(Path path) throws -> void;
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
fn run_substitution_to_temp(StringView source, bool bash_compatible) throws
    -> Maybe<String>;
#endif

fn get_file_creation_mask() wontthrow -> u32;
fn set_file_creation_mask(u32 mask) wontthrow -> void;

fn make_os_args(const ArrayList<String> &args) throws -> os_args;

fn last_system_error_message() throws -> String;

fn wait_and_monitor_process(process p, bool *was_stopped = nullptr) throws
    -> i32;

fn reap_process_quietly(process p) throws -> i32;

/* Unchanged means the poll reported no new transition, so the caller keeps the
   state it already recorded. */
enum class process_state : u8
{
  Running,
  Exited,
  Stopped,
  Unchanged,
};

fn poll_process(process p, i32 &status_out) wontthrow -> process_state;

fn signal_process(process p, i32 signal_number) wontthrow -> bool;

fn signal_number_from_name(StringView name) throws -> Maybe<i32>;

fn signal_name_from_number(i32 number) throws -> Maybe<String>;

fn signal_names() throws -> const ArrayList<StringView> &;

/* On Windows a handle is opened for it, which may be the invalid handle when
   the process is gone or not permitted. */
fn process_from_pid(i64 pid) wontthrow -> process;

/* One live process the shitbox pkill, killall, and ps utilities read, its
   numeric id, the basename of its command, the owner uid, and the full command
   line. The command line is empty for a process that exposes none. */
struct process_entry
{
  i64 pid{0};
  String name{heap_allocator()};
  u32 owner_id{0};
  String command_line{heap_allocator()};
  /* The BSD aux columns, filled only when resource stats are requested. */
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

fn make_directory(StringView path, u32 mode) wontthrow -> bool;
fn set_file_mode(StringView path, u32 mode) wontthrow -> bool;
fn touch_file_times(StringView path) wontthrow -> bool;
fn remove_directory(StringView path) wontthrow -> bool;
fn remove_file(StringView path) wontthrow -> bool;
fn rename_path(StringView from, StringView to) wontthrow -> bool;
fn create_symlink(StringView target, StringView link_path) wontthrow -> bool;
fn read_symlink(StringView path) wontthrow -> Maybe<String>;

fn current_executable_path() wontthrow -> Maybe<String>;

/* The mode carries the type and permission bits in the POSIX st_mode layout. */
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

fn format_mode_string(u32 mode) throws -> String;

fn file_type_letter(u32 mode) wontthrow -> char;

/* The user name for a numeric uid and the group name for a numeric gid, read
   directly from /etc/passwd and /etc/group, so the static build stays free of
   getpwuid and getgrgid. */
fn uid_to_username(u32 uid) throws -> Maybe<String>;
fn gid_to_groupname(u32 gid) throws -> Maybe<String>;

fn sleep_for_seconds(double seconds) wontthrow -> void;

extern const ArrayList<String> OMITTED_SUFFIXES;

fn write_fd(os::descriptor fd, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>;
fn write_to_numbered_fd(i64 fd_number, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>;
fn read_fd(os::descriptor fd, opaque *buf, usize size) wontthrow
    -> Maybe<usize>;

fn wait_for_fd_readable(os::descriptor fd, i64 timeout_nanos) wontthrow -> i32;

fn close_fd(os::descriptor fd) wontthrow -> bool;

fn redirect_stdout(os::descriptor target) wontthrow -> os::descriptor;
fn restore_stdout(os::descriptor saved) wontthrow -> void;

struct saved_descriptor
{
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

fn save_and_replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow
    -> saved_descriptor;
fn save_descriptor(i32 shell_fd) wontthrow -> saved_descriptor;
fn restore_descriptor(const saved_descriptor &saved) wontthrow -> void;

fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor;

/* On POSIX the descriptor is the shell fd number, on Windows it is the handle
   that occupies the shell's standard-handle slot. */
fn descriptor_is_shell_fd(os::descriptor fd, i32 shell_fd) wontthrow -> bool;

/* On POSIX the number is the descriptor, and on Windows it maps to the C
   runtime handle. */
fn descriptor_from_fd_number(i64 fd_number) wontthrow -> os::descriptor;

fn replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow -> bool;

fn close_shell_fd(i32 shell_fd) wontthrow -> bool;

fn allocate_free_shell_fd(i32 floor_fd) wontthrow -> i32;

fn get_environment_variable(StringView key) throws -> Maybe<String>;
fn set_environment_variable(StringView key, StringView value) throws -> void;
fn unset_environment_variable(StringView key) throws -> void;

fn environment_names() throws -> ArrayList<String>;

fn is_child_process() wontthrow -> bool;

fn get_shell_process_id() wontthrow -> i64;

fn get_parent_process_id() wontthrow -> i64;
fn get_real_user_id() wontthrow -> i64;
fn get_effective_user_id() wontthrow -> i64;
fn get_real_group_id() wontthrow -> i64;

fn child_max() wontthrow -> i64;

fn machine_type() throws -> String;

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

/* The shell skips its startup config files in the setuid or setgid case, so a
   file an attacker controls cannot run with the raised privileges. */
fn is_running_setuid() wontthrow -> bool;

fn reopen_terminal_as_stdin() wontthrow -> bool;

fn process_id_of(process p) wontthrow -> i64;
fn process_has_id(process p, i64 id) wontthrow -> bool;

fn is_stdin_a_tty() wontthrow -> bool;
fn is_stdout_a_tty() wontthrow -> bool;
fn is_stderr_a_tty() wontthrow -> bool;
fn is_fd_a_tty(descriptor fd) wontthrow -> bool;
fn shell_fd_is_a_tty(int shell_fd) wontthrow -> bool;

pure fn is_directory_separator(char c) wontthrow -> bool;

fn make_fd_inheritable(descriptor fd) wontthrow -> void;

fn erase_extension_and_get_its_index(String &program_name) throws -> ext_index;

fn get_current_user() throws -> Maybe<String>;

fn get_hostname() throws -> Maybe<String>;

fn get_home_directory() throws -> Maybe<Path>;

fn get_home_for_user(StringView username) throws -> Maybe<Path>;

fn enumerate_users() throws -> ArrayList<String>;

/* The interactive shell blocks the terminal-generated signals, a
   non-interactive script leaves those at their default. SIGINT routes to the
   polled handler in both modes. */
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

/* A signal the startup blocked is unblocked here. */
fn set_trap_handler(i32 signal_number) throws -> void;

/* Install the ignore disposition for a signal, for a trap with an empty action
   such as trap "" INT, so the signal is discarded. */
fn set_trap_ignore(i32 signal_number) throws -> void;

/* Restore a signal's default disposition when its trap is removed. SIGINT
   returns to the shell's interrupt handler, every other signal to SIG_DFL. */
fn clear_trap_handler(i32 signal_number) throws -> void;

fn take_pending_signal() wontthrow -> i32;

fn monotonic_nanos() wontthrow -> u64;

fn realtime_microseconds() wontthrow -> u64;

/* A negative epoch renders the current time, the bash -1 and -2 forms. */
fn format_local_time(StringView format, i64 epoch) throws -> String;

fn terminal_size(u32 &columns, u32 &rows) wontthrow -> bool;

/* The user and system seconds this process's children have consumed so far,
   read from RUSAGE_CHILDREN. Windows has no equivalent and reports zero. */
fn children_cpu_seconds(double &user_seconds, double &system_seconds) wontthrow
    -> void;

fn children_peak_rss_bytes() wontthrow -> u64;

struct perf_counts
{
  u64 cpu_cycles{0};
  u64 instructions{0};
  u64 cache_references{0};
  u64 cache_misses{0};
  u64 branch_misses{0};
};

/* The perf counts are filled only when has_perf is true. */
struct measured_result
{
  u64 wall_nanos{0};
  u64 peak_rss_bytes{0};
  i64 exit_status{0};
  bool has_perf{false};
  perf_counts perf{};
};

fn run_measured(const ArrayList<String> &argv, bool suppress_output) throws
    -> Maybe<measured_result>;

/* allow_script_fallback lets a single foreground command report an ENOEXEC file
   to the caller through ExecFormatError. new_process_group puts the child in
   its own process group so it can own the controlling terminal. */
fn execute_program(ExecContext &&ec, bool allow_script_fallback = false,
                   bool new_process_group = false) throws -> process;

fn shell_has_controlling_terminal() wontthrow -> bool;

/* On Windows it returns the input unchanged. */
fn canonical_path(const Path &path) wontthrow -> Maybe<Path>;

/* On Windows the wildcard applies to the last path component the way
   FindFirstFile expands it. */
fn glob_matches(StringView pattern, Allocator allocator) throws
    -> ArrayList<String>;

/* Whether the directory is safe to run a binary from for its --help text, so it
   is owned by root or the current user and is not writable by group or other,
   rejecting a world-writable directory such as /tmp. On Windows it returns
   false. */
fn directory_is_trusted_for_exec(const Path &directory) wontthrow -> bool;

fn capture_program_output(const ArrayList<String> &argv,
                          u64 timeout_nanos) wontthrow -> Maybe<String>;

/* Ignores SIGTTOU across the change. A no-op without a controlling terminal. */
fn give_controlling_terminal_to(process p) wontthrow -> void;
fn reclaim_controlling_terminal() wontthrow -> void;

fn fork_compound_stage(Maybe<descriptor> in_fd, Maybe<descriptor> out_fd,
                       Maybe<descriptor> err_fd) throws -> process;

/* In the child it returns zero so the caller runs the job and exits. */
fn fork_job_process() throws -> process;

#if SHIT_PLATFORM_IS WIN32
fn spawn_subshell_stage(StringView source, Maybe<descriptor> in_fd,
                        Maybe<descriptor> out_fd, bool bash_compatible) throws
    -> Maybe<process>;
#endif

/* A forked pipeline-stage child calls this so it never runs the parent's
   cleanup inside the duplicated process. */
[[noreturn]] fn exit_process_immediately(i32 status) wontthrow -> void;

/* It does not fork, so on success it never returns. */
[[noreturn]] fn replace_process(ExecContext &&ec) throws -> void;

fn redirect_self(const ExecContext &ec) throws -> void;

} // namespace os

} // namespace shit
