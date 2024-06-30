#include "Platform.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"

#include <csignal>
#include <cstdarg>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>

#if PLATFORM_IS(POSIX)

namespace shit {

namespace os {

std::optional<usize>
write_fd(os::descriptor fd, const void *buf, usize size)
{
  ssize_t w = write(fd, buf, size);
  if (w == -1) {
    return std::nullopt;
  }
  return static_cast<usize>(w);
}

std::optional<usize>
read_fd(os::descriptor fd, void *buf, usize size)
{
  ssize_t r = read(fd, buf, size);
  if (r == -1) {
    return std::nullopt;
  }
  return static_cast<usize>(r);
}

bool
close_fd(os::descriptor fd)
{
  return close(fd) != -1;
}

std::optional<std::string>
get_current_user()
{
  struct passwd *pw = getpwuid(getuid());
  if (pw != nullptr) {
    return std::string{pw->pw_name};
  } else {
    return std::nullopt;
  }
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

/* Cosmopolitan binaries can be run on both Linux and Windows. This will be
 * replaced by a runtime check. */
#if !PLATFORM_IS(COSMO)
const std::vector<std::string> OMITTED_SUFFIXES = {""};

usize
sanitize_program_name(std::string &program_name)
{
  /* POSIX does not really make use of extensions for executable files. */
  SHIT_UNUSED(program_name);
  return false;
}
#endif

std::optional<std::string>
get_environment_variable(const std::string &key)
{
  const char *e = std::getenv(key.c_str());
  return (e != nullptr) ? std::optional(std::string{e}) : std::nullopt;
}

i32
call_checked_impl(
    i32 status, const std::string &invocation,
    const std::optional<std::function<void()>> &cleanup = std::nullopt)
{
  if (status == -1) {
    if (cleanup) {
      (*cleanup)();
    }

    throw shit::Error{"'" + invocation +
                      "' fail: " + last_system_error_message()};
  }

  return status;
}

#define call_checked(f, ...)   call_checked_impl(f, #f, __VA_ARGS__)
#define call_checked_simple(f) call_checked_impl(f, #f)

process
execute_program(utils::ExecContext &&ec)
{
  auto c = [&]() { ec.close_fds(); };

  pid_t child_pid = call_checked(fork(), c);

  if (child_pid == 0) {
    std::vector<const char *> os_args = make_os_args(ec.args());

    if (ec.in_fd) {
      call_checked(dup2(*ec.in_fd, STDIN_FILENO), c);
      call_checked(close(*ec.in_fd), c);
    }
    if (ec.out_fd) {
      call_checked(dup2(*ec.out_fd, STDOUT_FILENO), c);
      call_checked(close(*ec.out_fd), c);
    }

    reset_signal_handlers();

    /* TODO: If execv() failed, try to execute the path as a shell script. */
    if (execv(ec.program_path().c_str(),
              const_cast<char *const *>(os_args.data())) == -1)
    {
      /* Throw a readable error message. */
      throw ErrorWithLocation{ec.source_location(),
                              last_system_error_message()};
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

  return Pipe{p[0], p[1]};
}

i32
wait_and_monitor_process(process pid)
{
  SHIT_ASSERT(pid >= 0);

  i32 status{};

  while (call_checked_simple(waitpid(pid, &status, 0) != pid)) {
    /* Waiting... */
  }

  /* Print appropriate message if the process was sent a signal. */
  if (WIFSIGNALED(status)) {
    i32         sig = WTERMSIG(status);
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

    return status;
  } else if (WIFSTOPPED(status)) {
    i32         sig = WSTOPSIG(status);
    const char *sig_str = strsignal(sig);
    std::string sig_desc =
        (sig_str != nullptr) ? std::string{sig_str} : "Unknown";

    std::cout << "[Process " << pid << ": " << sig_desc << ", signal "
              << std::to_string(sig) << " and killed]" << std::endl;

    /* We can't handle suspended processes yet, so goodbye. */
    call_checked_simple(kill(pid, SIGKILL));
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

  for (const std::string &arg : args) {
    os_args.push_back(arg.c_str());
  }

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
  for (int sig = first; sig != -1; sig = va_arg(va, int)) {
    sigaddset(&sm, sig);
  }
  va_end(va);

  return sm;
}

#define make_sigset(...) make_sigset_impl(__VA_ARGS__, -1)

void
reset_signal_handlers()
{
  sigset_t sm;
  sigfillset(&sm);
  call_checked_simple(sigprocmask(SIG_UNBLOCK, &sm, nullptr));
}

void
set_default_signal_handlers()
{
  /* Ignore bullshit. */
  sigset_t sm = make_sigset(SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGSTOP, SIGTSTP);
  call_checked_simple(sigprocmask(SIG_BLOCK, &sm, nullptr));
}

} /* namespace os */

} /* namespace shit */

#elif PLATFORM_IS(WIN32)

namespace shit {

namespace os {

std::optional<usize>
write_fd(os::descriptor fd, const void *buf, usize size)
{
  DWORD w = -1;
  if (WriteFile(fd, buf, size, &w, 0) == FALSE) { /* NOLINT */
    return std::nullopt;
  }
  return static_cast<usize>(w);
}

std::optional<usize>
read_fd(os::descriptor fd, void *buf, usize size)
{
  DWORD r = -1;
  if (ReadFile(fd, buf, size, &r, 0) == FALSE) { /* NOLINT */
    return std::nullopt;
  }
  return static_cast<usize>(r);
}

bool
close_fd(os::descriptor fd)
{
  return CloseHandle(fd);
}

std::optional<std::string>
get_current_user()
{
  DWORD size = 0;
  GetUserNameA(nullptr, &size);
  if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    std::vector<char> buffer;
    buffer.reserve(size);
    if (GetUserNameA(buffer.data(), &size)) {
      return std::string{buffer.data(), size - 1};
    }
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

constexpr static usize WIN32_MAX_ENV_SIZE = 32767;

std::optional<std::string>
get_environment_variable(const std::string &key)
{
  char buffer[WIN32_MAX_ENV_SIZE] = {0};
  if (GetEnvironmentVariableA(key.c_str(), buffer, sizeof(buffer)) == 0) {
    return std::nullopt;
  }
  return std::string{buffer};
}

process
execute_program(utils::ExecContext &&ec)
{
  std::string program_path = ec.program_path().string();
  std::string command_line = make_os_args(ec.args());

  PROCESS_INFORMATION process_info{};
  STARTUPINFOA        startup_info{};

  startup_info.cb = sizeof(startup_info);

  BOOL should_use_pipe = ec.in_fd || ec.out_fd;

  if (should_use_pipe) {
    startup_info.dwFlags |= STARTF_USESTDHANDLES;
  }

  startup_info.hStdInput = ec.in_fd.value_or(SHIT_STDIN);
  startup_info.hStdOutput = ec.out_fd.value_or(SHIT_STDOUT);

  SHIT_DEFER
  {
    if (ec.in_fd)
      CloseHandle(*ec.in_fd);
    if (ec.out_fd)
      CloseHandle(*ec.out_fd);
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
    if (in != INVALID_HANDLE_VALUE)
      close_fd(in);
    if (out != INVALID_HANDLE_VALUE)
      close_fd(out);

    return std::nullopt;
  }

  return Pipe{in, out};
}

i32
wait_and_monitor_process(process p)
{
  if (WaitForSingleObject(p, INFINITE) != WAIT_OBJECT_0) {
    throw Error{"WaitForSingleObject() failed: " + last_system_error_message()};
  }

  DWORD code = -1;
  if (GetExitCodeProcess(p, &code) == 0) {
    throw Error{"GetExitCodeProcess() failed: " + last_system_error_message()};
  }

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
  for (usize i = 0; i < err.length() - 1; i++) {
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
print_lf(int s)
{
  SHIT_UNUSED(s);
  std::cout << std::endl;
  /* TODO: Ignore error? */
  signal(SIGINT, print_lf);
}

/* TODO: Use Windows events. */
void
set_default_signal_handlers()
{
  if (signal(SIGTERM, SIG_IGN) == SIG_ERR ||
      signal(SIGINT, print_lf) == SIG_ERR)
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

#if PLATFORM_IS(COSMO) || PLATFORM_IS(WIN32)

namespace shit {

namespace os {

const std::vector<std::string> OMITTED_SUFFIXES = {
    /* First extension entry should be empty. */
    "", ".exe", ".com", ".scr", ".bat",
};

constexpr static usize MIN_SUFFIX_LEN = 3;

usize
sanitize_program_name(std::string &program_name)
{
#if PLATFORM_IS(COSMO)
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

#endif /* PLATFORM_IS(COSMO) || PLATFORM_IS(WIN32) */
