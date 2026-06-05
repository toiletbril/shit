#include "Platform.hpp"

#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"

#include <csignal>
#include <cstdarg>
#include <cstring>

#if SHIT_PLATFORM_IS POSIX
#include <fcntl.h>
#include <sys/stat.h>
#endif

#if SHIT_PLATFORM_IS POSIX

namespace shit {

namespace os {

Maybe<usize> write_fd(os::descriptor fd, const void *buf, usize size)
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

Maybe<usize> read_fd(os::descriptor fd, void *buf, usize size)
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

bool close_fd(os::descriptor fd) { return close(fd) != -1; }

os::descriptor redirect_stdout(os::descriptor target)
{
  /* The saved copy of the real stdout is close-on-exec, so a forked command
     does not inherit it and hold the shell's own output open. An immortal
     pipeline stage like yes would otherwise keep a downstream reader from ever
     seeing end of input. */
  os::descriptor saved = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
  dup2(target, STDOUT_FILENO);
  /* The original write end is close-on-exec for the same reason. The duplicate
     now living on STDOUT_FILENO stays open for the command to write to. */
  if (int flags = fcntl(target, F_GETFD); flags != -1)
    fcntl(target, F_SETFD, flags | FD_CLOEXEC);
  return saved;
}

void restore_stdout(os::descriptor saved)
{
  dup2(saved, STDOUT_FILENO);
  close(saved);
}

Maybe<String> get_current_user()
{
  struct passwd *pw = getpwuid(getuid());
  if (pw != NULL) return String{StringView{pw->pw_name}};
  return shit::None;
}

Maybe<Path> get_home_directory()
{
  if (Maybe<String> home = get_environment_variable("HOME"))
    return Path{StringView{*home}};
  return shit::None;
}

static const pid_t PARENT_SHELL_PID = getpid();

bool is_child_process() { return getpid() != PARENT_SHELL_PID; }

i64 get_shell_process_id() { return static_cast<i64>(PARENT_SHELL_PID); }

i64 process_id_of(process p) { return static_cast<i64>(p); }

bool is_stdin_a_tty() { return isatty(SHIT_STDIN); }

bool is_stdout_a_tty() { return isatty(SHIT_STDOUT); }

/* Cosmopolitan binaries can be run on both Linux and Windows. This will be
 * replaced by a runtime check. */
#if SHIT_PLATFORM_ISNT COSMO
const ArrayList<String> OMITTED_SUFFIXES = []() {
  ArrayList<String> suffixes{};
  suffixes.push(String{StringView{""}});
  return suffixes;
}();

ExtIndex erase_extension_and_get_its_index(std::string &program_name)
{
  /* POSIX does not really make use of extensions for executable files. */
  SHIT_UNUSED(program_name);
  return false;
}
#endif /* !COSMO */

Maybe<String> get_environment_variable(StringView key)
{
  String key_string{key};
  const char *e = std::getenv(key_string.c_str());
  if (e != NULL) return String{StringView{e}};
  return shit::None;
}

void set_environment_variable(StringView key, StringView value)
{
  String key_string{key};
  String value_string{value};
  setenv(key_string.c_str(), value_string.c_str(), 1);
}

void unset_environment_variable(StringView key)
{
  String key_string{key};
  unsetenv(key_string.c_str());
}

i32 check_syscall_impl(i32 status, StringView invocation)
{
  if (status == -1) {
    throw shit::Error{"'" + invocation +
                      "' fail: " + last_system_error_message()};
  }

  return status;
}

#define check_syscall(fn) check_syscall_impl(fn, #fn)

process execute_program(ExecContext &&ec)
{
  SHIT_DEFER { ec.close_fds(); };

  pid_t child_pid = check_syscall(fork());

  if (child_pid == 0) {
    os_args child_args = make_os_args(ec.args());

    if (ec.in_fd) {
      check_syscall(dup2(*ec.in_fd, STDIN_FILENO));
      check_syscall(close(*ec.in_fd));
    }
    if (ec.out_fd) {
      check_syscall(dup2(*ec.out_fd, STDOUT_FILENO));
      check_syscall(close(*ec.out_fd));
    }
    if (ec.err_fd) {
      check_syscall(dup2(*ec.err_fd, STDERR_FILENO));
      check_syscall(close(*ec.err_fd));
    }
    /* The descriptor duplications come after the files are placed, so 2>&1 sees
       the final standard output. */
    if (ec.dup_err_to_out) check_syscall(dup2(STDOUT_FILENO, STDERR_FILENO));
    if (ec.dup_out_to_err) check_syscall(dup2(STDERR_FILENO, STDOUT_FILENO));

    reset_signal_handlers();

    /* TODO: If execv() failed, try to execute the path as a shell script.
     */
    if (execv(ec.program_path().c_str(),
              const_cast<char *const *>(child_args.begin())) == -1)
    {
      /* We are the forked child. Report the failure and terminate the child
       * directly. Throwing here would unwind back into the parent's evaluator
       * inside the duplicated process. */
      String msg{};
      msg += ec.program_path().text();
      msg += ": ";
      msg += last_system_error_message();
      msg += '\n';
      (void) write_fd(STDERR_FILENO, msg.data(), msg.size());
      _exit(127);
    }
  }

  ec.close_fds();

  return child_pid;
}

void replace_process(ExecContext &&ec)
{
  os_args child_args = make_os_args(ec.args());

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
  if (ec.dup_err_to_out) check_syscall(dup2(STDOUT_FILENO, STDERR_FILENO));
  if (ec.dup_out_to_err) check_syscall(dup2(STDERR_FILENO, STDOUT_FILENO));

  reset_signal_handlers();

  execv(ec.program_path().c_str(),
        const_cast<char *const *>(child_args.begin()));

  /* execv returns only when it fails to replace the process. */
  throw shit::Error{ec.program_path().text() + ": " +
                    last_system_error_message()};
}

void redirect_self(const ExecContext &ec)
{
  if (ec.in_fd) check_syscall(dup2(*ec.in_fd, STDIN_FILENO));
  if (ec.out_fd) check_syscall(dup2(*ec.out_fd, STDOUT_FILENO));
  if (ec.err_fd) check_syscall(dup2(*ec.err_fd, STDERR_FILENO));
}

Maybe<Pipe> make_pipe()
{
  descriptor p[2] = {SHIT_INVALID_FD, SHIT_INVALID_FD};

  if (pipe(p) != 0) {
    return shit::None;
  }

  /* Close the pipe ends on exec, so a stage that dups one end onto its stdin or
     stdout does not also inherit the other end. Otherwise a producer like yes
     keeps a read end open and never sees the pipe close. The dup2 onto a
     standard descriptor clears the flag there, so the redirection survives. */
  for (descriptor end : p) {
    int flags = fcntl(end, F_GETFD);
    if (flags != -1) fcntl(end, F_SETFD, flags | FD_CLOEXEC);
  }

  return Pipe{p[0], p[1]};
}

Maybe<descriptor> open_file_descriptor(StringView path, FileOpenMode mode)
{
  int flags = 0;
  switch (mode) {
  case FileOpenMode::Truncate: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
  case FileOpenMode::TruncateNoClobber:
    /* O_EXCL makes the create fail atomically when the file already exists, the
       way noclobber requires. */
    flags = O_WRONLY | O_CREAT | O_EXCL;
    break;
  case FileOpenMode::Append: flags = O_WRONLY | O_CREAT | O_APPEND; break;
  case FileOpenMode::Read: flags = O_RDONLY; break;
  }

  /* ::open needs a null-terminated path, so the view is copied into a String
     that owns a trailing null. */
  String path_string{path};
  /* 0666 lets the umask decide the final permissions, as a shell redirection
     does. */
  int fd = ::open(path_string.c_str(), flags, 0666);
  if (fd < 0) return shit::None;
  return fd;
}

Maybe<descriptor> write_to_temp_file(StringView content)
{
  /* The temp directory is resolved at runtime rather than hardcoded to /tmp,
     so a cosmo binary running on Windows writes to the Windows temp directory
     where /tmp does not exist. */
  Path temp_dir = Path::temp_directory();

  Path path_template_path =
      PathBuilder{temp_dir.text()}.append("shit_heredoc_XXXXXX").build();

  /* mkstemp rewrites the XXXXXX suffix in place, so the template lives in a
     mutable buffer with a trailing null rather than the immutable Path text. */
  const String &path_template_text = path_template_path.text();
  ArrayList<char> path_template{};
  path_template.reserve(path_template_text.size() + 1);
  for (usize i = 0; i < path_template_text.size(); i++)
    path_template.push(path_template_text.c_str()[i]);
  path_template.push('\0');

  int fd = mkstemp(path_template.begin());
  if (fd < 0) return shit::None;

  /* Unlink at once, so the file is anonymous and is freed when closed. */
  unlink(path_template.begin());

  usize offset = 0;
  while (offset < content.size()) {
    ssize_t written =
        ::write(fd, content.data + offset, content.size() - offset);
    if (written <= 0) {
      close(fd);
      return shit::None;
    }
    offset += static_cast<usize>(written);
  }

  lseek(fd, 0, SEEK_SET);
  return fd;
}

u32 get_file_creation_mask()
{
  /* umask only reads through a set, so the old value is read and put back. */
  mode_t old = umask(0);
  umask(old);
  return static_cast<u32>(old);
}

void set_file_creation_mask(u32 mask) { umask(static_cast<mode_t>(mask)); }

i32 wait_and_monitor_process(process pid)
{
  SHIT_ASSERT(pid >= 0);

  i32 status{};

  for (;;) {
    pid_t w = waitpid(pid, &status, 0);
    /* A signal interrupted the wait. Retry instead of failing. */
    if (w == -1 && errno == EINTR) continue;
    if (check_syscall(w) == pid) break;
  }

  /* Print appropriate message if the process was sent a signal. */
  if (WIFSIGNALED(status)) {
    i32 sig = WTERMSIG(status);
    const char *sig_str = strsignal(sig);
    String sig_desc = (sig_str != NULL) ? String{StringView{sig_str}}
                                        : String{StringView{"Unknown"}};

    /* Ignore Ctrl-C. */
    if (sig & ~(SIGINT)) {
      shit::print(
          "[Process " + utils::integer_to_string(pid) + ": " + sig_desc +
          ", signal " + utils::integer_to_string(sig) + "]\n");
    } else {
      shit::print("\n");
    }

    return 128 + sig;
  } else if (WIFSTOPPED(status)) {
    i32 sig = WSTOPSIG(status);
    const char *sig_str = strsignal(sig);
    String sig_desc = (sig_str != NULL) ? String{StringView{sig_str}}
                                        : String{StringView{"Unknown"}};

    shit::print(
        "[Process " + utils::integer_to_string(pid) + ": " + sig_desc +
        ", signal " + utils::integer_to_string(sig) + " and killed]\n");

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

  SHIT_UNREACHABLE();
}

ProcessState poll_process(process p, i32 &status_out)
{
  i32 status = 0;
  pid_t result = waitpid(p, &status, WNOHANG | WUNTRACED | WCONTINUED);

  /* Still running, or already reaped, which the job table also treats as done.
   */
  if (result == 0) return ProcessState::Running;
  if (result == -1) {
    status_out = 0;
    return ProcessState::Exited;
  }

  if (WIFSTOPPED(status)) return ProcessState::Stopped;
  if (WIFCONTINUED(status)) return ProcessState::Running;
  if (WIFSIGNALED(status)) {
    status_out = 128 + WTERMSIG(status);
    return ProcessState::Exited;
  }
  status_out = WEXITSTATUS(status);
  return ProcessState::Exited;
}

bool signal_process(process p, i32 signal_number)
{
  return kill(p, signal_number) == 0;
}

process process_from_pid(i64 pid) { return static_cast<process>(pid); }

Maybe<i32> signal_number_from_name(StringView name)
{
  /* A bare number names the signal directly. */
  if (!name.empty() &&
      std::all_of(name.data, name.data + name.length,
                  [](unsigned char c) { return std::isdigit(c) != 0; }))
  {
    ErrorOr<i64> parsed = utils::parse_decimal_integer(name);
    if (parsed.is_error()) return shit::None;
    return static_cast<i32>(parsed.value());
  }

  String bare{name};
  if (bare.starts_with("SIG")) bare = String{bare.substring(3)};

  static constexpr StaticStringMap<i32>::Entry NAME_ENTRIES[] = {
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

os_args make_os_args(const ArrayList<String> &args)
{
  os_args result{};
  result.reserve(args.size() + 1);

  for (const String &arg : args)
    result.push(arg.c_str());

  result.push(nullptr);

  return result;
}

String last_system_error_message()
{
  return String{StringView{strerror(errno)}};
}

static sigset_t make_sigset_impl(int first, ...)
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

static void sigchild_handler(int n, siginfo_t *siginfo, void *ctx)
{
  SHIT_UNUSED(n);
  SHIT_UNUSED(ctx);
  SHIT_UNUSED(siginfo);
}

void reset_signal_handlers()
{
  sigset_t sm;
  sigfillset(&sm);
  check_syscall(sigprocmask(SIG_UNBLOCK, &sm, nullptr));

  struct sigaction sa = {};
  sa.sa_handler = SIG_DFL;
  check_syscall(sigaction(SIGCHLD, &sa, nullptr));
}

void set_default_signal_handlers()
{
  sigset_t sm = make_sigset(SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGSTOP, SIGTSTP);
  check_syscall(sigprocmask(SIG_BLOCK, &sm, nullptr));

  struct sigaction sa = {};
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = sigchild_handler;
  check_syscall(sigaction(SIGCHLD, &sa, nullptr));
}

} /* namespace os */

} /* namespace shit */

#elif SHIT_PLATFORM_IS WIN32

#include <io.h>

namespace shit {

namespace os {

Maybe<usize> write_fd(os::descriptor fd, const void *buf, usize size)
{
  DWORD w = -1;
  if (WriteFile(fd, buf, size, &w, 0) == FALSE) /* NOLINT */
    return shit::None;
  return static_cast<usize>(w);
}

Maybe<usize> read_fd(os::descriptor fd, void *buf, usize size)
{
  DWORD r = -1;
  if (ReadFile(fd, buf, size, &r, 0) == FALSE) /* NOLINT */
    return shit::None;
  return static_cast<usize>(r);
}

bool close_fd(os::descriptor fd) { return CloseHandle(fd); }

os::descriptor redirect_stdout(os::descriptor target)
{
  os::descriptor saved = GetStdHandle(STD_OUTPUT_HANDLE);
  SetStdHandle(STD_OUTPUT_HANDLE, target);
  return saved;
}

void restore_stdout(os::descriptor saved)
{
  SetStdHandle(STD_OUTPUT_HANDLE, saved);
}

Maybe<String> get_current_user()
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

Maybe<Path> get_home_directory()
{
  if (Maybe<String> home = get_environment_variable("USERPROFILE"))
    return Path{StringView{*home}};
  return shit::None;
}

static const DWORD PARENT_SHELL_PID = GetCurrentProcessId();

bool is_child_process() { return GetCurrentProcessId() != PARENT_SHELL_PID; }

i64 get_shell_process_id() { return static_cast<i64>(PARENT_SHELL_PID); }

i64 process_id_of(process p) { return static_cast<i64>(GetProcessId(p)); }

bool is_stdin_a_tty() { return _isatty(_fileno(stdin)); }

bool is_stdout_a_tty() { return _isatty(_fileno(stdout)); }

constexpr static usize WIN32_MAX_ENV_SIZE = 32767;

Maybe<String> get_environment_variable(StringView key)
{
  String key_string{key};
  char buffer[WIN32_MAX_ENV_SIZE] = {0};
  if (GetEnvironmentVariableA(key_string.c_str(), buffer, sizeof(buffer)) == 0)
    return shit::None;
  return String{StringView{buffer}};
}

void set_environment_variable(StringView key, StringView value)
{
  String key_string{key};
  String value_string{value};
  SetEnvironmentVariableA(key_string.c_str(), value_string.c_str());
}

void unset_environment_variable(StringView key)
{
  String key_string{key};
  SetEnvironmentVariableA(key_string.c_str(), nullptr);
}

process execute_program(ExecContext &&ec)
{
  std::string command_line = make_os_args(ec.args());

  PROCESS_INFORMATION process_info{};
  STARTUPINFOA startup_info{};

  startup_info.cb = sizeof(startup_info);

  BOOL needs_handles = ec.in_fd || ec.out_fd || ec.err_fd ||
                       ec.dup_err_to_out || ec.dup_out_to_err;

  if (needs_handles) startup_info.dwFlags |= STARTF_USESTDHANDLES;

  startup_info.hStdInput = ec.in_fd.value_or(GetStdHandle(STD_INPUT_HANDLE));
  startup_info.hStdOutput = ec.out_fd.value_or(GetStdHandle(STD_OUTPUT_HANDLE));
  startup_info.hStdError = ec.err_fd.value_or(GetStdHandle(STD_ERROR_HANDLE));

  /* Apply the descriptor duplications in the same order as the POSIX path, so
     2>&1 routes stderr to the current stdout and 1>&2 the reverse. */
  if (ec.dup_err_to_out) startup_info.hStdError = startup_info.hStdOutput;
  if (ec.dup_out_to_err) startup_info.hStdOutput = startup_info.hStdError;

  SHIT_DEFER
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

  if (CreateProcessA(ec.program_path().c_str(), command_line.data(), nullptr,
                     nullptr, needs_handles, 0, nullptr, nullptr, &startup_info,
                     &process_info) == 0)
  {
    throw ErrorWithLocation{ec.source_location(), last_system_error_message()};
  }

  return process_info.hProcess;
}

void replace_process(ExecContext &&ec)
{
  /* Windows cannot replace a process in place, so the program runs to
     completion and the shell exits with its status, which behaves like exec for
     a launched script. */
  process child = execute_program(std::move(ec));
  i32 status = wait_and_monitor_process(child);
  ExitProcess(static_cast<UINT>(status));
  SHIT_UNREACHABLE();
}

void redirect_self(const ExecContext &ec)
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

Maybe<Pipe> make_pipe()
{
  SECURITY_ATTRIBUTES att{};

  att.nLength = sizeof(SECURITY_ATTRIBUTES);
  /* Both ends are non-inheritable, so a child only receives the end execute
     handles explicitly through STARTF_USESTDHANDLES, like close-on-exec. */
  att.bInheritHandle = FALSE;
  att.lpSecurityDescriptor = NULL; /* NOLINT */

  HANDLE in = INVALID_HANDLE_VALUE;
  HANDLE out = INVALID_HANDLE_VALUE;

  if (CreatePipe(&in, &out, &att, 0) == 0) {
    if (in != INVALID_HANDLE_VALUE) close_fd(in);
    if (out != INVALID_HANDLE_VALUE) close_fd(out);

    return shit::None;
  }

  return Pipe{in, out};
}

Maybe<descriptor> open_file_descriptor(StringView path, FileOpenMode mode)
{
  DWORD access = (mode == FileOpenMode::Read) ? GENERIC_READ : GENERIC_WRITE;
  DWORD disposition = OPEN_EXISTING;
  switch (mode) {
  case FileOpenMode::Truncate: disposition = CREATE_ALWAYS; break;
  /* CREATE_NEW fails when the file already exists, the way noclobber wants. */
  case FileOpenMode::TruncateNoClobber: disposition = CREATE_NEW; break;
  case FileOpenMode::Append: disposition = OPEN_ALWAYS; break;
  case FileOpenMode::Read: disposition = OPEN_EXISTING; break;
  }

  /* The handle is created non-inheritable. execute_program marks it inheritable
     only while it spawns the child that the redirection feeds. */
  SECURITY_ATTRIBUTES att{};
  att.nLength = sizeof(SECURITY_ATTRIBUTES);
  att.bInheritHandle = FALSE;
  att.lpSecurityDescriptor = NULL; /* NOLINT */

  /* CreateFileA needs a null-terminated path, so the view is copied into a
     String that owns a trailing null. */
  String path_string{path};
  HANDLE handle = CreateFileA(path_string.c_str(), access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &att,
                              disposition, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) return shit::None;

  /* Append moves the write position to the end of the file. */
  if (mode == FileOpenMode::Append) SetFilePointer(handle, 0, NULL, FILE_END);

  return handle;
}

Maybe<descriptor> write_to_temp_file(StringView content)
{
  char temp_dir[MAX_PATH];
  if (GetTempPathA(MAX_PATH, temp_dir) == 0) return shit::None;

  char temp_path[MAX_PATH];
  if (GetTempFileNameA(temp_dir, "sht", 0, temp_path) == 0) return shit::None;

  HANDLE handle = CreateFileA(
      temp_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
  if (handle == INVALID_HANDLE_VALUE) return shit::None;

  DWORD written = 0;
  if (WriteFile(handle, content.data, static_cast<DWORD>(content.size()),
                &written, NULL) == 0)
  {
    close_fd(handle);
    return shit::None;
  }

  SetFilePointer(handle, 0, NULL, FILE_BEGIN);
  return handle;
}

u32 get_file_creation_mask()
{
  int old = _umask(0);
  _umask(old);
  return static_cast<u32>(old);
}

void set_file_creation_mask(u32 mask) { _umask(static_cast<int>(mask)); }

i32 wait_and_monitor_process(process p)
{
  if (WaitForSingleObject(p, INFINITE) != WAIT_OBJECT_0)
    throw Error{"WaitForSingleObject() failed: " + last_system_error_message()};

  DWORD code = -1;
  if (GetExitCodeProcess(p, &code) == 0)
    throw Error{"GetExitCodeProcess() failed: " + last_system_error_message()};

  return code;
}

ProcessState poll_process(process p, i32 &status_out)
{
  /* Windows has no stopped state, so a process is either alive or finished. */
  DWORD code = 0;
  if (GetExitCodeProcess(p, &code) == 0) {
    status_out = 0;
    return ProcessState::Exited;
  }
  if (code == STILL_ACTIVE) return ProcessState::Running;
  status_out = static_cast<i32>(code);
  return ProcessState::Exited;
}

bool signal_process(process p, i32 signal_number)
{
  /* Windows cannot deliver a POSIX signal, so only a terminate is honored and a
     resume or a stop is a no-op the caller treats as unsupported. */
  if (signal_number == 9 || signal_number == 15)
    return TerminateProcess(p, 1) != 0;
  return false;
}

process process_from_pid(i64 pid)
{
  return OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE |
                         PROCESS_QUERY_INFORMATION,
                     FALSE, static_cast<DWORD>(pid));
}

Maybe<i32> signal_number_from_name(StringView name)
{
  if (!name.empty() &&
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

os_args make_os_args(const ArrayList<String> &args)
{
  SHIT_ASSERT(args.size() > 0);

  std::string s{};

  s += '"';
  s.append(args[0].c_str(), args[0].size());
  s += '"';

  /* TODO: Remove CVE and escape quotes. */
  if (args.size() > 1) {
    for (usize i = 1; i < args.size(); i++) {
      s += ' ';
      s += '"';
      s.append(args[i].c_str(), args[i].size());
      s += '"';
    }
  }

  return s;
}

String last_system_error_message()
{
  LPSTR errno_str{};
  DWORD win_errno = GetLastError();

  DWORD ret = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, win_errno, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&errno_str), 0, NULL); /* NOLINT */

  if (ret == 0) {
    return utils::unsigned_integer_to_string(win_errno) +
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
    err = std::move(capitalized);
  }

  return err;
}

static void handle_interrupt(int s)
{
  SHIT_UNUSED(s);
  shit::print("\n");
  /* TODO: Ignore error? */
  signal(SIGINT, handle_interrupt);
}

/* TODO: Use Windows events. */
void set_default_signal_handlers()
{
  if (signal(SIGTERM, SIG_IGN) == SIG_ERR ||
      signal(SIGINT, handle_interrupt) == SIG_ERR)
  {
    throw Error{"signal() failed: " + last_system_error_message()};
  }
}

void reset_signal_handlers()
{
  if (signal(SIGTERM, SIG_DFL) == SIG_ERR || signal(SIGINT, SIG_DFL) == SIG_ERR)
  {
    throw Error{"signal() failed: " + last_system_error_message()};
  }
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

ExtIndex erase_extension_and_get_its_index(std::string &program_name)
{
#if SHIT_PLATFORM_IS COSMO
  if (IsWindows())
#endif
  {
    usize extension_pos = program_name.rfind('.');

    if (extension_pos != std::string::npos &&
        extension_pos + MIN_SUFFIX_LEN < program_name.length())
    {
      std::string extension = program_name.substr(extension_pos);

      if (usize i = utils::find_pos_in_vec(OMITTED_SUFFIXES, extension);
          i != std::string::npos)
      {
        program_name.erase(program_name.begin() + extension_pos,
                           program_name.end());
        return i;
      }
    }
  }

  return 0;
}

} /* namespace os */

} /* namespace shit */

#endif /* COSMO || WIN32 */
