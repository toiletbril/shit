#include "Platform.hpp"

#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdarg>
#include <cstring>

#if SHIT_PLATFORM_IS POSIX
#include <fcntl.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#if defined __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

/* posix_spawn takes the child environment as an envp argument. The shell passes
   its own process environment, which prefix assignments mutate before the spawn
   and restore after, so the declaration of environ is needed here. */
extern char **environ;
#endif

#if SHIT_PLATFORM_IS POSIX

namespace shit {

namespace os {

hot fn write_fd(os::descriptor fd, const void *buf, usize size) wontthrow
    -> Maybe<usize>
{
  for (;;) {
    ssize_t w = write(fd, buf, size);
    /* A signal that lands mid-write interrupts the call before any byte is
       transferred. Retry instead of reporting a spurious write failure. */
    if (w == -1 && errno == EINTR) continue;
    if (w == -1) return shit::None;
    return static_cast<usize>(w);
  }
}

hot fn read_fd(os::descriptor fd, void *buf, usize size) wontthrow
    -> Maybe<usize>
{
  for (;;) {
    ssize_t r = read(fd, buf, size);
    /* A signal that lands while the read blocks, such as SIGCHLD from a job
       that changes state, interrupts the call. Retry so the reader does not
       mistake the interruption for end of input. */
    if (r == -1 && errno == EINTR) continue;
    if (r == -1) return shit::None;
    return static_cast<usize>(r);
  }
}

fn close_fd(os::descriptor fd) wontthrow -> bool { return close(fd) != -1; }

fn TempFileSet::track(Path path) throws -> void { unused(path); }
fn TempFileSet::count() const wontthrow -> usize { return 0; }
fn TempFileSet::cleanup_from(usize mark) wontthrow -> void { unused(mark); }

fn redirect_stdout(os::descriptor target) wontthrow -> os::descriptor
{
  /* The saved copy of the real stdout is close-on-exec, so a forked command
     does not inherit it and hold the shell's own output open. An immortal
     pipeline stage like yes would otherwise keep a downstream reader from ever
     seeing end of input. */
  const os::descriptor saved = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
  dup2(target, STDOUT_FILENO);

  /* The original write end is close-on-exec for the same reason. The duplicate
     now living on STDOUT_FILENO stays open for the command to write to. */
  if (const int flags = fcntl(target, F_GETFD); flags != -1)
    fcntl(target, F_SETFD, flags | FD_CLOEXEC);

  return saved;
}

fn restore_stdout(os::descriptor saved) wontthrow -> void
{
  dup2(saved, STDOUT_FILENO);
  close(saved);
}

fn save_and_replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow
    -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;

  /* The backup is close-on-exec so a command spawned by the redirected child
     does not inherit the shell's own original descriptor and hold it open. */
  const os::descriptor backup = fcntl(shell_fd, F_DUPFD_CLOEXEC, 0);
  result.was_open = backup != -1;
  result.saved = backup;

  /* A dup2 from a closed or invalid target descriptor fails, as in >&5 with fd
     5 closed. The caller reports the failure so the command fails rather than
     writing to the original descriptor. */
  result.dup2_ok = dup2(target, shell_fd) != -1;
  return result;
}

fn restore_descriptor(const saved_descriptor &saved) wontthrow -> void
{
  if (saved.was_open) {
    dup2(saved.saved, saved.shell_fd);
    close(saved.saved);
  } else {
    /* The descriptor was not open before, so close the one the redirection
       opened on it to leave the shell as it was. */
    close(saved.shell_fd);
  }
}

fn save_descriptor(i32 shell_fd) wontthrow -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;
  /* The backup carries close-on-exec the same way save_and_replace_descriptor
     takes its backup, so a spawned command never inherits it. */
  const os::descriptor backup = fcntl(shell_fd, F_DUPFD_CLOEXEC, 0);
  result.was_open = backup != -1;
  result.saved = backup;
  result.dup2_ok = true;
  return result;
}

fn reopen_terminal_as_stdin() wontthrow -> bool
{
  const int tty_fd = open("/dev/tty", O_RDWR);
  if (tty_fd == -1) return false;
  LOG(verbosity::Info, "reopening the controlling terminal onto fd 0");
  const bool replaced = dup2(tty_fd, STDIN_FILENO) != -1;
  close(tty_fd);
  return replaced && isatty(STDIN_FILENO) == 1;
}

fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor
{
  return shell_fd;
}

fn replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow -> bool
{
  /* A self copy, as in exec 1>&1, already has the descriptor where it belongs,
     so the dup2 onto itself is skipped to avoid clearing the close-on-exec the
     caller may rely on. */
  if (target == shell_fd) return true;
  return dup2(target, shell_fd) != -1;
}

fn close_shell_fd(i32 shell_fd) wontthrow -> bool
{
  return close(shell_fd) != -1;
}

static fn passwd_field(StringView line, usize index) wontthrow -> StringView;

fn get_current_user() throws -> Maybe<String>
{
  /* The name comes from the environment rather than getpwuid, which a static
     build cannot call without pulling in the runtime glibc and which the linker
     warns about. A login shell sets LOGNAME and USER. */
  if (const char *name = std::getenv("LOGNAME"); name != nullptr)
    return String{StringView{name}};
  if (const char *name = std::getenv("USER"); name != nullptr)
    return String{StringView{name}};

  /* A container that exports neither leaves the environment bare, so the name
     is read from /etc/passwd by the current uid, the same direct read
     get_home_for_user uses. getuid is a plain syscall, so the static build
     stays free of the NSS modules getpwuid would pull in. */
  let const contents = utils::read_entire_file("/etc/passwd");
  if (!contents) return shit::None;
  let const wanted_uid = utils::uint_to_text(static_cast<u64>(getuid()));
  let const text = contents->view();
  usize line_start = 0;
  for (usize i = 0; i <= text.length; i++) {
    if (i != text.length && text[i] != '\n') continue;
    let const line = text.substring_of_length(line_start, i - line_start);
    line_start = i + 1;
    if (passwd_field(line, 2) != wanted_uid.view()) continue;
    let const name = passwd_field(line, 0);
    if (!name.is_empty()) return String{name};
  }
  return shit::None;
}

fn get_hostname() throws -> Maybe<String>
{
  /* HOST_NAME_MAX is not portable across every libc, so a fixed buffer holds
     the name and a trailing NUL guards against a truncated result that some
     implementations leave unterminated. */
  char buffer[256];
  if (gethostname(buffer, sizeof(buffer)) != 0) return shit::None;
  buffer[sizeof(buffer) - 1] = '\0';
  return String{StringView{buffer}};
}

fn get_home_directory() throws -> Maybe<Path>
{
  if (const Maybe<String> home = get_environment_variable("HOME"))
    return Path{StringView{*home}};
  return shit::None;
}

/* The colon field at index of an /etc/passwd line, empty when the line has too
   few fields. The format is name:passwd:uid:gid:gecos:home:shell, so the name
   is field 0 and the home is field 5. The database is read directly rather than
   through getpwnam or getpwent, which a static build cannot call without
   pulling in the runtime glibc NSS modules and which the linker warns about,
   the same reason get_current_user reads the environment. A user defined only
   through NSS is not seen, the accepted tradeoff for the static build. */
static fn passwd_field(StringView line, usize index) wontthrow -> StringView
{
  usize field_start = 0;
  usize field_index = 0;
  for (usize i = 0; i <= line.length; i++) {
    if (i != line.length && line[i] != ':') continue;
    if (field_index == index)
      return line.substring_of_length(field_start, i - field_start);
    field_index++;
    field_start = i + 1;
  }
  return StringView{};
}

fn get_home_for_user(StringView username) throws -> Maybe<Path>
{
  if (username.is_empty()) return shit::None;

  let const contents = utils::read_entire_file("/etc/passwd");
  if (!contents) return shit::None;

  let const text = contents->view();
  usize line_start = 0;
  for (usize i = 0; i <= text.length; i++) {
    if (i != text.length && text[i] != '\n') continue;
    let const line = text.substring_of_length(line_start, i - line_start);
    line_start = i + 1;
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

  let const contents = utils::read_entire_file("/etc/passwd");
  if (!contents) return users;

  let const text = contents->view();
  usize line_start = 0;
  for (usize i = 0; i <= text.length; i++) {
    if (i != text.length && text[i] != '\n') continue;
    let const line = text.substring_of_length(line_start, i - line_start);
    line_start = i + 1;
    let const name = passwd_field(line, 0);
    if (!name.is_empty()) users.push(String{name});
  }
  return users;
}

static const pid_t PARENT_SHELL_PID = getpid();

fn is_child_process() wontthrow -> bool { return getpid() != PARENT_SHELL_PID; }

fn get_shell_process_id() wontthrow -> i64
{
  return static_cast<i64>(PARENT_SHELL_PID);
}

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
  LOG(verbosity::Debug, "querying the terminal size");
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
  suffixes.push(String{StringView{""}});
  return suffixes;
}();

fn erase_extension_and_get_its_index(String &program_name) throws -> ext_index
{
  /* POSIX does not really make use of extensions for executable files. */
  unused(program_name);
  return false;
}
#endif /* !COSMO */

fn get_environment_variable(StringView key) throws -> Maybe<String>
{
  LOG(verbosity::All, "reading the environment variable '%.*s'",
      static_cast<int>(key.length), key.data);
  const String key_string{key};
  const char *e = std::getenv(key_string.c_str());
  if (e != nullptr) return String{StringView{e}};
  return shit::None;
}

fn set_environment_variable(StringView key, StringView value) throws -> void
{
  LOG(verbosity::All, "setting the environment variable '%.*s'",
      static_cast<int>(key.length), key.data);
  const String key_string{key};
  const String value_string{value};
  setenv(key_string.c_str(), value_string.c_str(), 1);
}

fn unset_environment_variable(StringView key) throws -> void
{
  LOG(verbosity::All, "unsetting the environment variable '%.*s'",
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

/* posix_spawn reports an exec failure to the parent through its return value
   rather than through a child that runs the parent's cleanup, so there is no
   spawned process to wait on. The fork path this replaces let the child print
   the path and message and exit 127, so the caller always received a waitable
   pid that yielded 127. This reproduces that on the cold failure path. The
   error came from posix_spawn rather than errno, so it is passed in. A trivial
   child is forked that only reports and exits 127, which gives the caller the
   same waitable pid and status the fork path produced. */
cold fn spawn_failure_child(const Path &program_path, int spawn_error) throws
    -> process
{
  LOG(verbosity::Debug, "forking a child to report the spawn failure for '%s'",
      program_path.c_str());

  const pid_t child_pid = check_syscall(fork());

  if (child_pid == 0) {
    String msg{};
    msg += program_path.text();
    msg += ": ";
    msg += String{StringView{strerror(spawn_error)}};
    msg += '\n';
    (void) write_fd(STDERR_FILENO, msg.data(), msg.count());
    /* The program was resolved but could not be executed, so bash exits 126,
       reserving 127 for a missing file, as when a shebang names an interpreter
       that does not exist. */
    _exit(spawn_error == ENOENT ? 127 : 126);
  }

  return child_pid;
}

hot fn execute_program(ExecContext &&ec, bool allow_script_fallback) throws
    -> process
{
  ASSERT(ec.args().count() > 0, "a program needs at least argv[0]");

  LOG(verbosity::Debug, "spawning '%s' with %zu arguments",
      ec.program_path().c_str(), ec.args().count());

  /* On the ENOEXEC fallback the context's descriptors are handed to the script
     run, which reapplies the command's redirections, so they are not closed
     here. */
  bool fds_handed_to_fallback = false;
  defer
  {
    if (!fds_handed_to_fallback) ec.close_fds();
  };

  let const child_args = make_os_args(ec.args());

  /* glibc backs posix_spawn with clone(CLONE_VM | CLONE_VFORK), so the child
     shares the parent address space until exec and pays no page-table copy, the
     way dash's vfork does. The redirections become file actions and the signal
     reset becomes spawn attributes, so the parent never enters a duplicated
     evaluator the way the fork path did. */
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_init(&file_actions);
  defer { posix_spawn_file_actions_destroy(&file_actions); };

  /* Each redirect is placed onto its standard descriptor and the original is
     closed, in this order, so the child sees the same descriptor layout the
     fork child built. A descriptor that already sits on its target slot, as
     when the inherited standard input is passed as in_fd, is left in place,
     since the dup2 onto itself is a no-op and the close would shut the live
     descriptor and leave the child with no standard input. */
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
     the final standard output. Each dup reads the current target of its source
     descriptor, so when both are present the source order decides the result.
     The earlier dup is applied first and the later one reads its effect, which
     reproduces the way dash routes a mixed 2>&1 1>&2 against 1>&2 2>&1. */
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

  /* The parent blocks the terminal-generated signals and handles SIGINT and
     SIGCHLD itself, so without a reset the exec'd program would inherit the
     blocked set and the parent's dispositions. An empty signal mask unblocks
     everything the parent blocked, and the default set restores SIG_DFL for the
     signals the parent installed handlers on. This matches the fork child's
     reset_signal_handlers, so a foreground command still dies on Ctrl-C and a
     producer in a pipe still dies on SIGPIPE. */
  sigset_t empty_mask;
  sigemptyset(&empty_mask);
  posix_spawnattr_setsigmask(&attr, &empty_mask);

  sigset_t default_signals;
  sigemptyset(&default_signals);
  sigaddset(&default_signals, SIGINT);
  sigaddset(&default_signals, SIGCHLD);
  posix_spawnattr_setsigdefault(&attr, &default_signals);

  posix_spawnattr_setflags(&attr,
                           POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

  pid_t child_pid = 0;
  /* The shell environment is passed as envp, so a prefix assignment the parent
     committed before this call reaches the child. */
  const int spawn_error =
      posix_spawn(&child_pid, ec.program_path().c_str(), &file_actions, &attr,
                  const_cast<char *const *>(child_args.begin()), environ);

  /* An ENOEXEC file is executable but carries no shebang and is not a binary.
     When the caller can fall back, this is signalled so the file runs as a
     shell script in place, the POSIX behavior, rather than failing 127. The
     check runs before the descriptors are closed, so the script run still sees
     the command's redirections on the context. */
  if (spawn_error == ENOEXEC && allow_script_fallback) {
    fds_handed_to_fallback = true;
    throw shit::ExecFormatError{};
  }

  ec.close_fds();

  if (spawn_error != 0)
    return spawn_failure_child(ec.program_path(), spawn_error);

  return child_pid;
}

fn fork_compound_stage(Maybe<descriptor> in_fd, Maybe<descriptor> out_fd,
                       Maybe<descriptor> err_fd) throws -> process
{
  LOG(verbosity::Debug, "forking a compound pipeline stage");

  const pid_t child_pid = check_syscall(fork());

  if (child_pid == 0) {
    /* A dup2 or close failure here throws through check_syscall. In the forked
       child that would unwind back into the parent's evaluator inside the
       duplicated process, running the parent's cleanup and never reaching
       _exit. The child must always terminate directly, so a failure in the
       descriptor setup is caught and reported, then the child exits. */
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
    } catch (const shit::Error &e) {
      String msg = e.message();
      msg += '\n';
      (void) write_fd(STDERR_FILENO, msg.data(), msg.count());
      exit_process_immediately(1);
    } catch (...) {
      LOG(verbosity::Debug,
          "swallowed an unknown error while preparing the forked stage child");
      exit_process_immediately(1);
    }
  }

  return child_pid;
}

[[noreturn]] fn exit_process_immediately(i32 status) wontthrow -> void
{
  _exit(status);
}

fn replace_process(ExecContext &&ec) throws -> void
{
  ASSERT(ec.args().count() > 0, "a program needs at least argv[0]");

  LOG(verbosity::Debug, "replacing the shell process with '%s'",
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

  execv(ec.program_path().c_str(),
        const_cast<char *const *>(child_args.begin()));

  /* execv returns only when it fails to replace the process. ENOEXEC means the
     file is executable but carries no shebang and is not a binary, so it is run
     as a shell script instead, the POSIX fallback, signalled to the caller by
     ExecFormatError. */
  if (errno == ENOEXEC) throw shit::ExecFormatError{};
  /* The program resolved but could not be executed, so the error carries the
     command's location for a caret and the caller exits 126 the way bash does
     for a file it found but could not run. */
  throw shit::ErrorWithLocation{
      ec.source_location(), "Unable to execute '" + ec.program_path().text() +
                                "' because " + last_system_error_message()};
}

fn redirect_self(const ExecContext &ec) throws -> void
{
  if (ec.in_fd) check_syscall(dup2(*ec.in_fd, STDIN_FILENO));
  if (ec.out_fd) check_syscall(dup2(*ec.out_fd, STDOUT_FILENO));
  if (ec.err_fd) check_syscall(dup2(*ec.err_fd, STDERR_FILENO));
}

fn make_pipe() wontthrow -> Maybe<Pipe>
{
  LOG(verbosity::Debug, "opening a close-on-exec pipe");

  descriptor p[2] = {SHIT_INVALID_FD, SHIT_INVALID_FD};

  if (pipe(p) != 0) {
    return shit::None;
  }

  /* Close the pipe ends on exec, so a stage that dups one end onto its stdin or
     stdout does not also inherit the other end. Otherwise a producer like yes
     keeps a read end open and never sees the pipe close. The dup2 onto a
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
  void (*entry)(void *);
  void *context;
};

fn thread_trampoline(void *raw_context) wontthrow -> void *
{
  let const start = static_cast<thread_start_context *>(raw_context);
  let const entry = start->entry;
  let const context = start->context;
  delete start;
  entry(context);
  return nullptr;
}

fn start_thread(void (*entry)(void *), void *context) wontthrow -> Maybe<thread>
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
  LOG(verbosity::Debug, "opening '%.*s'", static_cast<int>(path.length),
      path.data);

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
  LOG(verbosity::Debug, "writing %zu bytes into an anonymous temp file",
      content.count());

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

  lseek(fd, 0, SEEK_SET);
  return fd;
}

fn get_file_creation_mask() wontthrow -> u32
{
  /* umask only reads through a set, so the old value is read and put back. */
  const mode_t old = umask(0);
  umask(old);
  return static_cast<u32>(old);
}

fn set_file_creation_mask(u32 mask) wontthrow -> void
{
  umask(static_cast<mode_t>(mask));
}

fn wait_and_monitor_process(process pid) throws -> i32
{
  ASSERT(pid >= 0);

  LOG(verbosity::Debug, "waiting on process %lld", static_cast<long long>(pid));

  i32 status{};

  for (;;) {
    pid_t w = waitpid(pid, &status, 0);
    /* A signal interrupted the wait. Retry instead of failing. */
    if (w == -1 && errno == EINTR) continue;
    if (check_syscall(w) == pid) break;
  }

  /* Print appropriate message if the process was sent a signal. */
  if (WIFSIGNALED(status)) {
    const i32 sig = WTERMSIG(status);
    const char *sig_str = strsignal(sig);
    const String sig_desc = (sig_str != nullptr)
                                ? String{StringView{sig_str}}
                                : String{StringView{"Unknown"}};

    /* Ignore Ctrl-C. */
    if (sig & ~(SIGINT)) {
      shit::print("[Process " + utils::int_to_text(pid) + ": " + sig_desc +
                  ", signal " + utils::int_to_text(sig) + "]\n");
    } else {
      shit::print("\n");
    }

    return 128 + sig;
  } else if (WIFSTOPPED(status)) {
    const i32 sig = WSTOPSIG(status);
    const char *sig_str = strsignal(sig);
    const String sig_desc = (sig_str != nullptr)
                                ? String{StringView{sig_str}}
                                : String{StringView{"Unknown"}};

    shit::print("[Process " + utils::int_to_text(pid) + ": " + sig_desc +
                ", signal " + utils::int_to_text(sig) + " and killed]\n");

    /* We can't handle suspended processes yet, so goodbye. */
    check_syscall(kill(pid, SIGKILL));

    return 128 + SIGKILL;
  } else if (!WIFEXITED(status)) {
    /* Process was destroyed by otherworldly forces. */
    throw shit::Error{"???: " + last_system_error_message()};
  } else {
    /* We exited normally. */
    return WEXITSTATUS(status);
  }

  unreachable();
}

fn reap_process_quietly(process pid) throws -> i32
{
  ASSERT(pid >= 0);

  LOG(verbosity::Debug, "quietly reaping process %lld",
      static_cast<long long>(pid));

  i32 status{};
  for (;;) {
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

  /* Still running, or already reaped, which the job table also treats as done.
   */
  if (result == 0) return process_state::Running;
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
  /* A bare number names the signal directly. */
  if (!name.is_empty() &&
      std::all_of(name.data, name.data + name.length,
                  [](unsigned char c) { return std::isdigit(c) != 0; }))
  {
    const ErrorOr<i64> parsed = utils::parse_decimal_integer(name);
    if (parsed.is_error()) return shit::None;
    return static_cast<i32>(parsed.value());
  }

  String bare{name};
  if (bare.starts_with("SIG")) bare = String{bare.substring(3)};

  static constexpr StaticStringMap<i32>::entry NAME_ENTRIES[] = {
      {PackedStringKey::from_literal("HUP"),  SIGHUP },
      {PackedStringKey::from_literal("INT"),  SIGINT },
      {PackedStringKey::from_literal("QUIT"), SIGQUIT},
      {PackedStringKey::from_literal("KILL"), SIGKILL},
      {PackedStringKey::from_literal("TERM"), SIGTERM},
      {PackedStringKey::from_literal("STOP"), SIGSTOP},
      {PackedStringKey::from_literal("TSTP"), SIGTSTP},
      {PackedStringKey::from_literal("CONT"), SIGCONT},
      {PackedStringKey::from_literal("USR1"), SIGUSR1},
      {PackedStringKey::from_literal("USR2"), SIGUSR2},
      {PackedStringKey::from_literal("ABRT"), SIGABRT},
      {PackedStringKey::from_literal("ALRM"), SIGALRM},
      {PackedStringKey::from_literal("PIPE"), SIGPIPE},
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
  for (const signal_pair &pair : SIGNAL_PAIRS)
    if (pair.number == number) return String{pair.name};
  return shit::None;
}

fn signal_names() throws -> const ArrayList<StringView> &
{
  static ArrayList<StringView> names = [] throws {
    let collected = ArrayList<StringView>{};
    collected.reserve(sizeof(SIGNAL_PAIRS) / sizeof(SIGNAL_PAIRS[0]));
    for (const signal_pair &pair : SIGNAL_PAIRS)
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

  for (const String &arg : args)
    result.push(arg.c_str());

  result.push(nullptr);

  return result;
}

cold fn last_system_error_message() throws -> String
{
  return String{StringView{strerror(errno)}};
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

volatile sig_atomic_t CHILD_STATE_CHANGED = 0;

static fn sigchild_handler(int n, siginfo_t *siginfo, void *ctx) wontthrow
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
  LOG(verbosity::Debug, "restoring the default signal dispositions");

  sigset_t sm;
  sigfillset(&sm);
  check_syscall(sigprocmask(SIG_UNBLOCK, &sm, nullptr));

  struct sigaction sa = {};
  sa.sa_handler = SIG_DFL;
  check_syscall(sigaction(SIGCHLD, &sa, nullptr));

  /* A forked compound-pipeline child inherits the flag value at fork. A stale
     one would make the evaluator throw Interrupted before the child runs, so it
     is cleared here alongside the handler reset. */
  INTERRUPT_REQUESTED = 0;
}

volatile sig_atomic_t INTERRUPT_REQUESTED = 0;

static fn handle_interrupt(int s) wontthrow -> void
{
  unused(s);
  /* Setting the flag is the only async-signal-safe action. The evaluator polls
     it and aborts the running command, so a shell-internal loop is stoppable.
   */
  INTERRUPT_REQUESTED = 1;
}

fn set_default_signal_handlers() throws -> void
{
  LOG(verbosity::Info,
      "blocking the terminal signals and installing the shell handlers");

  /* The terminal-generated signals that would kill the shell stay blocked, but
     SIGINT gets a handler instead, so a Ctrl-C in a shell loop sets the flag
     the evaluator polls rather than spinning forever. An external command
     resets the handler to the default through execv and so still dies on
     Ctrl-C. */
  sigset_t sm = make_sigset(SIGTERM, SIGQUIT, SIGHUP, SIGSTOP, SIGTSTP);
  check_syscall(sigprocmask(SIG_BLOCK, &sm, nullptr));

  struct sigaction sa = {};
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = sigchild_handler;
  check_syscall(sigaction(SIGCHLD, &sa, nullptr));

  struct sigaction si = {};
  si.sa_handler = handle_interrupt;
  check_syscall(sigaction(SIGINT, &si, nullptr));
}

volatile sig_atomic_t SIGNAL_PENDING = 0;

/* One pending flag per signal number, set by the trap handler and cleared by
   the drain. The size covers every real-time and standard signal the platform
   defines, with a fixed bound so the array stays a flat block the handler may
   touch async-safely. */
static constexpr i32 SIGNAL_FLAG_COUNT = 128;
static volatile sig_atomic_t PENDING_SIGNAL_FLAGS[SIGNAL_FLAG_COUNT] = {};

static fn handle_trapped_signal(int signal_number) wontthrow -> void
{
  /* Recording the arrival is the only async-safe action. The evaluator drains
     the flag at the next command boundary and runs the trap action there. */
  if (signal_number > 0 && signal_number < SIGNAL_FLAG_COUNT)
    PENDING_SIGNAL_FLAGS[signal_number] = 1;
  SIGNAL_PENDING = 1;
}

fn set_trap_handler(i32 signal_number) throws -> void
{
  if (signal_number <= 0 || signal_number >= SIGNAL_FLAG_COUNT) return;

  LOG(verbosity::Info, "installing the trap handler for signal %d",
      signal_number);

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
  if (signal_number <= 0 || signal_number >= SIGNAL_FLAG_COUNT) return;
  LOG(verbosity::Info, "ignoring signal %d", signal_number);
  struct sigaction sa = {};
  sa.sa_handler = SIG_IGN;
  check_syscall(sigaction(signal_number, &sa, nullptr));
}

fn clear_trap_handler(i32 signal_number) throws -> void
{
  if (signal_number <= 0 || signal_number >= SIGNAL_FLAG_COUNT) return;
  LOG(verbosity::Info, "clearing the trap for signal %d", signal_number);
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

fn take_pending_signal() wontthrow -> i32
{
  /* The fast SIGNAL_PENDING flag is owned by the drain, which clears it before
     consuming, so this only reports and clears the per-signal flags. */
  for (i32 number = 1; number < SIGNAL_FLAG_COUNT; number++) {
    if (PENDING_SIGNAL_FLAGS[number] != 0) {
      PENDING_SIGNAL_FLAGS[number] = 0;
      return number;
    }
  }
  return 0;
}

fn monotonic_nanos() wontthrow -> u64
{
  struct timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
  return static_cast<u64>(now.tv_sec) * 1000000000ULL +
         static_cast<u64>(now.tv_nsec);
}

fn realtime_microseconds() wontthrow -> u64
{
  struct timespec now{};
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) return 0;
  return static_cast<u64>(now.tv_sec) * 1000000ULL +
         static_cast<u64>(now.tv_nsec) / 1000ULL;
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
  for (;;) {
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

} /* namespace */

fn run_measured(const ArrayList<String> &argv, bool suppress_output) throws
    -> Maybe<measured_result>
{
  if (argv.is_empty()) return None;

  measured_result result{};

  i64 status = 0;
  u64 peak_rss = 0;
  bool forked_ok = false;

  const u64 start_nanos = monotonic_nanos();

#if defined __linux__
  result.has_perf = collect_perf_counts(result.perf, [&]() wontthrow {
    forked_ok = fork_exec_wait4(argv, suppress_output, status, peak_rss);
  });
  if (!result.has_perf)
    forked_ok = fork_exec_wait4(argv, suppress_output, status, peak_rss);
#else
  forked_ok = fork_exec_wait4(argv, suppress_output, status, peak_rss);
#endif

  result.wall_nanos = monotonic_nanos() - start_nanos;

  if (!forked_ok) return None;

  result.exit_status = status;
  result.peak_rss_bytes = peak_rss;
  return result;
}

} /* namespace os */

} /* namespace shit */

#elif SHIT_PLATFORM_IS WIN32

#include <io.h>
#include <psapi.h>

namespace shit {

namespace os {

fn write_fd(os::descriptor fd, const void *buf, usize size) wontthrow
    -> Maybe<usize>
{
  DWORD w = -1;
  if (WriteFile(fd, buf, size, &w, 0) == FALSE) /* NOLINT */
    return shit::None;
  return static_cast<usize>(w);
}

fn read_fd(os::descriptor fd, void *buf, usize size) wontthrow -> Maybe<usize>
{
  DWORD r = -1;
  if (ReadFile(fd, buf, size, &r, 0) == FALSE) /* NOLINT */
    return shit::None;
  return static_cast<usize>(r);
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
    result.dup2_ok = false;
    return result;
  }

  /* An invalid target handle, as from a duplication onto a closed descriptor,
     is reported so the caller fails the command rather than redirecting onto a
     dead handle. */
  if (target == INVALID_HANDLE_VALUE) {
    result.dup2_ok = false;
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
    result.dup2_ok = false;
    return result;
  }
  SetStdHandle(*slot, duplicate);
  result.replacement = duplicate;
  result.dup2_ok = true;
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
  if (saved.dup2_ok && saved.replacement != INVALID_HANDLE_VALUE)
    CloseHandle(saved.replacement);
  if (saved.was_open) SetStdHandle(*slot, saved.saved);
}

fn save_descriptor(i32 shell_fd) wontthrow -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) {
    result.dup2_ok = false;
    return result;
  }
  result.saved = GetStdHandle(*slot);
  result.was_open = result.saved != INVALID_HANDLE_VALUE;
  /* No replacement handle is installed, so the restore only puts the slot
     back. */
  result.replacement = INVALID_HANDLE_VALUE;
  result.dup2_ok = true;
  return result;
}

/* Windows has no /dev/tty rebind for the console, so the recovery reports
   failure and the caller keeps its error path. */
fn reopen_terminal_as_stdin() wontthrow -> bool { return false; }

fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) return SHIT_INVALID_FD;
  return GetStdHandle(*slot);
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

fn get_shell_process_id() wontthrow -> i64
{
  return static_cast<i64>(PARENT_SHELL_PID);
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
  return String{StringView{buffer}};
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

fn execute_program(ExecContext &&ec, bool allow_script_fallback) -> process
{
  /* Windows has no ENOEXEC interpreter convention, so the script fallback the
     POSIX path offers does not apply here. */
  unused(allow_script_fallback);

  LOG(verbosity::Debug, "spawning '%s' with %zu arguments",
      ec.program_path().c_str(), ec.args().count());

  String command_line = make_os_args(ec.args());

  PROCESS_INFORMATION process_info{};
  STARTUPINFOA startup_info{};

  startup_info.cb = sizeof(startup_info);

  BOOL needs_handles = ec.in_fd || ec.out_fd || ec.err_fd ||
                       ec.dup_err_to_out || ec.dup_out_to_err;

  if (needs_handles) startup_info.dwFlags |= STARTF_USESTDHANDLES;

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
  if (needs_handles) {
    SetHandleInformation(startup_info.hStdInput, HANDLE_FLAG_INHERIT,
                         HANDLE_FLAG_INHERIT);
    SetHandleInformation(startup_info.hStdOutput, HANDLE_FLAG_INHERIT,
                         HANDLE_FLAG_INHERIT);
    SetHandleInformation(startup_info.hStdError, HANDLE_FLAG_INHERIT,
                         HANDLE_FLAG_INHERIT);
  }

  /* CreateProcessA may rewrite lpCommandLine in place, so the owned buffer is
     handed over as a mutable pointer rather than a const view. */
  if (CreateProcessA(ec.program_path().c_str(),
                     const_cast<LPSTR>(command_line.data()), nullptr, nullptr,
                     needs_handles, 0, nullptr, nullptr, &startup_info,
                     &process_info) == 0)
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
  if (bash_compatible)
    arguments.push(String{heap_allocator(), StringView{"--bash-compatible"}});
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
  if (bash_compatible)
    arguments.push(String{heap_allocator(), StringView{"--bash-compatible"}});
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
  LOG(verbosity::Debug, "running '%s' to completion in place of an exec",
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
  SECURITY_ATTRIBUTES att{};

  att.nLength = sizeof(SECURITY_ATTRIBUTES);
  /* Both ends are non-inheritable, so a child only receives the end execute
     handles explicitly through STARTF_USESTDHANDLES, like close-on-exec. */
  att.bInheritHandle = FALSE;
  att.lpSecurityDescriptor = nullptr; /* NOLINT */

  HANDLE in = INVALID_HANDLE_VALUE;
  HANDLE out = INVALID_HANDLE_VALUE;

  if (CreatePipe(&in, &out, &att, 0) == 0) {
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
  void (*entry)(void *);
  void *context;
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

fn start_thread(void (*entry)(void *), void *context) wontthrow -> Maybe<thread>
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
  /* CREATE_NEW fails when the file already exists, the way noclobber wants. */
  case file_open_mode::TruncateNoClobber: disposition = CREATE_NEW; break;
  case file_open_mode::Append: disposition = OPEN_ALWAYS; break;
  case file_open_mode::Read: disposition = OPEN_EXISTING; break;
  /* OPEN_ALWAYS opens the file or creates it, the way <> needs. */
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

  /* Append moves the write position to the end of the file. */
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

fn get_file_creation_mask() wontthrow -> u32
{
  int old = _umask(0);
  _umask(old);
  return static_cast<u32>(old);
}

fn set_file_creation_mask(u32 mask) wontthrow -> void
{
  _umask(static_cast<int>(mask));
}

fn wait_and_monitor_process(process p) -> i32
{
  if (WaitForSingleObject(p, INFINITE) != WAIT_OBJECT_0)
    throw Error{"could not wait for the process to finish: " +
                last_system_error_message()};

  DWORD code = -1;
  if (GetExitCodeProcess(p, &code) == 0)
    throw Error{"could not read the process exit code: " +
                last_system_error_message()};

  return code;
}

fn reap_process_quietly(process p) -> i32
{
  if (WaitForSingleObject(p, INFINITE) != WAIT_OBJECT_0)
    throw Error{"could not wait for the process to finish: " +
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
  if (signal_number == 9 || signal_number == 15)
    return TerminateProcess(p, 1) != 0;
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
  if (!name.is_empty() &&
      std::all_of(name.data, name.data + name.length,
                  [](unsigned char c) { return std::isdigit(c) != 0; }))
  {
    String name_string{name};
    return static_cast<i32>(std::strtol(name_string.c_str(), nullptr, 10));
  }

  String bare{name};
  if (bare.starts_with("SIG")) bare = String{bare.substring(3)};
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
  /* The names the Windows signal_number_from_name accepts. */
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
  bool needs_quotes = arg.count() == 0;
  for (usize i = 0; i < arg.count() && !needs_quotes; i++) {
    const char c = arg[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"')
      needs_quotes = true;
  }
  if (!needs_quotes) {
    out.append(arg);
    return;
  }

  out += '"';
  for (usize i = 0; i < arg.count();) {
    usize backslashes = 0;
    while (i < arg.count() && arg[i] == '\\') {
      i++;
      backslashes++;
    }
    if (i == arg.count()) {
      /* Trailing backslashes precede the closing quote, so they are doubled to
         stay literal rather than escaping the quote. */
      for (usize k = 0; k < backslashes * 2; k++)
        out += '\\';
      break;
    }
    if (arg[i] == '"') {
      /* The backslashes before a quote are doubled and the quote is escaped. */
      for (usize k = 0; k < backslashes * 2 + 1; k++)
        out += '\\';
      out += '"';
      i++;
    } else {
      /* Backslashes that do not precede a quote stay literal. */
      for (usize k = 0; k < backslashes; k++)
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

  String s{};
  append_windows_quoted_arg(s, args[0].view());
  for (usize i = 1; i < args.count(); i++) {
    s += ' ';
    append_windows_quoted_arg(s, args[i].view());
  }

  return s;
}

fn last_system_error_message() -> String
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
           StringView{" (Error message couldn't be proccessed due to "
                      "FormatMessage() fail)"};
  }

  StringView view{static_cast<char *>(errno_str)};
  /* I do not want the PERIOD. */
  if (view.find_character('.') || view.find_character(' ') ||
      view.find_character('\n'))
  {
    view = view.substring_of_length(0, view.length >= 3 ? view.length - 3 : 0);
  }

  /* The result is rebuilt rather than edited in place, so each kept byte is
     pushed and a %N insert is replaced with a literal word. */
  String err{};
  for (usize i = 0; i < view.length; i++) {
    /* Remove stupid inserts. I can't stand Windows */
    if (view[i] == '%' && i + 1 < view.length && isdigit(view[i + 1])) {
      /* Replace %N bullshit with just "Input". */
      err += StringView{"input"};
      i++;
      continue;
    }
    err.push(view[i]);
  }

  LocalFree(errno_str);

  if (err.length() > 0) {
    /* Capitalize first letter to sound formal. */
    String capitalized{};
    capitalized.push(static_cast<char>(toupper(err[0])));
    capitalized += err.substring(1);
    err = steal(capitalized);
  }

  return err;
}

volatile sig_atomic_t INTERRUPT_REQUESTED = 0;

/* Windows has no SIGCHLD, so the flag exists for the shared readers and
   stays down. */
volatile sig_atomic_t CHILD_STATE_CHANGED = 0;

static fn handle_interrupt(int s) -> void
{
  unused(s);
  /* Only set the flag, since printing or any non-async-signal-safe work inside
     a handler is undefined. The evaluator polls the flag and aborts the running
     command, so an infinite loop can be stopped from the keyboard. */
  INTERRUPT_REQUESTED = 1;
  signal(SIGINT, handle_interrupt);
}

fn set_default_signal_handlers() -> void
{
  if (signal(SIGTERM, SIG_IGN) == SIG_ERR ||
      signal(SIGINT, handle_interrupt) == SIG_ERR)
  {
    throw Error{"could not install the signal handlers: " +
                last_system_error_message()};
  }
}

fn reset_signal_handlers() -> void
{
  if (signal(SIGTERM, SIG_DFL) == SIG_ERR || signal(SIGINT, SIG_DFL) == SIG_ERR)
  {
    throw Error{"could not restore the default signal handlers: " +
                last_system_error_message()};
  }

  /* A child started for a compound pipeline stage inherits the flag value at
     spawn. A stale one would make the evaluator throw Interrupted before the
     child runs, so it is cleared here alongside the handler reset. */
  INTERRUPT_REQUESTED = 0;
}

volatile sig_atomic_t SIGNAL_PENDING = 0;

static constexpr i32 SIGNAL_FLAG_COUNT = 128;
static volatile sig_atomic_t PENDING_SIGNAL_FLAGS[SIGNAL_FLAG_COUNT] = {};

static fn handle_trapped_signal(int signal_number) -> void
{
  if (signal_number > 0 && signal_number < SIGNAL_FLAG_COUNT)
    PENDING_SIGNAL_FLAGS[signal_number] = 1;
  SIGNAL_PENDING = 1;
  /* The C runtime resets the disposition to default before the handler runs, so
     it is reinstalled here for the next arrival, the way the interrupt handler
     reinstalls itself. */
  signal(signal_number, handle_trapped_signal);
}

fn set_trap_handler(i32 signal_number) -> void
{
  if (signal_number <= 0 || signal_number >= SIGNAL_FLAG_COUNT) return;
  signal(signal_number, handle_trapped_signal);
}

fn set_trap_ignore(i32 signal_number) -> void
{
  if (signal_number <= 0 || signal_number >= SIGNAL_FLAG_COUNT) return;
  signal(signal_number, SIG_IGN);
}

fn clear_trap_handler(i32 signal_number) -> void
{
  if (signal_number <= 0 || signal_number >= SIGNAL_FLAG_COUNT) return;
  if (signal_number == SIGINT)
    signal(signal_number, handle_interrupt);
  else
    signal(signal_number, SIG_DFL);
}

fn take_pending_signal() wontthrow -> i32
{
  /* The fast SIGNAL_PENDING flag is owned by the drain, which clears it before
     consuming, so this only reports and clears the per-signal flags. */
  for (i32 number = 1; number < SIGNAL_FLAG_COUNT; number++) {
    if (PENDING_SIGNAL_FLAGS[number] != 0) {
      PENDING_SIGNAL_FLAGS[number] = 0;
      return number;
    }
  }
  return 0;
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

} /* namespace os */

} /* namespace shit */

#endif /* PLATFORM_IS(WIN32) */

#if SHIT_PLATFORM_IS COSMO or SHIT_PLATFORM_IS WIN32

namespace shit {

namespace os {

const ArrayList<String> OMITTED_SUFFIXES = []() {
  ArrayList<String> suffixes{};
  /* First extension entry should be empty. */
  for (const char *suffix : {"", ".exe", ".com", ".scr", ".bat"})
    suffixes.push(String{StringView{suffix}});
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

      if (usize i = utils::find_pos_in_vec(OMITTED_SUFFIXES, extension);
          i != utils::NOT_FOUND_INDEX)
      {
        program_name =
            String{program_name.substring_of_length(0, *extension_pos)};
        return i;
      }
    }
  }

  return 0;
}

} /* namespace os */

} /* namespace shit */

#endif /* COSMO || WIN32 */
