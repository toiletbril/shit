#include "Platform.hpp"

#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"

#include <csignal>
#include <cstdarg>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>

#if SHIT_PLATFORM_IS POSIX
#include <fcntl.h>
#endif

#if SHIT_PLATFORM_IS POSIX

namespace shit {

namespace os {

std::optional<usize>
write_fd(os::descriptor fd, const void *buf, usize size)
{
  ssize_t w = write(fd, buf, size);
  if (w == -1) return std::nullopt;
  return static_cast<usize>(w);
}

std::optional<usize>
read_fd(os::descriptor fd, void *buf, usize size)
{
  ssize_t r = read(fd, buf, size);
  if (r == -1) return std::nullopt;
  return static_cast<usize>(r);
}

bool
close_fd(os::descriptor fd)
{
  return close(fd) != -1;
}

os::descriptor
redirect_stdout(os::descriptor target)
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

void
restore_stdout(os::descriptor saved)
{
  dup2(saved, STDOUT_FILENO);
  close(saved);
}

std::optional<std::string>
get_current_user()
{
  struct passwd *pw = getpwuid(getuid());
  if (pw != nullptr) return pw->pw_name;
  return std::nullopt;
}

std::optional<std::filesystem::path>
get_home_directory()
{
  return get_environment_variable("HOME");
}

static const pid_t PARENT_SHELL_PID = getpid();

bool
is_child_process()
{
  return getpid() != PARENT_SHELL_PID;
}

i64
get_shell_process_id()
{
  return static_cast<i64>(PARENT_SHELL_PID);
}

i64
process_id_of(process p)
{
  return static_cast<i64>(p);
}

bool
is_stdin_a_tty()
{
  return isatty(SHIT_STDIN);
}

bool
is_stdout_a_tty()
{
  return isatty(SHIT_STDOUT);
}

/* Cosmopolitan binaries can be run on both Linux and Windows. This will be
 * replaced by a runtime check. */
#if SHIT_PLATFORM_ISNT COSMO
const std::vector<std::string> OMITTED_SUFFIXES = {""};

ExtIndex
erase_extension_and_get_its_index(std::string &program_name)
{
  /* POSIX does not really make use of extensions for executable files. */
  SHIT_UNUSED(program_name);
  return false;
}
#endif /* !COSMO */

std::optional<std::string>
get_environment_variable(const std::string &key)
{
  const char *e = std::getenv(key.c_str());
  return (e != nullptr) ? std::optional(e) : std::nullopt;
}

void
set_environment_variable(const std::string &key, const std::string &value)
{
  setenv(key.c_str(), value.c_str(), 1);
}

void
unset_environment_variable(const std::string &key)
{
  unsetenv(key.c_str());
}

i32
check_syscall_impl(
    i32 status, const std::string &invocation,
    const std::optional<std::function<void()>> &cleanup = std::nullopt)
{
  if (status == -1) {
    if (cleanup) (*cleanup)();

    throw shit::Error{"'" + invocation +
                      "' fail: " + last_system_error_message()};
  }

  return status;
}

#define check_syscall(fn)              check_syscall_impl(fn, #fn)
#define check_syscall2(fn, cleanup_fn) check_syscall_impl(fn, #fn, cleanup_fn)

process
execute_program(ExecContext &&ec)
{
  SHIT_DEFER { ec.close_fds(); };

  pid_t child_pid = check_syscall(fork());

  if (child_pid == 0) {
    std::vector<const char *> os_args = make_os_args(ec.args());

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
              const_cast<char *const *>(os_args.data())) == -1)
    {
      /* We are the forked child. Report the failure and terminate the child
       * directly. Throwing here would unwind back into the parent's evaluator
       * inside the duplicated process. */
      std::string msg = ec.program_path().string() + ": " +
                        last_system_error_message() + "\n";
      write_fd(STDERR_FILENO, msg.data(), msg.size());
      _exit(127);
    }
  }

  ec.close_fds();

  return child_pid;
}

std::optional<Pipe>
make_pipe()
{
  descriptor p[2] = {SHIT_INVALID_FD, SHIT_INVALID_FD};

  if (pipe(p) != 0) {
    return std::nullopt;
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

std::optional<descriptor>
open_file_descriptor(const std::string &path, FileOpenMode mode)
{
  int flags = 0;
  switch (mode) {
  case FileOpenMode::Truncate: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
  case FileOpenMode::Append: flags = O_WRONLY | O_CREAT | O_APPEND; break;
  case FileOpenMode::Read: flags = O_RDONLY; break;
  }

  /* 0666 lets the umask decide the final permissions, as a shell redirection
     does. */
  int fd = ::open(path.c_str(), flags, 0666);
  if (fd < 0) return std::nullopt;
  return fd;
}

std::optional<descriptor>
write_to_temp_file(const std::string &content)
{
  char path_template[] = "/tmp/shit_heredoc_XXXXXX";
  int fd = mkstemp(path_template);
  if (fd < 0) return std::nullopt;

  /* Unlink at once, so the file is anonymous and is freed when closed. */
  unlink(path_template);

  usize offset = 0;
  while (offset < content.size()) {
    ssize_t written =
        ::write(fd, content.data() + offset, content.size() - offset);
    if (written <= 0) {
      close(fd);
      return std::nullopt;
    }
    offset += static_cast<usize>(written);
  }

  lseek(fd, 0, SEEK_SET);
  return fd;
}

i32
wait_and_monitor_process(process pid)
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
    std::string sig_desc =
        (sig_str != nullptr) ? std::string{sig_str} : "Unknown";

    /* Ignore Ctrl-C. */
    if (sig & ~(SIGINT)) {
      std::cout << "[Process " << pid << ": " << sig_desc << ", signal "
                << std::to_string(sig) << "]" << std::endl;
    } else {
      std::cout << std::endl;
    }

    return 128 + sig;
  } else if (WIFSTOPPED(status)) {
    i32 sig = WSTOPSIG(status);
    const char *sig_str = strsignal(sig);
    std::string sig_desc =
        (sig_str != nullptr) ? std::string{sig_str} : "Unknown";

    std::cout << "[Process " << pid << ": " << sig_desc << ", signal "
              << std::to_string(sig) << " and killed]" << std::endl;

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

os_args
make_os_args(const std::vector<std::string> &args)
{
  std::vector<const char *> os_args;
  os_args.reserve(args.size() + 1);

  for (const std::string &arg : args)
    os_args.push_back(arg.c_str());

  os_args.push_back(nullptr);

  return os_args;
}

std::string
last_system_error_message()
{
  return std::string{strerror(errno)};
}

static sigset_t
make_sigset_impl(int first, ...)
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

static void
sigchild_handler(int n, siginfo_t *siginfo, void *ctx)
{
  SHIT_UNUSED(n);
  SHIT_UNUSED(ctx);
  SHIT_UNUSED(siginfo);
}

void
reset_signal_handlers()
{
  sigset_t sm;
  sigfillset(&sm);
  check_syscall(sigprocmask(SIG_UNBLOCK, &sm, nullptr));

  struct sigaction sa = {};
  sa.sa_handler = SIG_DFL;
  check_syscall(sigaction(SIGCHLD, &sa, nullptr));
}

void
set_default_signal_handlers()
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

std::optional<usize>
write_fd(os::descriptor fd, const void *buf, usize size)
{
  DWORD w = -1;
  if (WriteFile(fd, buf, size, &w, 0) == FALSE) /* NOLINT */
    return std::nullopt;
  return static_cast<usize>(w);
}

std::optional<usize>
read_fd(os::descriptor fd, void *buf, usize size)
{
  DWORD r = -1;
  if (ReadFile(fd, buf, size, &r, 0) == FALSE) /* NOLINT */
    return std::nullopt;
  return static_cast<usize>(r);
}

bool
close_fd(os::descriptor fd)
{
  return CloseHandle(fd);
}

os::descriptor
redirect_stdout(os::descriptor target)
{
  os::descriptor saved = GetStdHandle(STD_OUTPUT_HANDLE);
  SetStdHandle(STD_OUTPUT_HANDLE, target);
  return saved;
}

void
restore_stdout(os::descriptor saved)
{
  SetStdHandle(STD_OUTPUT_HANDLE, saved);
}

std::optional<std::string>
get_current_user()
{
  DWORD size = 0;
  GetUserNameA(nullptr, &size);
  if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    std::vector<char> buffer;
    buffer.resize(size);
    if (GetUserNameA(buffer.data(), &size))
      return std::string{buffer.data(), size - 1};
  }
  return std::nullopt;
}

std::optional<std::filesystem::path>
get_home_directory()
{
  return get_environment_variable("USERPROFILE");
}

static const DWORD PARENT_SHELL_PID = GetCurrentProcessId();

bool
is_child_process()
{
  return GetCurrentProcessId() != PARENT_SHELL_PID;
}

i64
get_shell_process_id()
{
  return static_cast<i64>(PARENT_SHELL_PID);
}

i64
process_id_of(process p)
{
  return static_cast<i64>(GetProcessId(p));
}

bool
is_stdin_a_tty()
{
  return _isatty(_fileno(stdin));
}

bool
is_stdout_a_tty()
{
  return _isatty(_fileno(stdout));
}

constexpr static usize WIN32_MAX_ENV_SIZE = 32767;

std::optional<std::string>
get_environment_variable(const std::string &key)
{
  char buffer[WIN32_MAX_ENV_SIZE] = {0};
  if (GetEnvironmentVariableA(key.c_str(), buffer, sizeof(buffer)) == 0)
    return std::nullopt;
  return std::string{buffer};
}

void
set_environment_variable(const std::string &key, const std::string &value)
{
  SetEnvironmentVariableA(key.c_str(), value.c_str());
}

void
unset_environment_variable(const std::string &key)
{
  SetEnvironmentVariableA(key.c_str(), nullptr);
}

process
execute_program(ExecContext &&ec)
{
  std::string program_path = ec.program_path().string();
  std::string command_line = make_os_args(ec.args());

  PROCESS_INFORMATION process_info{};
  STARTUPINFOA startup_info{};

  startup_info.cb = sizeof(startup_info);

  BOOL should_use_pipe = ec.in_fd || ec.out_fd;

  if (should_use_pipe) startup_info.dwFlags |= STARTF_USESTDHANDLES;

  startup_info.hStdInput = ec.in_fd.value_or(SHIT_STDIN);
  startup_info.hStdOutput = ec.out_fd.value_or(SHIT_STDOUT);
  startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  SHIT_DEFER
  {
    if (ec.in_fd) CloseHandle(*ec.in_fd);
    if (ec.out_fd) CloseHandle(*ec.out_fd);
  };

  if (CreateProcessA(program_path.c_str(), command_line.data(), nullptr,
                     nullptr, should_use_pipe, 0, nullptr, nullptr,
                     &startup_info, &process_info) == 0)
  {
    throw ErrorWithLocation{ec.source_location(), last_system_error_message()};
  }

  return process_info.hProcess;
}

std::optional<Pipe>
make_pipe()
{
  SECURITY_ATTRIBUTES att{};

  att.nLength = sizeof(SECURITY_ATTRIBUTES);
  att.bInheritHandle = TRUE;
  att.lpSecurityDescriptor = NULL; /* NOLINT */

  HANDLE in = INVALID_HANDLE_VALUE;
  HANDLE out = INVALID_HANDLE_VALUE;

  if (CreatePipe(&in, &out, &att, 0) == 0) {
    if (in != INVALID_HANDLE_VALUE) close_fd(in);
    if (out != INVALID_HANDLE_VALUE) close_fd(out);

    return std::nullopt;
  }

  return Pipe{in, out};
}

std::optional<descriptor>
open_file_descriptor(const std::string &path, FileOpenMode mode)
{
  DWORD access = (mode == FileOpenMode::Read) ? GENERIC_READ : GENERIC_WRITE;
  DWORD disposition = OPEN_EXISTING;
  switch (mode) {
  case FileOpenMode::Truncate: disposition = CREATE_ALWAYS; break;
  case FileOpenMode::Append: disposition = OPEN_ALWAYS; break;
  case FileOpenMode::Read: disposition = OPEN_EXISTING; break;
  }

  /* The handle is inheritable so a spawned child receives the redirection. */
  SECURITY_ATTRIBUTES att{};
  att.nLength = sizeof(SECURITY_ATTRIBUTES);
  att.bInheritHandle = TRUE;
  att.lpSecurityDescriptor = NULL; /* NOLINT */

  HANDLE handle = CreateFileA(path.c_str(), access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &att,
                              disposition, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

  /* Append moves the write position to the end of the file. */
  if (mode == FileOpenMode::Append)
    SetFilePointer(handle, 0, NULL, FILE_END);

  return handle;
}

std::optional<descriptor>
write_to_temp_file(const std::string &content)
{
  char temp_dir[MAX_PATH];
  if (GetTempPathA(MAX_PATH, temp_dir) == 0) return std::nullopt;

  char temp_path[MAX_PATH];
  if (GetTempFileNameA(temp_dir, "sht", 0, temp_path) == 0)
    return std::nullopt;

  HANDLE handle = CreateFileA(temp_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                              NULL);
  if (handle == INVALID_HANDLE_VALUE) return std::nullopt;

  DWORD written = 0;
  if (WriteFile(handle, content.data(), static_cast<DWORD>(content.size()),
                &written, NULL) == 0)
  {
    close_fd(handle);
    return std::nullopt;
  }

  SetFilePointer(handle, 0, NULL, FILE_BEGIN);
  return handle;
}

i32
wait_and_monitor_process(process p)
{
  if (WaitForSingleObject(p, INFINITE) != WAIT_OBJECT_0)
    throw Error{"WaitForSingleObject() failed: " + last_system_error_message()};

  DWORD code = -1;
  if (GetExitCodeProcess(p, &code) == 0)
    throw Error{"GetExitCodeProcess() failed: " + last_system_error_message()};

  return code;
}

os_args
make_os_args(const std::vector<std::string> &args)
{
  SHIT_ASSERT(args.size() > 0);

  std::string s{};

  s += '"' + args[0] + '"';

  /* TODO: Remove CVE and escape quotes. */
  if (args.size() > 1) {
    for (usize i = 1; i < args.size(); i++) {
      s += ' ';
      s += '"' + args[i] + '"';
    }
  }

  return s;
}

std::string
last_system_error_message()
{
  LPSTR errno_str{};
  DWORD win_errno = GetLastError();

  DWORD ret = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, win_errno, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&errno_str), 0, NULL); /* NOLINT */

  if (ret == 0) {
    return std::to_string(win_errno) + " (Error message couldn't be proccessed "
                                       "due to FormatMessage() fail)";
  }

  std::string_view view{static_cast<char *>(errno_str)};
  /* I do not want the PERIOD. */
  if (view.find_last_of(". \n") != std::string::npos) {
    view.remove_suffix(3);
  }
  std::string err{view};
  LocalFree(errno_str);

  /* Remove stupid inserts. I can't stand Windows */
  for (usize i = 0; i + 1 < err.length(); i++) {
    if (err[i] == '%' && isdigit(err[i + 1])) {
      err.erase(i, 2);
      /* Replace %N bullshit with just "Input". */
      err.insert(i, "input");
    }
  }

  if (err.length() > 0) {
    /* Capitalize first letter to sound formal. */
    err[0] = toupper(err[0]);
  }

  return err;
}

static void
handle_interrupt(int s)
{
  SHIT_UNUSED(s);
  std::cout << std::endl;
  /* TODO: Ignore error? */
  signal(SIGINT, handle_interrupt);
}

/* TODO: Use Windows events. */
void
set_default_signal_handlers()
{
  if (signal(SIGTERM, SIG_IGN) == SIG_ERR ||
      signal(SIGINT, handle_interrupt) == SIG_ERR)
  {
    throw Error{"signal() failed: " + last_system_error_message()};
  }
}

void
reset_signal_handlers()
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

const std::vector<std::string> OMITTED_SUFFIXES = {
    /* First extension entry should be empty. */
    "", ".exe", ".com", ".scr", ".bat",
};

constexpr static usize MIN_SUFFIX_LEN = 3;

ExtIndex
erase_extension_and_get_its_index(std::string &program_name)
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
