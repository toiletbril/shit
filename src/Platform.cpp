#include "Platform.hpp"

#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

/* A forked pipeline stage that never execs would run the address-sanitizer leak
   check at its own exit, which warns or hangs under a seccomp sandbox that
   blocks ptrace. The parent process reports the real leaks, so the forked child
   turns the check off. The interface is only present in a sanitizer build. */
#if defined __SANITIZE_ADDRESS__
#include <sanitizer/lsan_interface.h>
#define SHIT_HAS_ADDRESS_SANITIZER 1
#endif

#if SHIT_PLATFORM_IS POSIX
#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>

/* TODO move this to toiletline */
#include <sys/ioctl.h>
#include <termios.h>

#if defined __linux__
#include <linux/perf_event.h>
#include <sys/syscall.h>
#endif

#if defined __APPLE__
#include <libproc.h>
#include <mach-o/dyld.h>
#include <sys/proc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#endif

/* posix_spawn takes the child environment as an envp argument. The shell passes
   its own process environment, which prefix assignments mutate before the spawn
   and restore after, so the declaration of environ is needed here. */
extern char **environ;
#endif

/* The async-signal flag machinery is identical on every platform, so it lives
   in one block the POSIX and the Windows sections below both share rather than
   defining it twice. */
namespace shit {
namespace os {

volatile sig_atomic_t INTERRUPT_REQUESTED = 0;
volatile sig_atomic_t CHILD_STATE_CHANGED = 0;
volatile sig_atomic_t SIGNAL_PENDING = 0;

/* One pending flag per signal number, set by the trap handler and cleared by
   the drain. The size covers every real-time and standard signal the platform
   defines, with a fixed bound so the array stays a flat block the handler may
   touch async-safely. */
static constexpr i32 SIGNAL_FLAG_COUNT = 128;
static volatile sig_atomic_t PENDING_SIGNAL_FLAGS[SIGNAL_FLAG_COUNT] = {};

/* Whether the signal number indexes the pending-flag array, so a trap call on
   an out-of-range number is a no-op rather than an out-of-bounds write. */
static fn is_trappable_signal(i32 signal_number) wontthrow -> bool
{
  return signal_number > 0 && signal_number < SIGNAL_FLAG_COUNT;
}

/* Report and clear the lowest pending per-signal flag, or zero when none is
   set. The fast SIGNAL_PENDING flag is owned by the drain, which clears it
   before consuming, so this only reports the per-signal flags. */
fn take_pending_signal() wontthrow -> i32
{
  for (i32 number = 1; number < SIGNAL_FLAG_COUNT; number++) {
    if (PENDING_SIGNAL_FLAGS[number] != 0) {
      PENDING_SIGNAL_FLAGS[number] = 0;
      return number;
    }
  }
  return 0;
}

} // namespace os
} // namespace shit

/* The file creation mask call differs only by name and argument type across
   platforms. One macro bridges the shared mask functions to it. */
#if SHIT_PLATFORM_IS WIN32
#define SHIT_UMASK(mask) _umask(static_cast<int>(mask))
#else
#define SHIT_UMASK(mask) umask(static_cast<mode_t>(mask))
#endif

#if SHIT_PLATFORM_IS POSIX

namespace shit {

namespace os {

hot fn write_fd(os::descriptor fd, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>
{
  loop
  {
    ssize_t w = write(fd, buf, size);
    /* A signal that lands mid-write interrupts the call before any byte is
       transferred. Retry instead of reporting a spurious write failure. */
    if (w == -1 && errno == EINTR) continue;
    if (w == -1) return shit::None;
    return static_cast<usize>(w);
  }
}

hot fn write_to_numbered_fd(i64 fd_number, const opaque *buf,
                            usize size) wontthrow -> Maybe<usize>
{
  return write_fd(static_cast<os::descriptor>(fd_number), buf, size);
}

hot fn read_fd(os::descriptor fd, opaque *buf, usize size) wontthrow
    -> Maybe<usize>
{
  loop
  {
    ssize_t r = read(fd, buf, size);
    /* A SIGINT sets INTERRUPT_REQUESTED and returns to the caller rather than
       retrying and freezing on a Ctrl-C. Any other interrupting signal retries
       so the reader does not mistake the interruption for end of input. */
    if (r == -1 && errno == EINTR) {
      if (INTERRUPT_REQUESTED) return shit::None;
      continue;
    }
    if (r == -1) return shit::None;
    return static_cast<usize>(r);
  }
}

hot fn wait_for_fd_readable(os::descriptor fd, i64 timeout_nanos) wontthrow
    -> i32
{
  const bool has_deadline = timeout_nanos > 0;
  const u64 deadline_nanos =
      has_deadline ? monotonic_nanos() + static_cast<u64>(timeout_nanos) : 0;
  loop
  {
    int timeout_millis = -1;
    if (timeout_nanos == 0) {
      timeout_millis = 0;
    } else if (has_deadline) {
      const u64 now_nanos = monotonic_nanos();
      if (now_nanos >= deadline_nanos) return 0;
      timeout_millis =
          static_cast<int>((deadline_nanos - now_nanos) / 1'000'000);
    }

    struct pollfd watch;
    watch.fd = fd;
    watch.events = POLLIN;
    watch.revents = 0;
    const int ready = poll(&watch, 1, timeout_millis);
    if (ready < 0) {
      if (errno == EINTR) {
        if (INTERRUPT_REQUESTED) return -1;
        continue;
      }
      return -1;
    }
    return ready > 0 ? 1 : 0;
  }
}

fn close_fd(os::descriptor fd) wontthrow -> bool { return close(fd) != -1; }

fn TempFileSet::track(Path path) throws -> void { unused(path); }
fn TempFileSet::count() const wontthrow -> usize { return 0; }
fn TempFileSet::cleanup_from(usize mark) wontthrow -> void { unused(mark); }

fn redirect_stdout(os::descriptor target) wontthrow -> os::descriptor
{
  /* The saved copy of the real stdout is close-on-exec so a forked command does
     not inherit it and hold the shell's own output open. */
  const os::descriptor saved = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
  dup2(target, STDOUT_FILENO);

  if (const int flags = fcntl(target, F_GETFD); flags != -1)
    fcntl(target, F_SETFD, flags | FD_CLOEXEC);

  return saved;
}

fn restore_stdout(os::descriptor saved) wontthrow -> void
{
  dup2(saved, STDOUT_FILENO);
  close(saved);
}

/* Shell-internal descriptor backups live at or above this number so a script
   addressing the single-digit descriptors never sees the shell's bookkeeping.
 */
constexpr int SHELL_BACKUP_FD_FLOOR = 10;

fn save_and_replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow
    -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;

  /* The backup is close-on-exec so a command spawned by the redirected child
     does not inherit the shell's own original descriptor and hold it open. */
  const os::descriptor backup =
      fcntl(shell_fd, F_DUPFD_CLOEXEC, SHELL_BACKUP_FD_FLOOR);
  result.was_open = backup != -1;
  result.saved = backup;

  /* A dup2 from a closed or invalid target descriptor fails, as in >&5 with fd
     5 closed. The caller reports the failure so the command fails rather than
     writing to the original descriptor. */
  result.is_dup2_ok = dup2(target, shell_fd) != -1;
  return result;
}

fn restore_descriptor(const saved_descriptor &saved) wontthrow -> void
{
  if (saved.was_open) {
    dup2(saved.saved, saved.shell_fd);
    close(saved.saved);
  } else {
    close(saved.shell_fd);
  }
}

fn save_descriptor(i32 shell_fd) wontthrow -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;
  /* The backup carries close-on-exec the same way save_and_replace_descriptor
     takes its backup, so a spawned command never inherits it, and it lands
     above the user range so an in-process script never sees it. */
  const os::descriptor backup =
      fcntl(shell_fd, F_DUPFD_CLOEXEC, SHELL_BACKUP_FD_FLOOR);
  result.was_open = backup != -1;
  result.saved = backup;
  result.is_dup2_ok = true;
  return result;
}

fn reopen_terminal_as_stdin() wontthrow -> bool
{
  const int tty_fd = open("/dev/tty", O_RDWR);
  if (tty_fd == -1) return false;
  LOG(Info, "reopening the controlling terminal onto fd 0");
  const bool replaced = dup2(tty_fd, STDIN_FILENO) != -1;
  close(tty_fd);
  return replaced && isatty(STDIN_FILENO) == 1;
}

fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor
{
  return shell_fd;
}

fn descriptor_from_fd_number(i64 fd_number) wontthrow -> os::descriptor
{
  return static_cast<os::descriptor>(fd_number);
}

fn replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow -> bool
{
  /* A self copy such as exec 1>&1 skips the dup2 onto itself to avoid clearing
     the close-on-exec the caller may rely on. */
  if (target == shell_fd) return true;
  return dup2(target, shell_fd) != -1;
}

fn close_shell_fd(i32 shell_fd) wontthrow -> bool
{
  return close(shell_fd) != -1;
}

fn allocate_free_shell_fd(i32 floor_fd) wontthrow -> i32
{
  const i32 probe_sources[] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
  for (let const source : probe_sources) {
    const int allocated = fcntl(source, F_DUPFD_CLOEXEC, floor_fd);
    if (allocated != -1) {
      close(allocated);
      return allocated;
    }
  }

  return -1;
}

static fn passwd_field(StringView line, usize index) wontthrow -> StringView;

fn get_current_user() throws -> Maybe<String>
{
  /* The name comes from the environment rather than getpwuid, which a static
     build cannot call without pulling in the runtime glibc NSS modules. */
  if (const char *name = std::getenv("LOGNAME"); name != nullptr)
    return String{name};
  if (const char *name = std::getenv("USER"); name != nullptr)
    return String{name};

  /* A container that exports neither leaves the environment bare, so the name
     is read from /etc/passwd by the current uid. getuid is a plain syscall, so
     the static build avoids the NSS modules getpwuid would pull in. */
  let const contents = Path{StringView{"/etc/passwd"}}.read_entire_file();
  if (!contents) return shit::None;
  let const wanted_uid = utils::uint_to_text(static_cast<u64>(getuid()));
  let const text = contents->view();
  for (let const &line : utils::split_lines(text)) {
    if (passwd_field(line, 2) != wanted_uid.view()) continue;
    let const name = passwd_field(line, 0);
    if (!name.is_empty()) return String{name};
  }
  return shit::None;
}

fn get_hostname() throws -> Maybe<String>
{
  /* HOST_NAME_MAX is not portable, so a fixed buffer holds the name and a
     trailing NUL guards a truncated result some libcs leave unterminated. */
  char buffer[256];
  if (gethostname(buffer, sizeof(buffer)) != 0) return shit::None;
  buffer[sizeof(buffer) - 1] = '\0';

  return String{buffer};
}

fn get_home_directory() throws -> Maybe<Path>
{
  if (let const home = get_environment_variable("HOME"); home.has_value())
    return Path{StringView{*home}};
  return shit::None;
}

/* The colon field at index of an /etc/passwd line, empty when the line has too
   few fields. The format is name:passwd:uid:gid:gecos:home:shell. The database
   is read directly rather than through getpwnam, which a static build cannot
   call without the glibc NSS modules. A user defined only through NSS is not
   seen, the accepted tradeoff for the static build. */
static fn passwd_field(StringView line, usize index) wontthrow -> StringView
{
  usize field_start_position = 0;
  usize field_index = 0;
  for (usize i = 0; i <= line.length; i++) {
    if (i != line.length && line[i] != ':') continue;
    if (field_index == index)
      return line.substring_of_length(field_start_position,
                                      i - field_start_position);
    field_index++;
    field_start_position = i + 1;
  }
  return StringView{};
}

fn get_home_for_user(StringView username) throws -> Maybe<Path>
{
  if (username.is_empty()) return shit::None;

  let const contents = Path{StringView{"/etc/passwd"}}.read_entire_file();
  if (!contents) return shit::None;

  let const text = contents->view();
  for (let const &line : utils::split_lines(text)) {
    if (passwd_field(line, 0) != username) continue;
    let const home_field = passwd_field(line, 5);
    if (home_field.is_empty()) return shit::None;
    return Path{home_field};
  }
  return shit::None;
}

fn enumerate_users() throws -> ArrayList<String>
{
  ArrayList<String> users{};

  let const contents = Path{StringView{"/etc/passwd"}}.read_entire_file();
  if (!contents) return users;

  let const text = contents->view();
  for (let const &line : utils::split_lines(text)) {
    let const name = passwd_field(line, 0);
    if (!name.is_empty()) users.push(String{name});
  }
  return users;
}

static const pid_t PARENT_SHELL_PID = getpid();

fn is_child_process() wontthrow -> bool { return getpid() != PARENT_SHELL_PID; }

fn is_running_setuid() wontthrow -> bool
{
  return geteuid() != getuid() || getegid() != getgid();
}

fn process_id_of(process p) wontthrow -> i64 { return static_cast<i64>(p); }

fn is_stdin_a_tty() wontthrow -> bool { return isatty(SHIT_STDIN); }

fn is_stdout_a_tty() wontthrow -> bool { return isatty(SHIT_STDOUT); }

fn is_stderr_a_tty() wontthrow -> bool { return isatty(SHIT_STDERR); }
fn is_fd_a_tty(descriptor fd) wontthrow -> bool { return isatty(fd); }

fn terminal_size(u32 &columns, u32 &rows) wontthrow -> bool
{
  LOG(Debug, "querying the terminal size");
  struct winsize window{};
  if (ioctl(SHIT_STDOUT, TIOCGWINSZ, &window) != 0) return false;
  if (window.ws_col == 0 || window.ws_row == 0) return false;
  columns = window.ws_col;
  rows = window.ws_row;
  return true;
}

fn make_fd_inheritable(descriptor fd) wontthrow -> void
{
  const int flags = fcntl(fd, F_GETFD);
  if (flags != -1) fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
}

/* Cosmopolitan binaries can be run on both Linux and Windows. This will be
 * replaced by a runtime check. */
#if SHIT_PLATFORM_ISNT COSMO
const ArrayList<String> OMITTED_SUFFIXES = []() {
  ArrayList<String> suffixes{};
  suffixes.push(String{});
  return suffixes;
}();

fn erase_extension_and_get_its_index(String &program_name) throws -> ext_index
{
  unused(program_name);
  return false;
}
#endif /* !COSMO */

fn get_environment_variable(StringView key) throws -> Maybe<String>
{
  LOG(All, "reading the environment variable '%.*s'",
      static_cast<int>(key.length), key.data);
  const String key_string{key};
  const char *e = std::getenv(key_string.c_str());
  if (e != nullptr) return String{e};
  return shit::None;
}

fn set_environment_variable(StringView key, StringView value) throws -> void
{
  LOG(All, "setting the environment variable '%.*s'",
      static_cast<int>(key.length), key.data);
  const String key_string{key};
  const String value_string{value};
  setenv(key_string.c_str(), value_string.c_str(), 1);
}

fn unset_environment_variable(StringView key) throws -> void
{
  LOG(All, "unsetting the environment variable '%.*s'",
      static_cast<int>(key.length), key.data);
  const String key_string{key};
  unsetenv(key_string.c_str());
}

fn environment_names() throws -> ArrayList<String>
{
  ArrayList<String> names{};
  if (environ == nullptr) return names;
  for (char **entry = environ; *entry != nullptr; entry++) {
    StringView pair{*entry};
    let const equals = pair.find_character('=');
    /* An entry with no '=' is kept whole, since the name is the entry. */
    let const name =
        equals.has_value() ? pair.substring_of_length(0, *equals) : pair;
    names.push(String{name});
  }
  return names;
}

fn check_syscall_impl(i32 status, StringView invocation) throws -> i32
{
  if (status == -1) {
    throw shit::Error{"'" + invocation +
                      "' fail: " + last_system_error_message()};
  }

  return status;
}

#define check_syscall(call) check_syscall_impl(call, #call)

/* posix_spawn reports an exec failure through its return value, so there is no
   spawned process to wait on. A trivial child is forked that only reports the
   error and exits, giving the caller the same waitable pid and status the fork
   path produced. The error came from posix_spawn rather than errno, so it is
   passed in. */
cold fn spawn_failure_child(const Path &program_path, int spawn_error) throws
    -> process
{
  LOG(Debug, "forking a child to report the spawn failure for '%s'",
      program_path.c_str());

  const pid_t child_pid = check_syscall(fork());

  if (child_pid == 0) {
    String msg{};
    msg += program_path.text();
    msg += ": ";
    msg += String{strerror(spawn_error)};
    msg += '\n';
    (void) write_fd(STDERR_FILENO, msg.data(), msg.count());
    /* The program was resolved but could not be executed, so bash exits 126,
       reserving 127 for a missing file, as when a shebang names an interpreter
       that does not exist. */
    _exit(spawn_error == ENOENT ? 127 : 126);
  }

  return child_pid;
}

hot fn execute_program(ExecContext &&ec, bool allow_script_fallback,
                       bool new_process_group) throws -> process
{
  ASSERT(ec.args().count() > 0, "a program needs at least argv[0]");

  LOG(Debug, "spawning '%s' with %zu arguments", ec.program_path().c_str(),
      ec.args().count());

  /* On the ENOEXEC fallback the context's descriptors are handed to the script
     run, which reapplies the command's redirections, so they are not closed
     here. */
  bool was_fds_handed_to_fallback = false;
  defer
  {
    if (!was_fds_handed_to_fallback) ec.close_fds();
  };

  let const child_args = make_os_args(ec.args());

  /* glibc backs posix_spawn with clone(CLONE_VM | CLONE_VFORK), so the child
     shares the parent address space until exec and pays no page-table copy. The
     redirections become file actions and the signal reset becomes spawn
     attributes, so the parent never enters a duplicated evaluator. */
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  defer { posix_spawn_file_actions_destroy(&file_actions); };

  /* Each redirect is placed onto its standard descriptor and the original is
     closed. A descriptor already on its target slot is left in place, since the
     dup2 onto itself is a no-op and the close would shut the live descriptor.
   */
  if (ec.in_fd && *ec.in_fd != STDIN_FILENO) {
    posix_spawn_file_actions_adddup2(&file_actions, *ec.in_fd, STDIN_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, *ec.in_fd);
  }
  if (ec.out_fd && *ec.out_fd != STDOUT_FILENO) {
    posix_spawn_file_actions_adddup2(&file_actions, *ec.out_fd, STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, *ec.out_fd);
  }
  if (ec.err_fd && *ec.err_fd != STDERR_FILENO) {
    posix_spawn_file_actions_adddup2(&file_actions, *ec.err_fd, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, *ec.err_fd);
  }
  /* The descriptor duplications come after the files are placed, so 2>&1 sees
     the final standard output. Each dup reads the current target of its source,
     so the source order decides the result of a mixed 2>&1 1>&2. */
  ec.apply_dup_routing(
      [&]() {
        posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO,
                                         STDERR_FILENO);
      },
      [&]() {
        posix_spawn_file_actions_adddup2(&file_actions, STDERR_FILENO,
                                         STDOUT_FILENO);
      });

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  defer { posix_spawnattr_destroy(&attr); };

  /* Without a reset the exec'd program would inherit the parent's blocked set
     and dispositions. An empty mask unblocks everything the parent blocked and
     the default set restores SIG_DFL, so a foreground command still dies on
     Ctrl-C and a pipe producer still dies on SIGPIPE. */
  sigset_t empty_mask;
  sigemptyset(&empty_mask);
  posix_spawnattr_setsigmask(&attr, &empty_mask);

  sigset_t default_signals;
  sigemptyset(&default_signals);
  sigaddset(&default_signals, SIGINT);
  sigaddset(&default_signals, SIGCHLD);
  /* posix_spawn keeps the parent disposition for a signal absent from the
     default set. SIGPIPE is added so an external pipe producer dies on a closed
     reader rather than inheriting the shell's ignore. */
  sigaddset(&default_signals, SIGPIPE);
  posix_spawnattr_setsigdefault(&attr, &default_signals);

  short spawn_flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
  /* A foreground interactive command runs in its own process group so it can
     own the controlling terminal. pgid zero makes the child its own group
     leader. */
  if (new_process_group) {
    posix_spawnattr_setpgroup(&attr, 0);
    spawn_flags |= POSIX_SPAWN_SETPGROUP;
  }
  posix_spawnattr_setflags(&attr, spawn_flags);

  pid_t child_pid = 0;
  /* The shell environment is passed as envp, so a prefix assignment the parent
     committed before this call reaches the child. An exec -c in a subshell or a
     pipeline stage instead passes a single null, so the child starts empty. */
  char *const empty_environment[] = {nullptr};
  const int spawn_error =
      posix_spawn(&child_pid, ec.program_path().c_str(), &file_actions, &attr,
                  const_cast<char *const *>(child_args.begin()),
                  ec.should_use_empty_environment
                      ? const_cast<char *const *>(empty_environment)
                      : environ);

  /* An ENOEXEC file is executable but carries no shebang and is not a binary.
     It runs as a shell script in place, the POSIX behavior, rather than failing
     127. The check runs before the descriptors are closed, so the script run
     still sees the command's redirections. */
  if (spawn_error == ENOEXEC && allow_script_fallback) {
    was_fds_handed_to_fallback = true;
    throw shit::ExecFormatError{};
  }

  ec.close_fds();

  if (spawn_error != 0)
    return spawn_failure_child(ec.program_path(), spawn_error);

  return child_pid;
}

fn shell_has_controlling_terminal() wontthrow -> bool
{
  return isatty(STDIN_FILENO) == 1;
}

fn canonical_path(const Path &path) wontthrow -> Maybe<Path>
{
  /* A null buffer makes realpath allocate one of the right size, freed here
     after the view is copied into the returned Path. */
  char *resolved_path = realpath(path.c_str(), nullptr);
  if (resolved_path == nullptr) return None;
  Path result{StringView{resolved_path}};
  free(resolved_path);
  return result;
}

fn glob_matches(StringView pattern, Allocator allocator) throws
    -> ArrayList<String>
{
  let matches = ArrayList<String>{allocator};

  const String pattern_string{pattern};
  glob_t glob_result{};
  if (glob(pattern_string.c_str(), 0, nullptr, &glob_result) == 0) {
    for (usize i = 0; i < glob_result.gl_pathc; i++)
      matches.push(String{allocator, StringView{glob_result.gl_pathv[i]}});
  }
  globfree(&glob_result);

  return matches;
}

fn directory_is_trusted_for_exec(const Path &directory) wontthrow -> bool
{
  struct stat directory_stat;
  if (stat(directory.c_str(), &directory_stat) != 0) return false;
  /* Only a directory the current user or root owns is trusted, since another
     user could swap a binary into a directory they own. */
  const bool owner_is_trusted =
      directory_stat.st_uid == 0 || directory_stat.st_uid == geteuid();
  /* A directory writable by group or other lets someone other than the owner
     drop a binary in, so it is rejected even when the owner is trusted. */
  const bool others_cannot_write =
      (directory_stat.st_mode & (S_IWGRP | S_IWOTH)) == 0;
  return owner_is_trusted && others_cannot_write;
}

fn capture_program_output(const ArrayList<String> &argv,
                          u64 timeout_nanos) wontthrow -> Maybe<String>
{
  if (argv.is_empty()) return None;

  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) return None;
  const int read_end = pipe_fds[0];
  const int write_end = pipe_fds[1];

  const int devnull_fd = open("/dev/null", O_RDONLY);
  if (devnull_fd < 0) {
    close(read_end);
    close(write_end);
    return None;
  }

  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  posix_spawn_file_actions_adddup2(&file_actions, devnull_fd, STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&file_actions, write_end, STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&file_actions, write_end, STDERR_FILENO);
  posix_spawn_file_actions_addclose(&file_actions, read_end);
  posix_spawn_file_actions_addclose(&file_actions, write_end);
  posix_spawn_file_actions_addclose(&file_actions, devnull_fd);

  /* The argv strings outlive the spawn, so the NUL-terminated array points at
     their storage rather than copying. */
  ArrayList<char *> raw_args{heap_allocator()};
  for (let const &argument : argv)
    raw_args.push(const_cast<char *>(argument.c_str()));
  raw_args.push(nullptr);

  /* The shell ignores SIGPIPE. The spawn restores the default in the child, so
     a child that keeps writing after the read end closes on the timeout dies on
     SIGPIPE rather than seeing EPIPE. */
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  sigset_t default_signals;
  sigemptyset(&default_signals);
  sigaddset(&default_signals, SIGPIPE);
  posix_spawnattr_setsigdefault(&attr, &default_signals);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

  pid_t child_pid = 0;
  const int spawn_result = posix_spawn(&child_pid, raw_args[0], &file_actions,
                                       &attr, raw_args.begin(), environ);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&file_actions);
  close(write_end);
  close(devnull_fd);
  if (spawn_result != 0) {
    close(read_end);
    return None;
  }

  /* A child that stalls is cut at the deadline rather than blocking the prompt.
   */
  let captured = String{heap_allocator()};
  const u64 deadline_nanos = monotonic_nanos() + timeout_nanos;
  bool was_timed_out = false;
  loop
  {
    const u64 now_nanos = monotonic_nanos();
    if (now_nanos >= deadline_nanos) {
      was_timed_out = true;
      break;
    }
    int remaining_millis =
        static_cast<int>((deadline_nanos - now_nanos) / 1'000'000);
    if (remaining_millis <= 0) remaining_millis = 1;

    struct pollfd watch;
    watch.fd = read_end;
    watch.events = POLLIN;
    watch.revents = 0;
    const int ready = poll(&watch, 1, remaining_millis);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) {
      was_timed_out = true;
      break;
    }

    char buffer[4096];
    const ssize_t read_count = read(read_end, buffer, sizeof(buffer));
    if (read_count < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (read_count == 0) break;
    captured.append(StringView{buffer, static_cast<usize>(read_count)});
  }
  close(read_end);

  /* A child still running at the deadline is killed so it leaves no zombie, and
     either way it is reaped. */
  if (was_timed_out) signal_process(child_pid, SIGKILL);
  int wait_status = 0;
  waitpid(child_pid, &wait_status, 0);

  if (was_timed_out) return None;
  return captured;
}

fn give_controlling_terminal_to(process p) wontthrow -> void
{
  if (!shell_has_controlling_terminal()) return;
  /* Handing the terminal away would raise SIGTTOU on this write, so it is
     ignored across the change and the previous disposition restored. A failed
     tcsetpgrp is left alone since the shell reclaims next. */
  void (*const previous)(int) = signal(SIGTTOU, SIG_IGN);
  tcsetpgrp(STDIN_FILENO, p);
  signal(SIGTTOU, previous);
}

fn reclaim_controlling_terminal() wontthrow -> void
{
  if (!shell_has_controlling_terminal()) return;
  void (*const previous)(int) = signal(SIGTTOU, SIG_IGN);
  tcsetpgrp(STDIN_FILENO, getpgrp());
  signal(SIGTTOU, previous);
}

fn fork_compound_stage(Maybe<descriptor> in_fd, Maybe<descriptor> out_fd,
                       Maybe<descriptor> err_fd) throws -> process
{
  LOG(Debug, "forking a compound pipeline stage");

  const pid_t child_pid = check_syscall(fork());

  if (child_pid == 0) {
    /* A throw here would unwind into the parent's evaluator inside the
       duplicated process and run its cleanup. The child must terminate
       directly, so a descriptor-setup failure is caught, reported, and exits.
     */
    try {
      if (in_fd) {
        check_syscall(dup2(*in_fd, STDIN_FILENO));
        check_syscall(close(*in_fd));
      }
      if (out_fd) {
        check_syscall(dup2(*out_fd, STDOUT_FILENO));
        check_syscall(close(*out_fd));
      }
      if (err_fd) {
        check_syscall(dup2(*err_fd, STDERR_FILENO));
        check_syscall(close(*err_fd));
      }

      reset_signal_handlers();

#if defined SHIT_HAS_ADDRESS_SANITIZER
      /* The forked stage exits through _exit, so the leak check should not run
         here, and a sandbox may trap the sanitizer's tracer regardless. */
      __lsan_disable();
#endif
    } catch (const shit::Error &e) {
      String msg = e.message();
      msg += '\n';
      (void) write_fd(STDERR_FILENO, msg.data(), msg.count());
      exit_process_immediately(1);
    } catch (...) {
      LOG(Debug,
          "swallowed an unknown error while preparing the forked stage child");
      exit_process_immediately(1);
    }
  }

  return child_pid;
}

fn fork_job_process() throws -> process
{
  LOG(Debug, "forking a mimicked job into its own process group");

  const pid_t child_pid = check_syscall(fork());

  if (child_pid == 0) {
    try {
      reset_signal_handlers();
      (void) setpgid(0, 0);

#if defined SHIT_HAS_ADDRESS_SANITIZER
      __lsan_disable();
#endif
    } catch (...) {
      exit_process_immediately(1);
    }
    return 0;
  }

  (void) setpgid(child_pid, child_pid);
  return child_pid;
}

[[noreturn]] fn exit_process_immediately(i32 status) wontthrow -> void
{
  _exit(status);
}

fn replace_process(ExecContext &&ec) throws -> void
{
  ASSERT(ec.args().count() > 0, "a program needs at least argv[0]");

  LOG(Debug, "replacing the shell process with '%s'",
      ec.program_path().c_str());

  let const child_args = make_os_args(ec.args());

  /* Place each redirected file and close the original descriptor, the way the
     forked child does, so the opened file does not leak into the exec'd
     program. */
  if (ec.in_fd) {
    check_syscall(dup2(*ec.in_fd, STDIN_FILENO));
    if (*ec.in_fd != STDIN_FILENO) check_syscall(close(*ec.in_fd));
  }
  if (ec.out_fd) {
    check_syscall(dup2(*ec.out_fd, STDOUT_FILENO));
    if (*ec.out_fd != STDOUT_FILENO) check_syscall(close(*ec.out_fd));
  }
  if (ec.err_fd) {
    check_syscall(dup2(*ec.err_fd, STDERR_FILENO));
    if (*ec.err_fd != STDERR_FILENO) check_syscall(close(*ec.err_fd));
  }
  ec.apply_dup_routing(
      [&]() { check_syscall(dup2(STDOUT_FILENO, STDERR_FILENO)); },
      [&]() { check_syscall(dup2(STDERR_FILENO, STDOUT_FILENO)); });

  reset_signal_handlers();

  /* exec -c replaces the inherited environ with a single null, so the program
     starts with an empty environment. execve takes the envp explicitly where
     execv would have read environ. */
  char *const empty_environment[] = {nullptr};
  execve(ec.program_path().c_str(),
         const_cast<char *const *>(child_args.begin()),
         ec.should_use_empty_environment
             ? const_cast<char *const *>(empty_environment)
             : environ);

  /* execv returns only on failure. ENOEXEC means the file carries no shebang
     and is not a binary, so it runs as a shell script instead, signalled by
     ExecFormatError. */
  if (errno == ENOEXEC) throw shit::ExecFormatError{};
  /* The error carries the command's location for a caret and the caller exits
     126 the way bash does. The reason is read before the String concatenation,
     since building the message allocates and could clobber the errno that
     last_system_error_message reads. */
  let const reason = last_system_error_message();
  throw shit::ErrorWithLocation{
      ec.source_location(),
      "Unable to execute '" + ec.program_path().text() + "' because " + reason};
}

fn redirect_self(const ExecContext &ec) throws -> void
{
  if (ec.in_fd) check_syscall(dup2(*ec.in_fd, STDIN_FILENO));
  if (ec.out_fd) check_syscall(dup2(*ec.out_fd, STDOUT_FILENO));
  if (ec.err_fd) check_syscall(dup2(*ec.err_fd, STDERR_FILENO));
}

fn make_pipe() wontthrow -> Maybe<Pipe>
{
  LOG(Debug, "opening a close-on-exec pipe");

  descriptor p[2] = {SHIT_INVALID_FD, SHIT_INVALID_FD};

  if (pipe(p) != 0) {
    return shit::None;
  }

  /* Close the pipe ends on exec, so a stage that dups one end does not inherit
     the other and a producer never sees the pipe close. The dup2 onto a
     standard descriptor clears the flag there, so the redirection survives. */
  for (descriptor end : p) {
    const int flags = fcntl(end, F_GETFD);
    if (flags != -1) fcntl(end, F_SETFD, flags | FD_CLOEXEC);
  }

  return Pipe{p[0], p[1]};
}

/* pthread_create wants a void *(*)(void *) entry, so this trampoline carries
   the C-style entry and its context across that signature and returns nullptr.
 */
struct thread_start_context
{
  void (*entry)(opaque *);
  opaque *context;
};

fn thread_trampoline(opaque *raw_context) wontthrow -> opaque *
{
  let const start = static_cast<thread_start_context *>(raw_context);
  let const entry = start->entry;
  let const context = start->context;
  delete start;
  entry(context);
  return nullptr;
}

fn start_thread(void (*entry)(opaque *), opaque *context) wontthrow
    -> Maybe<thread>
{
  let const start = new thread_start_context{entry, context};
  pthread_t handle{};
  if (pthread_create(&handle, nullptr, thread_trampoline, start) != 0) {
    delete start;
    return shit::None;
  }
  return thread{handle};
}

fn join_thread(thread t) wontthrow -> void { pthread_join(t.handle, nullptr); }

fn open_file_descriptor(StringView path, file_open_mode mode) throws
    -> Maybe<descriptor>
{
  LOG(Debug, "opening '%.*s'", static_cast<int>(path.length), path.data);

  /* The descriptor is left inheritable rather than O_CLOEXEC, since a
     redirection such as exec 3>file keeps the descriptor open across an exec
     for a later command to write, and the high-descriptor redirection suite
     relies on it. */
  int flags = 0;
  switch (mode) {
  case file_open_mode::Truncate: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
  case file_open_mode::TruncateNoClobber:
    /* O_EXCL makes the create fail atomically when the file already exists, the
       way noclobber requires. */
    flags = O_WRONLY | O_CREAT | O_EXCL;
    break;
  case file_open_mode::Append: flags = O_WRONLY | O_CREAT | O_APPEND; break;
  case file_open_mode::Read: flags = O_RDONLY; break;
  case file_open_mode::ReadWrite: flags = O_RDWR | O_CREAT; break;
  }

  /* ::open needs a null-terminated path, so the view is copied into a String
     that owns a trailing null. */
  const String path_string{path};
  /* 0666 lets the umask decide the final permissions, as a shell redirection
     does. */
  const int fd = ::open(path_string.c_str(), flags, 0666);
  if (fd < 0) return shit::None;
  return fd;
}

fn write_to_temp_file(StringView content) throws -> Maybe<descriptor>
{
  LOG(Debug, "writing %zu bytes into an anonymous temp file", content.count());

  /* The temp directory is resolved at runtime rather than hardcoded to /tmp,
     so a cosmo binary running on Windows writes to the Windows temp directory
     where /tmp does not exist. */
  let const temp_dir = Path::temp_directory();

  let const path_template_path =
      PathBuilder{temp_dir.text()}.append("shit_heredoc_XXXXXX").build();

  /* mkstemp rewrites the XXXXXX suffix in place, so the template lives in a
     mutable buffer with a trailing null rather than the immutable Path text. */
  const String &path_template_text = path_template_path.text();
  ArrayList<char> path_template{};
  path_template.reserve(path_template_text.count() + 1);
  for (usize i = 0; i < path_template_text.count(); i++)
    path_template.push(path_template_text.c_str()[i]);
  path_template.push('\0');

  const int fd = mkstemp(path_template.begin());
  if (fd < 0) return shit::None;

  /* Unlink at once, so the file is anonymous and is freed when closed. */
  unlink(path_template.begin());

  usize offset = 0;
  while (offset < content.count()) {
    ssize_t written =
        ::write(fd, content.data + offset, content.count() - offset);
    /* A signal interrupted the write before any byte landed. Retry instead of
       dropping the heredoc, matching the EINTR loops in read_fd and write_fd.
       The handlers carry no SA_RESTART, so a SIGCHLD or a SIGINT mid-write can
       surface here. */
    if (written < 0 && errno == EINTR) continue;
    if (written < 0) {
      close(fd);
      return shit::None;
    }
    offset += static_cast<usize>(written);
  }

  /* The descriptor is rewound so the caller reads the bytes just written. A
     failed rewind would leave it at the end, so it closes and reports None
     rather than handing back an fd positioned past the data. */
  if (lseek(fd, 0, SEEK_SET) < 0) {
    close(fd);
    return shit::None;
  }
  return fd;
}

fn wait_and_monitor_process(process pid, bool *was_stopped) throws -> i32
{
  ASSERT(pid >= 0);

  LOG(Debug, "waiting on process %lld", static_cast<long long>(pid));

  i32 status{};
  const int wait_flags = was_stopped != nullptr ? WUNTRACED : 0;

  loop
  {
    pid_t w = waitpid(pid, &status, wait_flags);
    /* A signal interrupted the wait. Retry instead of failing. */
    if (w == -1 && errno == EINTR) continue;
    if (check_syscall(w) == pid) break;
  }

  if (was_stopped != nullptr && WIFSTOPPED(status)) {
    *was_stopped = true;
    return 128 + WSTOPSIG(status);
  }

  if (WIFSIGNALED(status)) {
    const i32 sig = WTERMSIG(status);
    const char *sig_str = strsignal(sig);
    const String sig_desc =
        (sig_str != nullptr) ? String{sig_str} : String{"Unknown"};

    /* A broken pipe is the normal way a producer ends once its consumer stops
       reading, such as seq into head, so it is reaped silently the way bash and
       dash do rather than reported as a job event. Ctrl-C prints a bare
       newline, and every other signal prints the located process message. */
    if (sig == SIGPIPE) {
      /* Reaped silently, see above. */
    } else if (sig != SIGINT) {
      shit::print("[Process " + utils::int_to_text(pid) + ": " + sig_desc +
                  ", signal " + utils::int_to_text(sig) + "]\n");
    } else {
      shit::print("\n");
    }

    return 128 + sig;
  } else if (!WIFEXITED(status)) {
    /* The process neither exited nor was signalled, so its status is unknown.
     */
    throw shit::Error{"???: " + last_system_error_message()};
  } else {
    return WEXITSTATUS(status);
  }
}

fn reap_process_quietly(process pid) throws -> i32
{
  ASSERT(pid >= 0);

  LOG(Debug, "quietly reaping process %lld", static_cast<long long>(pid));

  i32 status{};
  loop
  {
    const pid_t w = waitpid(pid, &status, 0);
    if (w == -1 && errno == EINTR) continue;
    /* The shell reaps a child through its SIGCHLD handling, so the child may be
       gone by the time this runs. A missing child is the goal, not a failure.
     */
    if (w == -1 && errno == ECHILD) return 0;
    if (check_syscall(w) == pid) break;
  }

  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return 1;
}

fn poll_process(process p, i32 &status_out) wontthrow -> process_state
{
  i32 status = 0;
  const pid_t result = waitpid(p, &status, WNOHANG | WUNTRACED | WCONTINUED);

  /* No new transition since the last poll, so the caller keeps the recorded
     state, which preserves a stop the foreground wait already consumed. */
  if (result == 0) return process_state::Unchanged;
  if (result == -1) {
    status_out = 0;
    return process_state::Exited;
  }

  if (WIFSTOPPED(status)) return process_state::Stopped;
  if (WIFCONTINUED(status)) return process_state::Running;
  if (WIFSIGNALED(status)) {
    status_out = 128 + WTERMSIG(status);
    return process_state::Exited;
  }
  status_out = WEXITSTATUS(status);
  return process_state::Exited;
}

fn signal_process(process p, i32 signal_number) wontthrow -> bool
{
  return kill(p, signal_number) == 0;
}

fn process_from_pid(i64 pid) wontthrow -> process
{
  return static_cast<process>(pid);
}

fn signal_number_from_name(StringView name) throws -> Maybe<i32>
{
  if (name.is_all_decimal_digits()) {
    const ErrorOr<i64> parsed_value = utils::parse_decimal_integer(name);
    if (parsed_value.is_error()) return shit::None;
    return static_cast<i32>(parsed_value.value());
  }

  let const bare = utils::strip_sig_prefix(name);

  static constexpr StaticStringMap<i32>::entry NAME_ENTRIES[] = {
      {SSK("HUP"),  SIGHUP },
      {SSK("INT"),  SIGINT },
      {SSK("QUIT"), SIGQUIT},
      {SSK("KILL"), SIGKILL},
      {SSK("TERM"), SIGTERM},
      {SSK("STOP"), SIGSTOP},
      {SSK("TSTP"), SIGTSTP},
      {SSK("CONT"), SIGCONT},
      {SSK("USR1"), SIGUSR1},
      {SSK("USR2"), SIGUSR2},
      {SSK("ABRT"), SIGABRT},
      {SSK("ALRM"), SIGALRM},
      {SSK("PIPE"), SIGPIPE},
  };
  static constexpr StaticStringMap<i32> NAMES{
      NAME_ENTRIES, sizeof(NAME_ENTRIES) / sizeof(NAME_ENTRIES[0])};
  return NAMES.find(bare);
}

/* The number-name pairs mirror signal_number_from_name's table, so a number
   and the name it carries round-trip through the helpers, and the completion
   engine offers exactly the accepted spellings. The list is short, so a
   linear scan beats a second map. */
struct signal_pair
{
  i32 number;
  StringView name;
};
static const signal_pair SIGNAL_PAIRS[] = {
    {SIGHUP,  "HUP" },
    {SIGINT,  "INT" },
    {SIGQUIT, "QUIT"},
    {SIGKILL, "KILL"},
    {SIGTERM, "TERM"},
    {SIGSTOP, "STOP"},
    {SIGTSTP, "TSTP"},
    {SIGCONT, "CONT"},
    {SIGUSR1, "USR1"},
    {SIGUSR2, "USR2"},
    {SIGABRT, "ABRT"},
    {SIGALRM, "ALRM"},
    {SIGPIPE, "PIPE"},
};

fn signal_name_from_number(i32 number) throws -> Maybe<String>
{
  for (let const &pair : SIGNAL_PAIRS)
    if (pair.number == number) return String{pair.name};
  return shit::None;
}

fn signal_names() throws -> const ArrayList<StringView> &
{
  static ArrayList<StringView> names = [] throws {
    let collected = ArrayList<StringView>{};
    collected.reserve(sizeof(SIGNAL_PAIRS) / sizeof(SIGNAL_PAIRS[0]));
    for (let const &pair : SIGNAL_PAIRS)
      collected.push(pair.name);
    return collected;
  }();
  return names;
}

hot fn make_os_args(const ArrayList<String> &args) throws -> os_args
{
  ASSERT(args.count() > 0, "argv must carry at least the program name");

  os_args result{};
  result.reserve(args.count() + 1);

  for (let const &arg : args)
    result.push(arg.c_str());

  result.push(nullptr);

  return result;
}

cold fn last_system_error_message() throws -> String
{
  return String{strerror(errno)};
}

static fn make_sigset_impl(int first, ...) wontthrow -> sigset_t
{
  va_list va;

  sigset_t sm;
  sigemptyset(&sm);

  va_start(va, first);
  for (int sig = first; sig != -1; sig = va_arg(va, int))
    sigaddset(&sm, sig);
  va_end(va);

  return sm;
}

#define make_sigset(...) make_sigset_impl(__VA_ARGS__, -1)

static fn sigchild_handler(int n, siginfo_t *siginfo, opaque *ctx) wontthrow
    -> void
{
  unused(n);
  unused(ctx);
  unused(siginfo);
  /* The flag store is the only async-signal-safe action. The handlers carry
     no SA_RESTART, so the editor's poll wakes with EINTR and its wake hook
     reads this for the set -b notification. */
  CHILD_STATE_CHANGED = 1;
}

fn reset_signal_handlers() throws -> void
{
  LOG(Debug, "restoring the default signal dispositions");

  sigset_t sm;
  sigfillset(&sm);
  check_syscall(sigprocmask(SIG_UNBLOCK, &sm, nullptr));

  struct sigaction sa = {};
  sa.sa_handler = SIG_DFL;
  check_syscall(sigaction(SIGCHLD, &sa, nullptr));

  /* A forked command or pipeline producer must die on a broken pipe rather than
     see its write fail with EPIPE, so it exits with the SIGPIPE status the way
     bash and the coreutils do. The shell itself ignores SIGPIPE, so the default
     disposition is restored here in the child before it runs the command. */
  check_syscall(sigaction(SIGPIPE, &sa, nullptr));

  /* A forked compound-pipeline child inherits the flag value at fork. A stale
     one would make the evaluator throw Interrupted before the child runs, so it
     is cleared here alongside the handler reset. */
  INTERRUPT_REQUESTED = 0;
}

static fn handle_interrupt(int s) wontthrow -> void
{
  unused(s);
  /* Setting the flag is the only async-signal-safe action. The evaluator polls
     it and aborts the running command, so a shell-internal loop is stoppable.
   */
  INTERRUPT_REQUESTED = 1;
}

fn set_default_signal_handlers(bool is_interactive) throws -> void
{
  LOG(Info, "installing the shell signal handlers, interactive %d",
      is_interactive ? 1 : 0);

  /* The interactive shell blocks the terminal-generated signals that would kill
     or stop it at the prompt, so a Ctrl-C, a SIGTERM, or a Ctrl-Z does not take
     the shell down. SIGHUP stays at its default disposition so a hangup ends
     the shell rather than leaving it reparented to init and spinning on a loop
     whose output was redirected off the dead terminal. A non-interactive script
     leaves them all at their default disposition the way dash and bash scripts
     behave, and a Ctrl-Z still suspends the job. SIGINT gets the polled handler
     in both modes, so a Ctrl-C in a shell loop sets the flag the evaluator
     polls rather than spinning forever, and an external command resets the
     handler to the default through execv and so still dies on Ctrl-C. */
  if (is_interactive) {
    sigset_t sm = make_sigset(SIGTERM, SIGQUIT, SIGSTOP, SIGTSTP);
    check_syscall(sigprocmask(SIG_BLOCK, &sm, nullptr));
  }

  struct sigaction sa = {};
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = sigchild_handler;
  check_syscall(sigaction(SIGCHLD, &sa, nullptr));

  struct sigaction si = {};
  si.sa_handler = handle_interrupt;
  check_syscall(sigaction(SIGINT, &si, nullptr));

  /* The shell ignores SIGPIPE so a write to a reader that closed returns EPIPE
     for the shell to handle rather than killing the shell. A forked child
     restores the default disposition in reset_signal_handlers, so a pipeline
     producer still dies on the signal. */
  struct sigaction sp = {};
  sp.sa_handler = SIG_IGN;
  check_syscall(sigaction(SIGPIPE, &sp, nullptr));
}

static fn handle_trapped_signal(int signal_number) wontthrow -> void
{
  /* Recording the arrival is the only async-safe action. The evaluator drains
     the flag at the next command boundary and runs the trap action there. */
  if (is_trappable_signal(signal_number))
    PENDING_SIGNAL_FLAGS[signal_number] = 1;
  SIGNAL_PENDING = 1;
}

fn set_trap_handler(i32 signal_number) throws -> void
{
  if (!is_trappable_signal(signal_number)) return;

  LOG(Info, "installing the trap handler for signal %d", signal_number);

  /* A signal the startup blocked, such as SIGTERM, must be unblocked so the
     handler runs while the shell waits at the prompt or in a loop. */
  sigset_t sm;
  sigemptyset(&sm);
  sigaddset(&sm, signal_number);
  check_syscall(sigprocmask(SIG_UNBLOCK, &sm, nullptr));

  struct sigaction sa = {};
  sa.sa_handler = handle_trapped_signal;
  check_syscall(sigaction(signal_number, &sa, nullptr));
}

fn set_trap_ignore(i32 signal_number) throws -> void
{
  if (!is_trappable_signal(signal_number)) return;
  LOG(Info, "ignoring signal %d", signal_number);
  struct sigaction sa = {};
  sa.sa_handler = SIG_IGN;
  check_syscall(sigaction(signal_number, &sa, nullptr));
}

fn clear_trap_handler(i32 signal_number) throws -> void
{
  if (!is_trappable_signal(signal_number)) return;
  LOG(Info, "clearing the trap for signal %d", signal_number);
  struct sigaction sa = {};
  /* SIGINT returns to the shell's own interrupt handler so a Ctrl-C still
     aborts a shell-internal loop. Every other signal returns to its default
     action. */
  if (signal_number == SIGINT)
    sa.sa_handler = handle_interrupt;
  else
    sa.sa_handler = SIG_DFL;
  check_syscall(sigaction(signal_number, &sa, nullptr));
}

fn monotonic_nanos() wontthrow -> u64
{
  struct timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
  return static_cast<u64>(now.tv_sec) * 1000000000ULL +
         static_cast<u64>(now.tv_nsec);
}

fn get_parent_process_id() wontthrow -> i64
{
  return static_cast<i64>(getppid());
}

fn get_real_user_id() wontthrow -> i64 { return static_cast<i64>(getuid()); }

fn get_effective_user_id() wontthrow -> i64
{
  return static_cast<i64>(geteuid());
}

fn get_real_group_id() wontthrow -> i64 { return static_cast<i64>(getgid()); }

fn child_max() wontthrow -> i64
{
  return static_cast<i64>(sysconf(_SC_CHILD_MAX));
}

fn machine_type() throws -> String
{
  /* The machine name does not change during a process lifetime, so the uname
     call runs once and every later $MACHTYPE or $HOSTTYPE read copies the
     cached value rather than issuing the syscall again. */
  static const String cached = []() -> String {
    struct utsname info{};
    if (uname(&info) != 0) return String{"unknown"};
    return String{
        StringView{info.machine, std::strlen(info.machine)}
    };
  }();
  return cached;
}

fn realtime_microseconds() wontthrow -> u64
{
  struct timespec now{};
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) return 0;
  return static_cast<u64>(now.tv_sec) * 1000000ULL +
         static_cast<u64>(now.tv_nsec) / 1000ULL;
}

fn format_local_time(StringView format, i64 epoch) throws -> String
{
  /* A negative epoch is the current time, so a fixed value renders a fixed time
     while the bash -1 and -2 magic values track the clock. */
  const time_t when = epoch < 0 ? time(nullptr) : static_cast<time_t>(epoch);
  struct tm broken_down{};
  /* localtime_r returns null and leaves the struct unspecified for an epoch
     outside the representable range, so an unchecked struct would feed strftime
     garbage. An out-of-range time renders as empty rather than a wrong date. */
  if (localtime_r(&when, &broken_down) == nullptr) return String{};
  let const format_string = String{format};
  char buffer[512];
  let const written =
      strftime(buffer, sizeof(buffer), format_string.c_str(), &broken_down);
  return String{
      StringView{buffer, written}
  };
}

fn children_cpu_seconds(double &user_seconds, double &system_seconds) wontthrow
    -> void
{
  struct rusage usage{};
  if (getrusage(RUSAGE_CHILDREN, &usage) != 0) {
    user_seconds = 0;
    system_seconds = 0;
    return;
  }
  user_seconds = static_cast<double>(usage.ru_utime.tv_sec) +
                 static_cast<double>(usage.ru_utime.tv_usec) / 1000000.0;
  system_seconds = static_cast<double>(usage.ru_stime.tv_sec) +
                   static_cast<double>(usage.ru_stime.tv_usec) / 1000000.0;
}

namespace {

#if defined __linux__

/* One hardware counter the measured run opens through perf_event_open. The
   first event of the group is the group leader and the rest are its members, so
   enabling and reading the leader sweeps the whole group at once. */
struct perf_event_request
{
  u32 type;
  u64 config;
  u64 *destination;
};

/* perf_event_open has no libc wrapper, so the raw syscall is issued here. The
   child inherits the group through inherit and enable_on_exec, so the counts
   cover the exec'd program and not the fork bookkeeping. */
fn open_perf_event(const struct perf_event_attr &attr, int group_fd) wontthrow
    -> int
{
  return static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1, group_fd,
                                  PERF_FLAG_FD_CLOEXEC));
}

/* Open the counter group, run runner while the group counts, and write the
   five counts into out. Returns false when the kernel denies perf_event_open,
   such as a restrictive perf_event_paranoid, so the caller falls back to wall
   time and resident set alone. The runner forks and waits for the child, since
   enable_on_exec ties the counting window to the exec inside it. */
template <typename Runner>
fn collect_perf_counts(perf_counts &out, Runner &&runner) wontthrow -> bool
{
  perf_event_request requests[] = {
      {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,       &out.cpu_cycles   },
      {PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,     &out.instructions },
      {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES,
       &out.cache_references                                                 },
      {PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,     &out.cache_misses },
      {PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,    &out.branch_misses},
  };
  constexpr usize PERF_COUNT = sizeof(requests) / sizeof(requests[0]);

  int perf_fds[PERF_COUNT];
  for (usize i = 0; i < PERF_COUNT; i++)
    perf_fds[i] = -1;

  defer
  {
    for (usize i = 0; i < PERF_COUNT; i++)
      if (perf_fds[i] != -1) close(perf_fds[i]);
  };

  for (usize i = 0; i < PERF_COUNT; i++) {
    struct perf_event_attr attr{};
    attr.size = sizeof(attr);
    attr.type = requests[i].type;
    attr.config = requests[i].config;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.inherit = 1;
    attr.enable_on_exec = 1;

    const int group_fd = (i == 0) ? -1 : perf_fds[0];
    perf_fds[i] = open_perf_event(attr, group_fd);
    if (perf_fds[i] == -1) return false;
  }

  ioctl(perf_fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
  ioctl(perf_fds[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);

  runner();

  ioctl(perf_fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

  for (usize i = 0; i < PERF_COUNT; i++) {
    u64 value = 0;
    if (read(perf_fds[i], &value, sizeof(value)) ==
        static_cast<ssize_t>(sizeof(value)))
      *requests[i].destination = value;
  }

  return true;
}

#endif /* __linux__ */

/* Fork the child, point its output at the null device when asked, exec argv,
   and wait4 for it so the rusage is captured. The decoded wait status goes into
   status_out and the peak resident set into peak_rss_out. Returns false when
   the fork itself failed, since an exec failure surfaces as the child's exit
   127 the way the shell reports a missing command. ru_maxrss is kilobytes on
   Linux and bytes on macOS and the BSDs, so it is scaled per platform. */
fn fork_exec_wait4(const ArrayList<String> &argv, bool suppress_output,
                   i64 &status_out, u64 &peak_rss_out) wontthrow -> bool
{
  os::os_args raw_argv{};
  for (usize i = 0; i < argv.count(); i++)
    raw_argv.push(argv[i].c_str());
  raw_argv.push(nullptr);

  const pid_t child_pid = fork();
  if (child_pid == -1) return false;

  if (child_pid == 0) {
    /* The shell ignores SIGPIPE and execve keeps an ignored disposition. The
       default is restored here before the exec, so the measured program dies on
       a broken pipe rather than inheriting the ignore. */
    signal(SIGPIPE, SIG_DFL);
    if (suppress_output) {
      const int null_fd = open("/dev/null", O_WRONLY);
      if (null_fd != -1) {
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        if (null_fd != STDOUT_FILENO && null_fd != STDERR_FILENO)
          close(null_fd);
      }
    }
    execvp(raw_argv[0], const_cast<char *const *>(raw_argv.begin()));
    _exit(127);
  }

  int status = 0;
  struct rusage usage{};
  loop
  {
    const pid_t waited = wait4(child_pid, &status, 0, &usage);
    if (waited == -1 && errno == EINTR) continue;
    break;
  }

  if (WIFEXITED(status))
    status_out = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    status_out = 128 + WTERMSIG(status);
  else
    status_out = -1;

#if defined __linux__
  /* Linux reports ru_maxrss in kilobytes, so it is scaled to bytes. */
  peak_rss_out = static_cast<u64>(usage.ru_maxrss) * 1024ULL;
#else
  /* macOS and the BSDs already report ru_maxrss in bytes. */
  peak_rss_out = static_cast<u64>(usage.ru_maxrss);
#endif

  return true;
}

} // namespace

fn run_measured(const ArrayList<String> &argv, bool suppress_output) throws
    -> Maybe<measured_result>
{
  if (argv.is_empty()) return None;

  measured_result result{};

  i64 status = 0;
  u64 peak_rss = 0;
  bool did_fork_ok = false;

  const u64 start_nanos = monotonic_nanos();

#if defined __linux__
  result.has_perf = collect_perf_counts(result.perf, [&]() wontthrow {
    did_fork_ok = fork_exec_wait4(argv, suppress_output, status, peak_rss);
  });
  if (!result.has_perf)
    did_fork_ok = fork_exec_wait4(argv, suppress_output, status, peak_rss);
#else
  did_fork_ok = fork_exec_wait4(argv, suppress_output, status, peak_rss);
#endif

  result.wall_nanos = monotonic_nanos() - start_nanos;

  if (!did_fork_ok) return None;

  result.exit_status = status;
  result.peak_rss_bytes = peak_rss;
  return result;
}

/* The path String frees its heap buffer when its scope ends, and a free may
   clobber errno before the caller reads last_system_error_message, so each
   helper saves the syscall errno across the String destructor. The inner scope
   ends the String first, then errno is restored. */
fn make_directory(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::mkdir(path_string.c_str(), mode) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn set_file_mode(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::chmod(path_string.c_str(), mode) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn touch_file_times(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    /* A null times array sets both the access and the modification time to the
       current time, the touch update for an existing file. */
    did_succeed = ::utimensat(AT_FDCWD, path_string.c_str(), nullptr, 0) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn remove_directory(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::rmdir(path_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn remove_file(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String path_string{path};
    did_succeed = ::unlink(path_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn rename_path(StringView from, StringView to) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String from_string{from};
    const String to_string{to};
    did_succeed = ::rename(from_string.c_str(), to_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn create_symlink(StringView target, StringView link_path) wontthrow -> bool
{
  bool did_succeed;
  int saved_errno;
  {
    const String target_string{target};
    const String link_string{link_path};
    did_succeed = ::symlink(target_string.c_str(), link_string.c_str()) == 0;
    saved_errno = errno;
  }
  errno = saved_errno;
  return did_succeed;
}

fn read_symlink(StringView path) wontthrow -> Maybe<String>
{
  const String path_string{path};
  /* A target longer than the buffer cannot be told apart from an exact fit, so
     the buffer grows until readlink no longer fills it, which is the signal the
     result is complete rather than truncated. The cap bounds a pathological
     filesystem. */
  usize capacity = 256;
  loop
  {
    ArrayList<char> buffer{};
    buffer.reserve(capacity);
    let const length =
        ::readlink(path_string.c_str(), buffer.begin(), capacity);
    if (length < 0) return shit::None;
    if (static_cast<usize>(length) < capacity)
      return String{
          StringView{buffer.begin(), static_cast<usize>(length)}
      };

    if (capacity >= (1U << 20)) return shit::None;
    capacity *= 2;
  }
}

fn current_executable_path() wontthrow -> Maybe<String>
{
#if defined __APPLE__
  /* macOS exposes no /proc/self/exe, so the running binary is read through
     _NSGetExecutablePath. The first call sizes the buffer, and the result is
     canonicalized since it may carry .. or pass through a symlink. */
  u32 capacity = 0;
  _NSGetExecutablePath(nullptr, &capacity);
  if (capacity == 0) return shit::None;

  ArrayList<char> buffer{};
  buffer.reserve(capacity);
  if (_NSGetExecutablePath(buffer.begin(), &capacity) != 0) return shit::None;

  let const raw_path = StringView{buffer.begin()};
  if (let const canonical = canonical_path(Path{raw_path}); canonical)
    return String{canonical->text()};

  return String{raw_path};
#else
  /* /proc/self/exe resolves to the running binary on Linux. A platform without
     it reports None, so the assimilate caller falls back to an error. */
  return read_symlink("/proc/self/exe");
#endif
}

fn stat_path(StringView path, file_status &status) wontthrow -> bool
{
  const String path_string{path};
  struct stat info{};
  /* lstat reports a symlink as itself rather than following it, so the ls long
     format shows the l type the way coreutils does without -L. */
  if (::lstat(path_string.c_str(), &info) != 0) return false;
  status.mode = static_cast<u32>(info.st_mode);
  status.link_count = static_cast<u64>(info.st_nlink);
  status.owner_id = static_cast<u32>(info.st_uid);
  status.group_id = static_cast<u32>(info.st_gid);
  status.size = static_cast<u64>(info.st_size);
  status.modification_time = static_cast<i64>(info.st_mtime);
  status.blocks = static_cast<u64>(info.st_blocks);
  return true;
}

fn file_type_letter(u32 mode) wontthrow -> char
{
  const mode_t bits = static_cast<mode_t>(mode);
  if (S_ISDIR(bits)) return 'd';
  if (S_ISLNK(bits)) return 'l';
  if (S_ISCHR(bits)) return 'c';
  if (S_ISBLK(bits)) return 'b';
  if (S_ISFIFO(bits)) return 'p';
  if (S_ISSOCK(bits)) return 's';
  return '-';
}

fn format_mode_string(u32 mode) throws -> String
{
  const mode_t bits = static_cast<mode_t>(mode);
  String result{};
  result.push(file_type_letter(mode));
  result.push((bits & S_IRUSR) != 0 ? 'r' : '-');
  result.push((bits & S_IWUSR) != 0 ? 'w' : '-');
  result.push((bits & S_ISUID) != 0 ? ((bits & S_IXUSR) != 0 ? 's' : 'S')
                                    : ((bits & S_IXUSR) != 0 ? 'x' : '-'));
  result.push((bits & S_IRGRP) != 0 ? 'r' : '-');
  result.push((bits & S_IWGRP) != 0 ? 'w' : '-');
  result.push((bits & S_ISGID) != 0 ? ((bits & S_IXGRP) != 0 ? 's' : 'S')
                                    : ((bits & S_IXGRP) != 0 ? 'x' : '-'));
  result.push((bits & S_IROTH) != 0 ? 'r' : '-');
  result.push((bits & S_IWOTH) != 0 ? 'w' : '-');
  result.push((bits & S_ISVTX) != 0 ? ((bits & S_IXOTH) != 0 ? 't' : 'T')
                                    : ((bits & S_IXOTH) != 0 ? 'x' : '-'));
  return result;
}

/* The field 0 name of the first colon line whose field at id_field_index equals
   the wanted id, read from the named database. The passwd and group files share
   the colon layout, with the id in field 2 of each, so one reader serves both.
 */
static fn lookup_name_by_id(StringView database_path, u32 wanted_id,
                            usize id_field_index) throws -> Maybe<String>
{
  let const contents = Path{database_path}.read_entire_file();
  if (!contents) return shit::None;
  let const wanted = utils::uint_to_text(static_cast<u64>(wanted_id));
  let const text = contents->view();
  for (let const &line : utils::split_lines(text)) {
    if (passwd_field(line, id_field_index) != wanted.view()) continue;
    let const name = passwd_field(line, 0);
    if (!name.is_empty()) return String{name};
  }
  return shit::None;
}

fn uid_to_username(u32 uid) throws -> Maybe<String>
{
  return lookup_name_by_id("/etc/passwd", uid, 2);
}

fn gid_to_groupname(u32 gid) throws -> Maybe<String>
{
  return lookup_name_by_id("/etc/group", gid, 2);
}

fn sleep_for_seconds(double seconds) wontthrow -> void
{
  if (seconds <= 0.0) return;
  struct timespec requested;
  requested.tv_sec = static_cast<time_t>(seconds);
  requested.tv_nsec = static_cast<long>(
      (seconds - static_cast<double>(requested.tv_sec)) * 1000000000.0);
  /* A signal can cut the sleep short, so the remaining time is slept again
     until the whole interval has passed, unless the shell's interrupt fired, in
     which case the sleep returns at once so a Ctrl-C reaches the caller. */
  struct timespec remaining;
  while (nanosleep(&requested, &remaining) == -1 && errno == EINTR) {
    if (INTERRUPT_REQUESTED) break;
    requested = remaining;
  }
}

/* The index-th whitespace-separated field of a line, empty when the line has
   too few fields, for reading the space-delimited /proc stat files. */
static donteliminate fn nth_space_field(StringView text, usize index) wontthrow
    -> StringView
{
  usize field = 0;
  usize i = 0;
  while (i < text.length) {
    while (i < text.length && (text[i] == ' ' || text[i] == '\n'))
      i++;
    if (i >= text.length) break;
    let const start = i;
    while (i < text.length && text[i] != ' ' && text[i] != '\n')
      i++;
    if (field == index) return text.substring_of_length(start, i - start);
    field++;
  }
  return StringView{};
}

#if defined __APPLE__
/* The single status letter ps shows for a macOS process, mapped from the kinfo
   p_stat field the way the BSD ps reads it. */
static fn mac_process_state_letter(char stat_value) wontthrow -> char
{
  switch (stat_value) {
  case SIDL: return 'I';
  case SRUN: return 'R';
  case SSLEEP: return 'S';
  case SSTOP: return 'T';
  case SZOMB: return 'Z';
  default: return '?';
  }
}
#endif

fn enumerate_processes(bool include_resource_stats) throws
    -> ArrayList<process_entry>
{
  ArrayList<process_entry> processes{};

#if defined __APPLE__
  /* macOS exposes no /proc, so the process table is read through sysctl with
     KERN_PROC_ALL. The first call sizes the kinfo_proc array, and the resource
     columns are filled per pid through the libproc calls when asked. */
  int name_mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
  usize byte_length = 0;
  if (::sysctl(name_mib, 4, nullptr, &byte_length, nullptr, 0) != 0)
    return processes;

  ArrayList<struct kinfo_proc> records{};
  records.reserve(byte_length / sizeof(struct kinfo_proc) + 1);
  if (::sysctl(name_mib, 4, records.begin(), &byte_length, nullptr, 0) != 0)
    return processes;

  let const entry_count = byte_length / sizeof(struct kinfo_proc);
  for (usize i = 0; i < entry_count; i++) {
    let const &record = records.begin()[i];

    process_entry process{};
    process.pid = static_cast<i64>(record.kp_proc.p_pid);
    process.name = String{StringView{record.kp_proc.p_comm}};
    process.owner_id = static_cast<u32>(record.kp_eproc.e_ucred.cr_uid);
    process.state = mac_process_state_letter(record.kp_proc.p_stat);

    if (include_resource_stats) {
      char path_buffer[PROC_PIDPATHINFO_MAXSIZE];
      if (::proc_pidpath(record.kp_proc.p_pid, path_buffer,
                         sizeof(path_buffer)) > 0)
        process.command_line = String{StringView{path_buffer}};

      struct proc_taskinfo task_info{};
      if (::proc_pidinfo(record.kp_proc.p_pid, PROC_PIDTASKINFO, 0, &task_info,
                         sizeof(task_info)) ==
          static_cast<int>(sizeof(task_info)))
      {
        process.resident_kib =
            static_cast<u64>(task_info.pti_resident_size) / 1024;
        process.virtual_kib =
            static_cast<u64>(task_info.pti_virtual_size) / 1024;
        process.cpu_ticks = static_cast<u64>(task_info.pti_total_user +
                                             task_info.pti_total_system);
      }
    }

    if (process.command_line.is_empty())
      process.command_line = "[" + process.name + "]";

    processes.push(steal(process));
  }
  return processes;
#else
  /* The process list is read from /proc, the Linux and Cosmopolitan way. A
     directory whose name is all digits is one process, and its comm file holds
     the command name the way ps reads it. A platform without /proc opens
     nothing and reports an empty list. */
  DIR *proc_dir = ::opendir("/proc");
  if (proc_dir == nullptr) return processes;
  defer { ::closedir(proc_dir); };

  for (struct dirent *entry = ::readdir(proc_dir); entry != nullptr;
       entry = ::readdir(proc_dir))
  {
    StringView name{entry->d_name};
    if (name.is_empty()) continue;
    bool is_all_digits = true;
    for (usize i = 0; i < name.length; i++)
      if (name[i] < '0' || name[i] > '9') {
        is_all_digits = false;
        break;
      }
    if (!is_all_digits) continue;

    let const parsed = utils::parse_decimal_integer(name);
    if (parsed.is_error()) continue;

    /* The three /proc files share the same per-pid prefix, built once here
       rather than reconcatenated for each. comm names the process for ps, for
       killall, and for pkill, so it is read for every process rather than only
       the kernel-thread fallback. */
    const String proc_pid = "/proc/" + name;
    const String comm_path = proc_pid + "/comm";
    Maybe<String> comm = Path{comm_path.view()}.read_entire_file();
    if (!comm.has_value()) continue;
    /* The comm file ends with a newline the listing does not want. */
    String command_name = steal(*comm);
    while (!command_name.is_empty() && command_name.back() == '\n')
      command_name.pop_back();

    process_entry process{};
    process.pid = parsed.value();
    process.name = steal(command_name);

    /* The status file names the owner on its Uid line, the real uid first, so
       ps can render the owner the way it does. A process that has gone since
       the directory scan leaves the owner at zero. */
    const String status_path = proc_pid + "/status";
    if (Maybe<String> status = Path{status_path.view()}.read_entire_file();
        status.has_value())
    {
      let const text = status->view();
      usize status_line_start = 0;
      for (usize i = 0; i <= text.length; i++) {
        if (i != text.length && text[i] != '\n') continue;
        let const line =
            text.substring_of_length(status_line_start, i - status_line_start);
        status_line_start = i + 1;
        /* The Uid line reads "Uid:\t<real>\t<effective>...", so the first run
           of digits after the tab is the real uid. */
        if (line.length < 5 ||
            line.substring_of_length(0, 5) != StringView{"Uid:\t"})
          continue;
        usize cursor = 5;
        usize digit_end = cursor;
        while (digit_end < line.length && line[digit_end] >= '0' &&
               line[digit_end] <= '9')
          digit_end++;
        let const uid_text =
            line.substring_of_length(cursor, digit_end - cursor);
        if (let const uid = utils::parse_decimal_integer(uid_text);
            !uid.is_error())
          process.owner_id = static_cast<u32>(uid.value());
        break;
      }
    }

    /* The cmdline file holds the full argument vector with a NUL after each
       argument, so the separators become spaces for the listing. A kernel
       thread exposes an empty cmdline, so the bracketed command name stands in
       the way ps shows it. */
    const String cmdline_path = proc_pid + "/cmdline";
    if (Maybe<String> cmdline = Path{cmdline_path.view()}.read_entire_file();
        cmdline.has_value() && !cmdline->is_empty())
    {
      String command_line{};
      for (usize i = 0; i < cmdline->count(); i++) {
        let const byte = cmdline->view()[i];
        if (byte == '\0') {
          if (i + 1 < cmdline->count()) command_line += ' ';
        } else {
          command_line.push(byte);
        }
      }
      process.command_line = steal(command_line);
    } else {
      process.command_line = "[" + process.name + "]";
    }

    if (include_resource_stats) {
      /* /proc/<pid>/stat holds the state and the cpu time. The comm field is
         parenthesized and may contain spaces and parens, so the fields are read
         after the last ')', where the state is first and the user and system
         times are the twelfth and thirteenth. */
      if (Maybe<String> stat =
              Path{(proc_pid + "/stat").view()}.read_entire_file();
          stat.has_value())
      {
        let const text = stat->view();
        usize after_comm = text.length;
        for (usize i = text.length; i > 0; i--)
          if (text[i - 1] == ')') {
            after_comm = i;
            break;
          }
        if (after_comm < text.length) {
          let const fields = text.substring(after_comm);
          let const state = nth_space_field(fields, 0);
          if (!state.is_empty()) process.state = state[0];
          if (let const utime =
                  utils::parse_decimal_integer(nth_space_field(fields, 11));
              !utime.is_error())
            process.cpu_ticks += static_cast<u64>(utime.value());
          if (let const stime =
                  utils::parse_decimal_integer(nth_space_field(fields, 12));
              !stime.is_error())
            process.cpu_ticks += static_cast<u64>(stime.value());
        }
      }
      /* /proc/<pid>/statm gives the sizes in pages, the first the virtual size
         and the second the resident set. */
      if (Maybe<String> statm =
              Path{(proc_pid + "/statm").view()}.read_entire_file();
          statm.has_value())
      {
        let const page_kib = static_cast<u64>(sysconf(_SC_PAGESIZE)) / 1024;
        if (let const size =
                utils::parse_decimal_integer(nth_space_field(statm->view(), 0));
            !size.is_error())
          process.virtual_kib = static_cast<u64>(size.value()) * page_kib;
        if (let const resident =
                utils::parse_decimal_integer(nth_space_field(statm->view(), 1));
            !resident.is_error())
          process.resident_kib = static_cast<u64>(resident.value()) * page_kib;
      }
    }

    processes.push(steal(process));
  }
  return processes;
#endif
}

} // namespace os

} // namespace shit

#elif SHIT_PLATFORM_IS WIN32

#include <io.h>
#include <psapi.h>
#include <sys/stat.h>
#include <tlhelp32.h>

namespace shit {

namespace os {

fn write_fd(os::descriptor fd, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>
{
  DWORD w = -1;
  if (WriteFile(fd, buf, size, &w, 0) == FALSE) /* NOLINT */
    return shit::None;
  return static_cast<usize>(w);
}

fn write_to_numbered_fd(i64 fd_number, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>
{
  let const handle = reinterpret_cast<os::descriptor>(
      _get_osfhandle(static_cast<int>(fd_number)));
  if (handle == INVALID_HANDLE_VALUE) return shit::None;
  return write_fd(handle, buf, size);
}

fn read_fd(os::descriptor fd, opaque *buf, usize size) wontthrow -> Maybe<usize>
{
  DWORD r = -1;
  if (ReadFile(fd, buf, size, &r, 0) == FALSE) /* NOLINT */
    return shit::None;
  return static_cast<usize>(r);
}

fn wait_for_fd_readable(os::descriptor fd, i64 timeout_nanos) wontthrow -> i32
{
  unused(fd);
  unused(timeout_nanos);
  return 1;
}

fn close_fd(os::descriptor fd) wontthrow -> bool
{
  return CloseHandle(fd) != FALSE;
}

fn TempFileSet::track(Path path) throws -> void { m_paths.push(steal(path)); }
fn TempFileSet::count() const wontthrow -> usize { return m_paths.count(); }
fn TempFileSet::cleanup_from(usize mark) wontthrow -> void
{
  /* A file the consuming command still holds open cannot be deleted yet, as
     when a while loop reads it across iterations, so a failed delete keeps the
     path and retries on the next cleanup once that descriptor closes, rather
     than dropping it and leaking the file. */
  usize kept = mark;
  for (usize i = mark; i < m_paths.count(); i++) {
    if (DeleteFileA(m_paths[i].c_str()) != FALSE) continue;
    if (kept != i) m_paths[kept] = steal(m_paths[i]);
    kept++;
  }
  while (m_paths.count() > kept)
    m_paths.remove(m_paths.count() - 1);
}

/* A Windows handle is inherited per CreateProcess through the bInheritHandles
   flag and the handle's own inherit bit set at creation, not per descriptor the
   way the POSIX close-on-exec bit is cleared, so this is a no-op that keeps the
   one call site in the expansion path portable. */
fn make_fd_inheritable(os::descriptor fd) wontthrow -> void { unused(fd); }

fn redirect_stdout(os::descriptor target) wontthrow -> os::descriptor
{
  os::descriptor saved = GetStdHandle(STD_OUTPUT_HANDLE);
  SetStdHandle(STD_OUTPUT_HANDLE, target);
  return saved;
}

fn restore_stdout(os::descriptor saved) wontthrow -> void
{
  SetStdHandle(STD_OUTPUT_HANDLE, saved);
}

/* Map a shell descriptor number to the Windows standard handle slot. Windows
   has no general numbered descriptor table, so only the three standard streams
   are addressable. */
static fn std_handle_slot_for_shell_fd(i32 shell_fd) -> Maybe<DWORD>
{
  switch (shell_fd) {
  case 0: return STD_INPUT_HANDLE;
  case 1: return STD_OUTPUT_HANDLE;
  case 2: return STD_ERROR_HANDLE;
  default: return shit::None;
  }
}

fn save_and_replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow
    -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;

  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) {
    result.is_dup2_ok = false;
    return result;
  }

  /* An invalid target handle, as from a duplication onto a closed descriptor,
     is reported so the caller fails the command rather than redirecting onto a
     dead handle. */
  if (target == INVALID_HANDLE_VALUE) {
    result.is_dup2_ok = false;
    return result;
  }

  result.saved = GetStdHandle(*slot);
  result.was_open = result.saved != INVALID_HANDLE_VALUE;

  /* SetStdHandle only stores the handle, it does not make an independent copy
     the way the POSIX dup2 makes a new descriptor, so the target is duplicated
     here. The caller closes the original target after this returns, and the
     duplicate stays valid in the slot until restore_descriptor closes it. */
  HANDLE duplicate = INVALID_HANDLE_VALUE;
  if (DuplicateHandle(GetCurrentProcess(), target, GetCurrentProcess(),
                      &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS) == 0)
  {
    result.is_dup2_ok = false;
    return result;
  }
  SetStdHandle(*slot, duplicate);
  result.replacement = duplicate;
  result.is_dup2_ok = true;
  return result;
}

fn restore_descriptor(const saved_descriptor &saved) wontthrow -> void
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(saved.shell_fd);
  if (!slot.has_value()) return;
  /* Close the exact duplicate this redirection installed, not whatever the slot
     holds now, since a later redirection inside the run may have replaced it.
     The saved original then returns to the slot, the way the POSIX restore
     closes the backup descriptor it kept. */
  if (saved.is_dup2_ok && saved.replacement != INVALID_HANDLE_VALUE)
    CloseHandle(saved.replacement);
  if (saved.was_open) SetStdHandle(*slot, saved.saved);
}

fn save_descriptor(i32 shell_fd) wontthrow -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) {
    result.is_dup2_ok = false;
    return result;
  }
  result.saved = GetStdHandle(*slot);
  result.was_open = result.saved != INVALID_HANDLE_VALUE;
  result.replacement = INVALID_HANDLE_VALUE;
  result.is_dup2_ok = true;
  return result;
}

/* Windows has no /dev/tty rebind for the console, so the recovery reports
   failure and the caller keeps its error path. */
fn reopen_terminal_as_stdin() wontthrow -> bool { return false; }

/* Windows has no POSIX process groups, so the terminal handoff that names the
   tmux window after the running program is a no-op and the console keeps its
   own job tracking. */
fn shell_has_controlling_terminal() wontthrow -> bool { return false; }
fn give_controlling_terminal_to(process p) wontthrow -> void { unused(p); }
fn reclaim_controlling_terminal() wontthrow -> void {}

/* Windows resolves a path lazily, so completion uses it as given. */
fn canonical_path(const Path &path) wontthrow -> Maybe<Path>
{
  return path.clone();
}

fn glob_matches(StringView pattern, Allocator allocator) throws
    -> ArrayList<String>
{
  let matches = ArrayList<String>{allocator};

  const String pattern_string{pattern};
  WIN32_FIND_DATAA find_data;
  const HANDLE handle = FindFirstFileA(pattern_string.c_str(), &find_data);
  if (handle == INVALID_HANDLE_VALUE) return matches;
  defer { FindClose(handle); };

  /* FindFirstFile yields bare names and matches only the last component, so the
     pattern's directory prefix is kept to rebuild a path the way the POSIX glob
     returns it. */
  usize prefix_length = 0;
  for (usize i = 0; i < pattern.length; i++)
    if (pattern[i] == '/' || pattern[i] == '\\') prefix_length = i + 1;
  const StringView prefix = pattern.substring_of_length(0, prefix_length);

  do {
    const StringView name{find_data.cFileName};
    if (name == "." || name == "..") continue;

    let entry = String{allocator, prefix};
    entry += name;
    matches.push(steal(entry));
  } while (FindNextFileA(handle, &find_data) != 0);

  return matches;
}

fn directory_is_trusted_for_exec(const Path &directory) wontthrow -> bool
{
  /* Windows ownership and ACL checks differ from the POSIX owner and mode bits,
     so until a Windows check lands no directory is trusted and the --help
     completion fork stays off. */
  unused(directory);
  return false;
}

fn capture_program_output(const ArrayList<String> &argv,
                          u64 timeout_nanos) wontthrow -> Maybe<String>
{
  /* The --help completion fork is off on Windows until the timed capture is
     ported, so nothing is captured. */
  unused(argv);
  unused(timeout_nanos);
  return None;
}

fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) return SHIT_INVALID_FD;
  return GetStdHandle(*slot);
}

fn descriptor_from_fd_number(i64 fd_number) wontthrow -> os::descriptor
{
  /* A Windows descriptor is a HANDLE, so the shell fd number is mapped to its C
     runtime handle the way the -t test maps one. */
  return reinterpret_cast<os::descriptor>(
      _get_osfhandle(static_cast<int>(fd_number)));
}

fn replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow -> bool
{
  /* Windows addresses only the three standard streams, so a higher descriptor
     number has no slot to point at target. */
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) return false;
  if (target == INVALID_HANDLE_VALUE) return false;
  SetStdHandle(*slot, target);
  return true;
}

fn close_shell_fd(i32 shell_fd) wontthrow -> bool
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) return false;
  const os::descriptor handle = GetStdHandle(*slot);
  SetStdHandle(*slot, INVALID_HANDLE_VALUE);
  if (handle == INVALID_HANDLE_VALUE) return false;
  return CloseHandle(handle) != FALSE;
}

fn allocate_free_shell_fd(i32 floor_fd) wontthrow -> i32
{
  (void) floor_fd;
  return -1;
}

fn get_current_user() -> Maybe<String>
{
  DWORD size = 0;
  GetUserNameA(nullptr, &size);
  if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    ArrayList<char> buffer{};
    buffer.reserve(size);
    for (DWORD i = 0; i < size; i++)
      buffer.push('\0');
    if (GetUserNameA(buffer.begin(), &size))
      return String{
          StringView{buffer.begin(), size - 1}
      };
  }
  return shit::None;
}

fn get_hostname() throws -> Maybe<String>
{
  char buffer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD size = sizeof(buffer);
  if (GetComputerNameA(buffer, &size))
    return String{
        StringView{buffer, size}
    };
  return shit::None;
}

fn get_home_directory() -> Maybe<Path>
{
  if (Maybe<String> home = get_environment_variable("USERPROFILE"))
    return Path{StringView{*home}};
  return shit::None;
}

/* Windows has no /etc/passwd, so a named user does not resolve and ~user stays
   literal. A bare ~ still expands through USERPROFILE above. */
fn get_home_for_user(StringView username) throws -> Maybe<Path>
{
  unused(username);
  return shit::None;
}

fn enumerate_users() throws -> ArrayList<String> { return ArrayList<String>{}; }

static const DWORD PARENT_SHELL_PID = GetCurrentProcessId();

fn is_child_process() wontthrow -> bool
{
  return GetCurrentProcessId() != PARENT_SHELL_PID;
}

/* Windows has no setuid or setgid notion, so the shell is never privileged in
   this sense and always reads its config. */
fn is_running_setuid() wontthrow -> bool { return false; }

fn process_id_of(process p) wontthrow -> i64
{
  return static_cast<i64>(GetProcessId(p));
}

fn is_stdin_a_tty() wontthrow -> bool { return _isatty(_fileno(stdin)) != 0; }

fn is_stdout_a_tty() wontthrow -> bool { return _isatty(_fileno(stdout)) != 0; }

fn is_stderr_a_tty() wontthrow -> bool { return _isatty(_fileno(stderr)) != 0; }

/* A Windows descriptor is a HANDLE, so the C runtime descriptor number that
   _isatty wants is recovered from it before the tty check. */
fn is_fd_a_tty(descriptor fd) wontthrow -> bool
{
  const int crt_fd = _open_osfhandle(reinterpret_cast<intptr_t>(fd), 0);
  if (crt_fd == -1) return false;
  return _isatty(crt_fd) != 0;
}

fn terminal_size(u32 &columns, u32 &rows) wontthrow -> bool
{
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) == 0)
    return false;
  const i32 width = info.srWindow.Right - info.srWindow.Left + 1;
  const i32 height = info.srWindow.Bottom - info.srWindow.Top + 1;
  if (width <= 0 || height <= 0) return false;
  columns = static_cast<u32>(width);
  rows = static_cast<u32>(height);

  return true;
}

constexpr static usize WIN32_MAX_ENV_SIZE = 32767;

fn get_environment_variable(StringView key) -> Maybe<String>
{
  String key_string{key};
  char buffer[WIN32_MAX_ENV_SIZE] = {0};
  if (GetEnvironmentVariableA(key_string.c_str(), buffer, sizeof(buffer)) == 0)
    return shit::None;
  return String{buffer};
}

fn set_environment_variable(StringView key, StringView value) -> void
{
  String key_string{key};
  String value_string{value};
  SetEnvironmentVariableA(key_string.c_str(), value_string.c_str());
}

fn unset_environment_variable(StringView key) -> void
{
  String key_string{key};
  SetEnvironmentVariableA(key_string.c_str(), nullptr);
}

fn environment_names() -> ArrayList<String>
{
  ArrayList<String> names{};
  char *block = GetEnvironmentStringsA();
  if (block == nullptr) return names;
  for (char *entry = block; *entry != '\0';) {
    StringView pair{entry};
    let const equals = pair.find_character('=');
    /* The drive entries such as =C: begin with '=', so a leading '=' is kept as
       part of the name rather than splitting on it. */
    let const split = (equals.has_value() && *equals > 0)
                          ? pair.substring_of_length(0, *equals)
                          : pair;
    names.push(String{split});
    entry += pair.length + 1;
  }
  FreeEnvironmentStringsA(block);
  return names;
}

fn execute_program(ExecContext &&ec, bool allow_script_fallback,
                   bool new_process_group) -> process
{
  /* Windows has no ENOEXEC interpreter convention, so the script fallback the
     POSIX path offers does not apply here, and no POSIX process groups, so the
     terminal handoff is a no-op below. */
  unused(allow_script_fallback);
  unused(new_process_group);

  LOG(Debug, "spawning '%s' with %zu arguments", ec.program_path().c_str(),
      ec.args().count());

  String command_line = make_os_args(ec.args());

  PROCESS_INFORMATION process_info{};
  STARTUPINFOA startup_info{};

  startup_info.cb = sizeof(startup_info);

  BOOL should_inherit_handles = ec.in_fd || ec.out_fd || ec.err_fd ||
                                ec.dup_err_to_out || ec.dup_out_to_err;

  if (should_inherit_handles) startup_info.dwFlags |= STARTF_USESTDHANDLES;

  startup_info.hStdInput = ec.in_fd.value_or(GetStdHandle(STD_INPUT_HANDLE));
  startup_info.hStdOutput = ec.out_fd.value_or(GetStdHandle(STD_OUTPUT_HANDLE));
  startup_info.hStdError = ec.err_fd.value_or(GetStdHandle(STD_ERROR_HANDLE));

  /* Each duplication reads the current target of its source handle, so when
     both are present the source order decides the result, the same way the
     POSIX path applies the earlier dup first. */
  ec.apply_dup_routing(
      [&]() { startup_info.hStdError = startup_info.hStdOutput; },
      [&]() { startup_info.hStdOutput = startup_info.hStdError; });

  defer
  {
    if (ec.in_fd) CloseHandle(*ec.in_fd);
    if (ec.out_fd) CloseHandle(*ec.out_fd);
    if (ec.err_fd) CloseHandle(*ec.err_fd);
  };

  /* Pipe and file handles are created non-inheritable, so a capture pipe the
     parent keeps does not leak into the child and hang the read. Only the
     handles the child actually receives are made inheritable here, mirroring
     how the POSIX path clears close-on-exec on just the dup targets. */
  if (should_inherit_handles) {
    SetHandleInformation(startup_info.hStdInput, HANDLE_FLAG_INHERIT,
                         HANDLE_FLAG_INHERIT);
    SetHandleInformation(startup_info.hStdOutput, HANDLE_FLAG_INHERIT,
                         HANDLE_FLAG_INHERIT);
    SetHandleInformation(startup_info.hStdError, HANDLE_FLAG_INHERIT,
                         HANDLE_FLAG_INHERIT);
  }

  /* exec -c starts the child with an empty environment. An empty CreateProcess
     environment block is two null bytes, a single empty string closed by the
     block terminator, while a null pointer means the child inherits the shell's
     environment. */
  char empty_environment_block[] = {'\0', '\0'};
  LPVOID environment_block =
      ec.should_use_empty_environment ? empty_environment_block : nullptr;

  /* CreateProcessA may rewrite lpCommandLine in place, so the owned buffer is
     handed over as a mutable pointer rather than a const view. */
  if (CreateProcessA(ec.program_path().c_str(),
                     const_cast<LPSTR>(command_line.data()), nullptr, nullptr,
                     should_inherit_handles, 0, environment_block, nullptr,
                     &startup_info, &process_info) == 0)
  {
    throw ErrorWithLocation{ec.source_location(), last_system_error_message()};
  }

  return process_info.hProcess;
}

fn run_substitution_to_temp(StringView source, bool bash_compatible) throws
    -> Maybe<String>
{
  /* Windows has no fork, so a <(cmd) substitution spawns a fresh shell that
     re-parses the inner command and writes its standard output into a temporary
     file. The consuming command then reads that file by path, the way it reads
     /dev/fd on POSIX. The whole output is written before the path returns, so a
     reader such as diff that wants its complete input sees all of it. The fresh
     shell inherits the environment but not the parent's functions or unexported
     variables, the unavoidable cost of running a separate process with no fork
     to clone the in-memory state. */
  char module_path[MAX_PATH];
  if (GetModuleFileNameA(nullptr, module_path, MAX_PATH) == 0)
    return shit::None;

  char temp_dir[MAX_PATH];
  if (GetTempPathA(MAX_PATH, temp_dir) == 0) return shit::None;
  char temp_path[MAX_PATH];
  if (GetTempFileNameA(temp_dir, "sht", 0, temp_path) == 0) return shit::None;

  SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const HANDLE temp_file =
      CreateFileA(temp_path, GENERIC_WRITE, FILE_SHARE_READ, &inheritable,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (temp_file == INVALID_HANDLE_VALUE) return shit::None;

  let arguments = ArrayList<String>{heap_allocator()};
  arguments.push(String{heap_allocator(), StringView{module_path}});
  if (bash_compatible) {
    arguments.push(String{heap_allocator(), StringView{"--mood"}});
    arguments.push(String{heap_allocator(), StringView{"bash"}});
  }
  arguments.push(String{heap_allocator(), StringView{"-c"}});
  arguments.push(String{heap_allocator(), source});
  let command_line = make_os_args(arguments);

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup_info.hStdOutput = temp_file;
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  SetHandleInformation(startup_info.hStdInput, HANDLE_FLAG_INHERIT,
                       HANDLE_FLAG_INHERIT);
  SetHandleInformation(startup_info.hStdError, HANDLE_FLAG_INHERIT,
                       HANDLE_FLAG_INHERIT);

  PROCESS_INFORMATION process_info{};
  if (CreateProcessA(module_path, const_cast<LPSTR>(command_line.data()),
                     nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup_info,
                     &process_info) == 0)
  {
    CloseHandle(temp_file);
    return shit::None;
  }
  WaitForSingleObject(process_info.hProcess, INFINITE);
  CloseHandle(process_info.hProcess);
  CloseHandle(process_info.hThread);
  CloseHandle(temp_file);

  /* The shell hands this path back as a literal field, but a backslash in it
     would read as an escape when the redirection target word is processed, so
     the separators are returned as forward slashes, which CreateFile accepts
     the same as backslashes. */
  let result = String{heap_allocator()};
  for (const char *byte = temp_path; *byte != '\0'; byte++)
    result += *byte == '\\' ? '/' : *byte;
  return result;
}

fn spawn_subshell_stage(StringView source, Maybe<descriptor> in_fd,
                        Maybe<descriptor> out_fd, bool bash_compatible) throws
    -> Maybe<process>
{
  /* A compound stage of a pipeline, such as the brace group in { a; b; } | c,
     runs in a fresh shell that re-parses the stage's source, since Windows has
     no fork to clone the in-memory state. The pipe ends are wired as the new
     shell's standard input and output. The process is returned unwaited so the
     pipeline reaps it the way it reaps a forked stage. The fresh shell inherits
     the environment but not the parent's functions or unexported variables. */
  char module_path[MAX_PATH];
  if (GetModuleFileNameA(nullptr, module_path, MAX_PATH) == 0)
    return shit::None;

  let arguments = ArrayList<String>{heap_allocator()};
  arguments.push(String{heap_allocator(), StringView{module_path}});
  if (bash_compatible) {
    arguments.push(String{heap_allocator(), StringView{"--mood"}});
    arguments.push(String{heap_allocator(), StringView{"bash"}});
  }
  arguments.push(String{heap_allocator(), StringView{"-c"}});
  arguments.push(String{heap_allocator(), source});
  let command_line = make_os_args(arguments);

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = in_fd ? *in_fd : GetStdHandle(STD_INPUT_HANDLE);
  startup_info.hStdOutput = out_fd ? *out_fd : GetStdHandle(STD_OUTPUT_HANDLE);
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  SetHandleInformation(startup_info.hStdInput, HANDLE_FLAG_INHERIT,
                       HANDLE_FLAG_INHERIT);
  SetHandleInformation(startup_info.hStdOutput, HANDLE_FLAG_INHERIT,
                       HANDLE_FLAG_INHERIT);
  SetHandleInformation(startup_info.hStdError, HANDLE_FLAG_INHERIT,
                       HANDLE_FLAG_INHERIT);

  PROCESS_INFORMATION process_info{};
  if (CreateProcessA(module_path, const_cast<LPSTR>(command_line.data()),
                     nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup_info,
                     &process_info) == 0)
    return shit::None;
  CloseHandle(process_info.hThread);
  return process_info.hProcess;
}

fn fork_compound_stage(Maybe<descriptor> in_fd, Maybe<descriptor> out_fd,
                       Maybe<descriptor> err_fd) -> process
{
  unused(in_fd);
  unused(out_fd);
  unused(err_fd);
  /* Windows has no fork. A compound stage whose source span is known re-execs
     through spawn_subshell_stage, so this throw is reached only for a stage
     type whose end position the parser does not yet record, such as an if or
     case used as a pipeline stage. */
  throw shit::Error{
      "A compound command in a pipeline is not supported on this platform"};
}

fn fork_job_process() -> process
{
  throw shit::Error{"Job control is not supported on this platform"};
}

[[noreturn]] fn exit_process_immediately(i32 status) wontthrow -> void
{
  ExitProcess(static_cast<UINT>(status));
  unreachable();
}

fn replace_process(ExecContext &&ec) -> void
{
  /* Windows cannot replace a process in place, so the program runs to
     completion and the shell exits with its status, which behaves like exec for
     a launched script. */
  LOG(Debug, "running '%s' to completion in place of an exec",
      ec.program_path().c_str());
  process child = execute_program(steal(ec));
  i32 status = wait_and_monitor_process(child);
  ExitProcess(static_cast<UINT>(status));
  unreachable();
}

fn redirect_self(const ExecContext &ec) -> void
{
  /* Duplicate each redirect handle into the standard slot, so the caller's
     close of the original handles leaves the shell's new standard handles
     valid for the rest of the session. */
  HANDLE self = GetCurrentProcess();
  HANDLE duplicate = nullptr;
  if (ec.in_fd && DuplicateHandle(self, *ec.in_fd, self, &duplicate, 0, TRUE,
                                  DUPLICATE_SAME_ACCESS))
    SetStdHandle(STD_INPUT_HANDLE, duplicate);
  if (ec.out_fd && DuplicateHandle(self, *ec.out_fd, self, &duplicate, 0, TRUE,
                                   DUPLICATE_SAME_ACCESS))
    SetStdHandle(STD_OUTPUT_HANDLE, duplicate);
  if (ec.err_fd && DuplicateHandle(self, *ec.err_fd, self, &duplicate, 0, TRUE,
                                   DUPLICATE_SAME_ACCESS))
    SetStdHandle(STD_ERROR_HANDLE, duplicate);
}

fn make_pipe() wontthrow -> Maybe<Pipe>
{
  SECURITY_ATTRIBUTES attributes{};

  attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  /* Both ends are non-inheritable, so a child only receives the end execute
     handles explicitly through STARTF_USESTDHANDLES, like close-on-exec. */
  attributes.bInheritHandle = FALSE;
  attributes.lpSecurityDescriptor = nullptr; /* NOLINT */

  HANDLE in = INVALID_HANDLE_VALUE;
  HANDLE out = INVALID_HANDLE_VALUE;

  if (CreatePipe(&in, &out, &attributes, 0) == 0) {
    if (in != INVALID_HANDLE_VALUE) close_fd(in);
    if (out != INVALID_HANDLE_VALUE) close_fd(out);

    return shit::None;
  }

  return Pipe{in, out};
}

/* CreateThread wants a DWORD(*)(LPVOID) entry, so this trampoline carries the
   C-style entry and its context across that signature and returns zero. */
struct thread_start_context
{
  void (*entry)(opaque *);
  opaque *context;
};

fn thread_trampoline(LPVOID raw_context) -> DWORD
{
  let const start = static_cast<thread_start_context *>(raw_context);
  let const entry = start->entry;
  let const context = start->context;
  delete start;
  entry(context);
  return 0;
}

fn start_thread(void (*entry)(opaque *), opaque *context) wontthrow
    -> Maybe<thread>
{
  let const start = new thread_start_context{entry, context};
  HANDLE handle =
      CreateThread(nullptr, 0, thread_trampoline, start, 0, nullptr);
  if (handle == nullptr) {
    delete start;
    return shit::None;
  }
  return thread{handle};
}

fn join_thread(thread t) wontthrow -> void
{
  WaitForSingleObject(t.handle, INFINITE);
  CloseHandle(t.handle);
}

fn open_file_descriptor(StringView path, file_open_mode mode)
    -> Maybe<descriptor>
{
  DWORD access = (mode == file_open_mode::Read) ? GENERIC_READ : GENERIC_WRITE;
  if (mode == file_open_mode::ReadWrite) access = GENERIC_READ | GENERIC_WRITE;
  DWORD disposition = OPEN_EXISTING;
  switch (mode) {
  case file_open_mode::Truncate: disposition = CREATE_ALWAYS; break;
  case file_open_mode::TruncateNoClobber: disposition = CREATE_NEW; break;
  case file_open_mode::Append: disposition = OPEN_ALWAYS; break;
  case file_open_mode::Read: disposition = OPEN_EXISTING; break;
  case file_open_mode::ReadWrite: disposition = OPEN_ALWAYS; break;
  }

  /* The handle is created non-inheritable. execute_program marks it inheritable
     only while it spawns the child that the redirection feeds. */
  SECURITY_ATTRIBUTES att{};
  att.nLength = sizeof(SECURITY_ATTRIBUTES);
  att.bInheritHandle = FALSE;
  att.lpSecurityDescriptor = nullptr; /* NOLINT */

  /* CreateFileA needs a null-terminated path, so the view is copied into a
     String that owns a trailing null. */
  String path_string{path};
  HANDLE handle = CreateFileA(path_string.c_str(), access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &att,
                              disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return shit::None;

  if (mode == file_open_mode::Append)
    SetFilePointer(handle, 0, nullptr, FILE_END);

  return handle;
}

fn write_to_temp_file(StringView content) -> Maybe<descriptor>
{
  char temp_dir[MAX_PATH];
  if (GetTempPathA(MAX_PATH, temp_dir) == 0) return shit::None;

  char temp_path[MAX_PATH];
  if (GetTempFileNameA(temp_dir, "sht", 0, temp_path) == 0) return shit::None;

  HANDLE handle = CreateFileA(
      temp_path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return shit::None;

  DWORD written = 0;
  if (WriteFile(handle, content.data, static_cast<DWORD>(content.count()),
                &written, nullptr) == 0)
  {
    close_fd(handle);
    return shit::None;
  }

  SetFilePointer(handle, 0, nullptr, FILE_BEGIN);
  return handle;
}

fn wait_and_monitor_process(process p, bool *was_stopped) -> i32
{
  unused(was_stopped);
  if (WaitForSingleObject(p, INFINITE) != WAIT_OBJECT_0)
    throw Error{"Could not wait for the process to finish: " +
                last_system_error_message()};

  DWORD code = -1;
  if (GetExitCodeProcess(p, &code) == 0)
    throw Error{"Could not read the process exit code: " +
                last_system_error_message()};

  return code;
}

fn reap_process_quietly(process p) -> i32
{
  if (WaitForSingleObject(p, INFINITE) != WAIT_OBJECT_0)
    throw Error{"Could not wait for the process to finish: " +
                last_system_error_message()};
  DWORD code = 1;
  GetExitCodeProcess(p, &code);
  return static_cast<i32>(code);
}

fn poll_process(process p, i32 &status_out) wontthrow -> process_state
{
  /* Windows has no stopped state, so a process is either alive or finished. */
  DWORD code = 0;
  if (GetExitCodeProcess(p, &code) == 0) {
    status_out = 0;
    return process_state::Exited;
  }
  if (code == STILL_ACTIVE) return process_state::Running;
  status_out = static_cast<i32>(code);
  return process_state::Exited;
}

fn signal_process(process p, i32 signal_number) wontthrow -> bool
{
  /* Windows cannot deliver a POSIX signal, so only a terminate is honored and a
     resume or a stop is a no-op the caller treats as unsupported. */
  if (signal_number == 9 || signal_number == 15) {
    return TerminateProcess(p, 1) != 0;
  }

  return false;
}

fn process_from_pid(i64 pid) wontthrow -> process
{
  return OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE |
                         PROCESS_QUERY_INFORMATION,
                     FALSE, static_cast<DWORD>(pid));
}

fn signal_number_from_name(StringView name) -> Maybe<i32>
{
  if (name.is_all_decimal_digits()) {
    const ErrorOr<i64> parsed_value = utils::parse_decimal_integer(name);
    if (parsed_value.is_error()) return shit::None;
    return static_cast<i32>(parsed_value.value());
  }

  let const bare = utils::strip_sig_prefix(name);
  if (bare == "KILL") return 9;
  if (bare == "TERM") return 15;
  if (bare == "INT") return 2;
  return None;
}

fn signal_name_from_number(i32 number) -> Maybe<String>
{
  /* The numbers mirror the Windows signal_number_from_name above, so a trap set
     or listed by number on Windows reports the same name a trap set by name
     reports. */
  if (number == 1) return String{"HUP"};
  if (number == 2) return String{"INT"};
  if (number == 3) return String{"QUIT"};
  if (number == 9) return String{"KILL"};
  if (number == 15) return String{"TERM"};
  return None;
}

fn signal_names() throws -> const ArrayList<StringView> &
{
  static ArrayList<StringView> names = [] throws {
    let collected = ArrayList<StringView>{};
    static const StringView WINDOWS_SIGNAL_NAMES[] = {"HUP", "INT", "QUIT",
                                                      "KILL", "TERM"};
    for (const StringView name : WINDOWS_SIGNAL_NAMES)
      collected.push(name);
    return collected;
  }();
  return names;
}

/* Append one argument to a Windows command line, quoted and escaped the way
   CommandLineToArgvW parses it back, so an argument that carries a space, a
   tab, or a quote cannot break out of its slot and inject further arguments.
   The rules are the Microsoft ones, a run of backslashes is doubled only when
   it precedes a quote or the closing quote, and an embedded quote is escaped
   with a backslash. An argument with no space, tab, or quote is emitted bare,
   and an empty argument is quoted so it is not dropped. */
static fn append_windows_quoted_arg(String &out, StringView arg) -> void
{
  bool should_quote_arg = arg.count() == 0;
  for (usize i = 0; i < arg.count() && !should_quote_arg; i++) {
    const char c = arg[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"') {
      should_quote_arg = true;
    }
  }
  if (!should_quote_arg) {
    out.append(arg);
    return;
  }

  out += '"';
  for (usize i = 0; i < arg.count();) {
    usize backslash_count = 0;
    while (i < arg.count() && arg[i] == '\\') {
      i++;
      backslash_count++;
    }
    if (i == arg.count()) {
      /* Trailing backslashes precede the closing quote, so they are doubled to
         stay literal rather than escaping the quote. */
      for (usize k = 0; k < backslash_count * 2; k++)
        out += '\\';
      break;
    }
    if (arg[i] == '"') {
      /* The backslashes before a quote are doubled and the quote is escaped. */
      for (usize k = 0; k < backslash_count * 2 + 1; k++)
        out += '\\';
      out += '"';
      i++;
    } else {
      for (usize k = 0; k < backslash_count; k++)
        out += '\\';
      out += arg[i];
      i++;
    }
  }
  out += '"';
}

fn make_os_args(const ArrayList<String> &args) -> os_args
{
  ASSERT(args.count() > 0);

  String command_line{};
  append_windows_quoted_arg(command_line, args[0].view());
  for (usize i = 1; i < args.count(); i++) {
    command_line += ' ';
    append_windows_quoted_arg(command_line, args[i].view());
  }

  return command_line;
}

cold fn last_system_error_message() throws -> String
{
  LPSTR errno_str{};
  DWORD win_errno = GetLastError();

  DWORD ret = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, win_errno, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&errno_str), 0, nullptr); /* NOLINT */

  if (ret == 0) {
    return utils::uint_to_text(win_errno) +
           StringView{" (Error message could not be processed due to "
                      "a FormatMessage() failure)"};
  }

  StringView view{static_cast<char *>(errno_str)};
  /* FormatMessage ends the text with a trailing period, spacing, and a CRLF, so
     the trailing run of those bytes is trimmed to leave a bare message rather
     than a fixed count that a shorter message would over-cut. */
  while (view.length > 0) {
    let const last_byte = view[view.length - 1];
    if (last_byte != '.' && last_byte != ' ' && last_byte != '\r' &&
        last_byte != '\n')
    {
      break;
    }
    view = view.substring_of_length(0, view.length - 1);
  }

  String err{};
  for (usize i = 0; i < view.length; i++) {
    /* FormatMessage leaves a %N placeholder where an argument would go, which
       this call passes none of, so the placeholder is replaced with a word. */
    if (view[i] == '%' && i + 1 < view.length && isdigit(view[i + 1])) {
      err += StringView{"input"};
      i++;
      continue;
    }
    err.push(view[i]);
  }

  LocalFree(errno_str);

  if (err.length() > 0) {
    String capitalized{};
    capitalized.push(static_cast<char>(toupper(err[0])));
    capitalized += err.substring(1);
    err = steal(capitalized);
  }

  return err;
}

static fn handle_interrupt(int s) -> void
{
  unused(s);
  /* Only set the flag, since printing or any non-async-signal-safe work inside
     a handler is undefined. The evaluator polls the flag and aborts the running
     command, so an infinite loop can be stopped from the keyboard. */
  INTERRUPT_REQUESTED = 1;
  signal(SIGINT, handle_interrupt);
}

fn set_default_signal_handlers(bool is_interactive) -> void
{
  /* The interactive shell ignores SIGTERM so a stray terminate does not close
     the prompt, while a non-interactive script leaves it at the default so it
     terminates as usual. SIGINT routes to the polled handler in both modes. */
  if (is_interactive && signal(SIGTERM, SIG_IGN) == SIG_ERR) {
    throw Error{"Could not install the signal handlers: " +
                last_system_error_message()};
  }

  if (signal(SIGINT, handle_interrupt) == SIG_ERR) {
    throw Error{"Could not install the signal handlers: " +
                last_system_error_message()};
  }
}

fn reset_signal_handlers() -> void
{
  if (signal(SIGTERM, SIG_DFL) == SIG_ERR || signal(SIGINT, SIG_DFL) == SIG_ERR)
  {
    throw Error{"Could not restore the default signal handlers: " +
                last_system_error_message()};
  }

  /* A child started for a compound pipeline stage inherits the flag value at
     spawn. A stale one would make the evaluator throw Interrupted before the
     child runs, so it is cleared here alongside the handler reset. */
  INTERRUPT_REQUESTED = 0;
}

static fn handle_trapped_signal(int signal_number) -> void
{
  if (is_trappable_signal(signal_number))
    PENDING_SIGNAL_FLAGS[signal_number] = 1;
  SIGNAL_PENDING = 1;
  /* The C runtime resets the disposition to default before the handler runs, so
     it is reinstalled here for the next arrival, the way the interrupt handler
     reinstalls itself. */
  signal(signal_number, handle_trapped_signal);
}

fn set_trap_handler(i32 signal_number) -> void
{
  if (!is_trappable_signal(signal_number)) return;
  signal(signal_number, handle_trapped_signal);
}

fn set_trap_ignore(i32 signal_number) -> void
{
  if (!is_trappable_signal(signal_number)) return;
  signal(signal_number, SIG_IGN);
}

fn clear_trap_handler(i32 signal_number) -> void
{
  if (!is_trappable_signal(signal_number)) return;
  if (signal_number == SIGINT)
    signal(signal_number, handle_interrupt);
  else
    signal(signal_number, SIG_DFL);
}

fn monotonic_nanos() wontthrow -> u64
{
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  if (QueryPerformanceFrequency(&frequency) == 0) return 0;
  if (QueryPerformanceCounter(&counter) == 0) return 0;
  /* The counter is scaled to nanoseconds through the frequency, splitting the
     whole seconds from the remainder so the multiply never overflows the way a
     raw counter times a billion would. */
  const u64 whole_seconds = counter.QuadPart / frequency.QuadPart;
  const u64 remainder = counter.QuadPart % frequency.QuadPart;
  return whole_seconds * 1000000000ULL +
         (remainder * 1000000000ULL) / static_cast<u64>(frequency.QuadPart);
}

fn get_parent_process_id() wontthrow -> i64 { return 0; }

fn get_real_user_id() wontthrow -> i64 { return 0; }

fn get_effective_user_id() wontthrow -> i64 { return 0; }

fn get_real_group_id() wontthrow -> i64 { return 0; }

fn child_max() wontthrow -> i64 { return 0; }

fn machine_type() throws -> String { return String{"x86_64"}; }

fn realtime_microseconds() wontthrow -> u64
{
  FILETIME file_time;
  GetSystemTimePreciseAsFileTime(&file_time);
  ULARGE_INTEGER ticks;
  ticks.LowPart = file_time.dwLowDateTime;
  ticks.HighPart = file_time.dwHighDateTime;
  /* The FILETIME counts 100-nanosecond intervals since 1601, so the offset to
     1970 is removed and the rest scaled from 100ns units down to microseconds.
   */
  const u64 epoch_offset_100ns = 116444736000000000ULL;
  if (ticks.QuadPart < epoch_offset_100ns) return 0;
  return (ticks.QuadPart - epoch_offset_100ns) / 10ULL;
}

fn format_local_time(StringView format, i64 epoch) throws -> String
{
  const time_t when = epoch < 0 ? time(nullptr) : static_cast<time_t>(epoch);
  struct tm broken_down{};
  localtime_s(&broken_down, &when);
  let const format_string = String{format};
  char buffer[512];
  let const written =
      strftime(buffer, sizeof(buffer), format_string.c_str(), &broken_down);
  return String{
      StringView{buffer, written}
  };
}

fn children_cpu_seconds(double &user_seconds, double &system_seconds) wontthrow
    -> void
{
  /* Windows carries no RUSAGE_CHILDREN equivalent here, so the cpu split is
     reported as zero and only the wall time of the timed command is meaningful.
   */
  user_seconds = 0;
  system_seconds = 0;
}

fn run_measured(const ArrayList<String> &argv, bool suppress_output) throws
    -> Maybe<measured_result>
{
  if (argv.is_empty()) return None;

  /* Windows carries no hardware perf counters here, so a measured run reports
     wall time, the peak working set, and the user and system process times. The
     child output is pointed at the null device when suppressed, the same as the
     POSIX path. */
  measured_result result{};

  String command_line{};
  for (usize i = 0; i < argv.count(); i++) {
    if (i > 0) command_line.push(' ');
    command_line.append(argv[i].view());
  }

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);

  HANDLE null_handle = INVALID_HANDLE_VALUE;
  if (suppress_output) {
    SECURITY_ATTRIBUTES inherit_sa{};
    inherit_sa.nLength = sizeof(inherit_sa);
    inherit_sa.bInheritHandle = TRUE;
    null_handle = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                              &inherit_sa, OPEN_EXISTING, 0, nullptr);
    if (null_handle != INVALID_HANDLE_VALUE) {
      startup.dwFlags |= STARTF_USESTDHANDLES;
      startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      startup.hStdOutput = null_handle;
      startup.hStdError = null_handle;
    }
  }
  defer
  {
    if (null_handle != INVALID_HANDLE_VALUE) CloseHandle(null_handle);
  };

  PROCESS_INFORMATION process_info{};
  String mutable_command_line = command_line;

  const u64 start_nanos = monotonic_nanos();

  /* CreateProcessA may rewrite lpCommandLine in place, so the owned buffer is
     handed over as a mutable pointer rather than a const view. */
  if (CreateProcessA(nullptr, const_cast<LPSTR>(mutable_command_line.data()),
                     nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                     &process_info) == 0)
    return None;

  WaitForSingleObject(process_info.hProcess, INFINITE);

  result.wall_nanos = monotonic_nanos() - start_nanos;

  DWORD exit_code = 0;
  GetExitCodeProcess(process_info.hProcess, &exit_code);
  result.exit_status = static_cast<i64>(exit_code);

  PROCESS_MEMORY_COUNTERS memory_counters{};
  memory_counters.cb = sizeof(memory_counters);
  if (GetProcessMemoryInfo(process_info.hProcess, &memory_counters,
                           sizeof(memory_counters)) != 0)
    result.peak_rss_bytes =
        static_cast<u64>(memory_counters.PeakWorkingSetSize);

  CloseHandle(process_info.hProcess);
  CloseHandle(process_info.hThread);

  return result;
}

fn make_directory(StringView path, u32 mode) wontthrow -> bool
{
  unused(mode);
  const String path_string{path};
  return CreateDirectoryA(path_string.c_str(), nullptr) != 0;
}

fn set_file_mode(StringView path, u32 mode) wontthrow -> bool
{
  /* Windows carries no POSIX permission bits on the create path, so the exact
     mode is accepted and ignored the way make_directory ignores it. */
  unused(path);
  unused(mode);
  return true;
}

fn touch_file_times(StringView path) wontthrow -> bool
{
  const String path_string{path};
  HANDLE handle =
      CreateFileA(path_string.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;

  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  let const did_set = SetFileTime(handle, nullptr, &now, &now) != 0;
  CloseHandle(handle);
  return did_set;
}

fn remove_directory(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return RemoveDirectoryA(path_string.c_str()) != 0;
}

fn remove_file(StringView path) wontthrow -> bool
{
  const String path_string{path};
  return DeleteFileA(path_string.c_str()) != 0;
}

fn rename_path(StringView from, StringView to) wontthrow -> bool
{
  const String from_string{from};
  const String to_string{to};
  return MoveFileExA(from_string.c_str(), to_string.c_str(),
                     MOVEFILE_REPLACE_EXISTING) != 0;
}

fn create_symlink(StringView target, StringView link_path) wontthrow -> bool
{
  const String target_string{target};
  const String link_string{link_path};
/* An older mingw SDK omits these CreateSymbolicLink flags, so each is defined
   here when the toolchain header does not provide it. */
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif
  /* A symlink to a directory needs the directory flag, so the target is probed
     once. The unprivileged-create flag lets the call succeed without elevation
     on a developer-mode Windows. */
  DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  const DWORD attributes = GetFileAttributesA(target_string.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
  return CreateSymbolicLinkA(link_string.c_str(), target_string.c_str(),
                             flags) != 0;
}

fn read_symlink(StringView path) wontthrow -> Maybe<String>
{
  /* Reading a reparse point needs a device control call this layer does not
     wrap, so cp on Windows copies a symlink's contents rather than the link. */
  unused(path);
  return shit::None;
}

fn current_executable_path() wontthrow -> Maybe<String>
{
  char module_path[MAX_PATH];
  if (GetModuleFileNameA(nullptr, module_path, MAX_PATH) == 0)
    return shit::None;
  return String{module_path};
}

fn stat_path(StringView path, file_status &status) wontthrow -> bool
{
  const String path_string{path};
  struct stat info{};
  /* Windows has no lstat, so a symlink reports the target it resolves to, the
     closest the platform comes to the POSIX behavior. */
  if (::stat(path_string.c_str(), &info) != 0) return false;
  status.mode = static_cast<u32>(info.st_mode);
  status.link_count = static_cast<u64>(info.st_nlink);
  status.owner_id = static_cast<u32>(info.st_uid);
  status.group_id = static_cast<u32>(info.st_gid);
  status.size = static_cast<u64>(info.st_size);
  status.modification_time = static_cast<i64>(info.st_mtime);
  /* Windows stat carries no block count, so the 512-byte blocks are derived
     from the size for the ls total line. */
  status.blocks = (static_cast<u64>(info.st_size) + 511) / 512;
  return true;
}

fn format_mode_string(u32 mode) throws -> String
{
  /* Windows stat exposes only the read, write, and execute bits for the owner,
     mirrored across the group and other triplets so the column keeps its width.
     The directory bit gives the type letter. */
  const bool is_readable = (mode & 0000400u) != 0;
  const bool is_writable = (mode & 0000200u) != 0;
  const bool is_executable = (mode & 0000100u) != 0;

  String result{};
  result.push(file_type_letter(mode));
  for (usize triplet = 0; triplet < 3; triplet++) {
    result.push(is_readable ? 'r' : '-');
    result.push(is_writable ? 'w' : '-');
    result.push(is_executable ? 'x' : '-');
  }
  return result;
}

fn file_type_letter(u32 mode) wontthrow -> char
{
  /* Windows stat distinguishes only the directory bit from a regular file. */
  return (mode & 0040000u) != 0 ? 'd' : '-';
}

fn uid_to_username(u32 uid) throws -> Maybe<String>
{
  /* Windows names users through the security database rather than a passwd
     file, so ls falls back to the numeric id. */
  unused(uid);
  return shit::None;
}

fn gid_to_groupname(u32 gid) throws -> Maybe<String>
{
  unused(gid);
  return shit::None;
}

fn sleep_for_seconds(double seconds) wontthrow -> void
{
  if (seconds <= 0.0) return;
  Sleep(static_cast<DWORD>(seconds * 1000.0));
}

fn enumerate_processes(bool include_resource_stats) throws
    -> ArrayList<process_entry>
{
  /* The Windows snapshot carries no per-process resource stats this layer
     reads, so the flag is accepted and the BSD columns stay zero. */
  unused(include_resource_stats);
  ArrayList<process_entry> processes{};
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return processes;
  defer { CloseHandle(snapshot); };

  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Process32First(snapshot, &entry) == 0) return processes;
  do {
    process_entry process{};
    process.pid = static_cast<i64>(entry.th32ProcessID);
    process.name = String{entry.szExeFile};
    /* The snapshot exposes the executable name rather than the full argument
       vector, so the command line stands in as that name. */
    process.command_line = process.name.clone();
    processes.push(steal(process));
  } while (Process32Next(snapshot, &entry) != 0);
  return processes;
}

} // namespace os

} // namespace shit

#endif /* PLATFORM_IS(WIN32) */

#if SHIT_PLATFORM_IS COSMO or SHIT_PLATFORM_IS WIN32

namespace shit {

namespace os {

const ArrayList<String> OMITTED_SUFFIXES = []() {
  ArrayList<String> suffixes{};
  for (const char *suffix : {"", ".exe", ".com", ".scr", ".bat"})
    suffixes.push(String{suffix});
  return suffixes;
}();

constexpr static usize MIN_SUFFIX_LEN = 3;

fn erase_extension_and_get_its_index(String &program_name) -> ext_index
{
#if SHIT_PLATFORM_IS COSMO
  if (IsWindows())
#endif
  {
    let const extension_pos = program_name.find_last_character('.');

    if (extension_pos.has_value() &&
        *extension_pos + MIN_SUFFIX_LEN < program_name.length())
    {
      const StringView extension = program_name.substring(*extension_pos);

      if (Maybe<usize> found_index =
              utils::find_pos_in_vec(OMITTED_SUFFIXES, extension);
          found_index.has_value())
      {
        program_name =
            String{program_name.substring_of_length(0, *extension_pos)};
        return *found_index;
      }
    }
  }

  return 0;
}

} // namespace os

} // namespace shit

#endif /* COSMO || WIN32 */

/* The platform blocks above set PARENT_SHELL_PID and pull in the headers each
   platform needs. These functions read the same way on every platform, so they
   live once here rather than once per block. */
namespace shit {
namespace os {

fn get_shell_process_id() wontthrow -> i64
{
  return static_cast<i64>(PARENT_SHELL_PID);
}

fn get_file_creation_mask() wontthrow -> u32
{
  /* umask only reads through a set, so the previous value is read and put back.
     The macro casts to the platform type, so the value passes through as is. */
  let const previous_mask = SHIT_UMASK(0);
  SHIT_UMASK(previous_mask);

  return static_cast<u32>(previous_mask);
}

fn set_file_creation_mask(u32 mask) wontthrow -> void { SHIT_UMASK(mask); }

} // namespace os
} // namespace shit
