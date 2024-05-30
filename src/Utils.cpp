#include "Utils.hpp"

#include "Errors.hpp"
#include "Lexer.hpp"
#include "Toiletline.hpp"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>

namespace shit {

namespace utils {

#if defined __linux__ || defined BSD || defined __APPLE__ || __COSMOPOLITAN__

#if defined __COSMOPOLITAN__
#include <cosmo.h>
#endif

#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr const uchar PATH_DELIMITER = ':';

/* Only parent can execute some operations. */
static const pid_t PARENT_SHELL_PID = getpid();

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

static sigset_t
ignored_signals()
{
  return make_sigset(SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGSTOP, SIGTSTP);
}

static void
reset_signal_handlers()
{
  sigset_t sm;
  sigfillset(&sm);
  sigprocmask(SIG_UNBLOCK, &sm, NULL);
}

void
set_default_signal_handlers()
{
  sigset_t sm = ignored_signals();
  sigprocmask(SIG_BLOCK, &sm, NULL);
}

std::string
last_system_error_message()
{
  return std::string{strerror(errno)};
}

std::optional<std::string>
get_environment_variable(std::string_view key)
{
  const char *e = std::getenv(key.data());
  return (e != nullptr) ? std::optional(std::string{e}) : std::nullopt;
}

i32
execute_program_by_path(const std::filesystem::path    &path,
                        std::string_view                program,
                        const std::vector<std::string> &args)
{
  std::vector<const char *> real_args;

  /* argv[0] is the program itself. */
  real_args.push_back(program.data());

  /* Then actual arguments. */
  for (const std::string &arg : args) {
    real_args.push_back(arg.c_str());
  }

  /* And then NULL at the end. */
  real_args.push_back(nullptr);

  pid_t child_pid = fork();

  if (child_pid == 0) {
    reset_signal_handlers();
    if (execv(path.c_str(), const_cast<char *const *>(real_args.data())) == -1)
      throw shit::Error{last_system_error_message()};
  }

  if (child_pid == -1)
    throw Error("fork() failed: " + last_system_error_message());

  i32 status{};
  i32 retcode = 255;

  if (waitpid(child_pid, &status, WUNTRACED) == -1)
    throw shit::Error{"waitpid() failed: " + last_system_error_message()};

  /* Print appropriate message if the process was sent a signal. */
  if (WIFSIGNALED(status)) {
    i32         sig = WTERMSIG(status);
    const char *sig_str = strsignal(sig);
    std::string sig_desc =
        (sig_str != nullptr) ? std::string{sig_str} : "Unknown";

    /* Ignore Ctrl-C. */
    if (sig & ~(SIGINT)) {
      std::cout << "[Process " << child_pid << ": " << sig_desc << ", signal "
                << std::to_string(sig) << "]" << std::endl;
    }

    retcode = status;
  } else if (WIFSTOPPED(status)) {
    i32         sig = WSTOPSIG(status);
    const char *sig_str = strsignal(sig);
    std::string sig_desc =
        (sig_str != nullptr) ? std::string{sig_str} : "Unknown";

    std::cout << "[Process " << child_pid << ": " << sig_desc << ", signal "
              << std::to_string(sig) << " and killed]" << std::endl;

    /* TODO: support background processes. */
    if (kill(child_pid, SIGKILL) == -1)
      throw shit::Error{"kill() failed: " + last_system_error_message()};
  } else if (!WIFEXITED(status)) {
    throw shit::Error{"waitpid() failed: " + last_system_error_message()};
  } else {
    retcode = WEXITSTATUS(status);
  }

  return retcode;
}

bool
is_child_process()
{
  return getpid() != PARENT_SHELL_PID;
}

/* Cosmopolitan binaries can be run on both Linux and Windows. This will be
 * replaced by a runtime check. */
#if !defined __COSMOPOLITAN__
std::string_view
sanitize_program_name(std::string_view program_name)
{
  return program_name;
}
#endif /* !__COSMOPOLITAN__ */

std::optional<std::string>
get_current_user()
{
  struct passwd *pw = getpwuid(getuid());
  if (pw != nullptr)
    return std::string{pw->pw_name};
  else
    return std::nullopt;
}

std::optional<std::filesystem::path>
get_home_directory()
{
  return get_environment_variable("HOME");
}

#elif defined _WIN32 /* __linux__ || BSD || __APPLE__ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

constexpr static const uchar PATH_DELIMITER = ';';

/* Only parent can execute some operations. */
static const DWORD PARENT_SHELL_PID = GetCurrentProcessId();

void
set_default_signal_handlers()
{
  /* TODO */
}

std::string
last_system_error_message()
{
  LPSTR errno_str{};
  DWORD win_errno = GetLastError();

  DWORD ret = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, win_errno, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&errno_str), 0, NULL);

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

  return err;
}

constexpr static usize WIN32_MAX_ENV_SIZE = 32767;

std::optional<std::string>
get_environment_variable(std::string_view key)
{
  char buffer[WIN32_MAX_ENV_SIZE] = {0};
  if (GetEnvironmentVariableA(key.data(), buffer, sizeof(buffer)) == 0) {
    return std::nullopt;
  }
  return std::string(buffer);
}

i32
execute_program_by_path(const std::filesystem::path    &path,
                        std::string_view                program,
                        const std::vector<std::string> &args)
{
  std::string command_line;

  /* TODO: remove CVE and escape quotes */
  command_line += '"';
  command_line += program;
  command_line += '"';

  if (args.size() > 0) {
    for (usize i = 0; i < args.size(); i++) {
      command_line += ' ';
      command_line += '"' + args[i] + '"';
    }
  }

  PROCESS_INFORMATION pi{};
  STARTUPINFOA        si{.cb = sizeof(si)};

  if (CreateProcessA(path.string().c_str(), command_line.data(), NULL, NULL,
                     FALSE, 0, NULL, NULL, &si, &pi) == 0)
  {
    throw Error{last_system_error_message()};
  }

  if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) {
    throw Error{"WaitForSingleObject() failed: " + last_system_error_message()};
  }

  DWORD code = 1;
  if (GetExitCodeProcess(pi.hProcess, &code) == 0) {
    throw Error{"GetExitCodeProcess() failed: " + last_system_error_message()};
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return code;
}

bool
is_child_process()
{
  return GetCurrentProcessId() != PARENT_SHELL_PID;
}

std::optional<std::string>
get_current_user()
{
  DWORD size = 0;
  GetUserNameA(NULL, &size);
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

#endif /* _WIN32 */

#if defined _WIN32 || defined __COSMOPOLITAN__

const static std::set<std::string> OMITTED_EXTENSIONS = {
    "exe",
    "com",
    "scr",
    "bat",
};

std::string_view
sanitize_program_name(std::string_view program_name)
{
#if defined __COSMOPOLITAN__
  if (IsWindows())
#endif
  {
    usize extension_pos = program_name.rfind(".");

    if (extension_pos != std::string::npos &&
        extension_pos + 3 < program_name.length())
    {
      std::string_view extension = program_name.substr(extension_pos + 1);
      if (OMITTED_EXTENSIONS.find(extension.data()) != OMITTED_EXTENSIONS.end())
      {
        return program_name.substr(0, extension_pos);
      }
    }
  }

  return program_name;
}

#endif /* _WIN32 || __COSMOPOLITAN__ */

std::optional<std::string>
simple_shell_expand(std::string_view path)
{
  usize       pos = std::string::npos;
  std::string expanded_path{path};

  /* Expand tilde. */
  while ((pos = expanded_path.find('~')) != std::string::npos) {
    if (pos > 0 && !lexer::is_whitespace(expanded_path[pos - 1])) {
      break;
    }

    if (expanded_path.length() > pos + 1 &&
        expanded_path[pos + 1] != std::filesystem::path::preferred_separator)
    {
      /* TODO: Expand different users. */
      break;
    }

    /* Remove the tilde. */
    expanded_path.erase(pos, 1);

    std::optional<std::filesystem::path> u = get_home_directory();
    if (!u) {
      return std::nullopt;
    }

    expanded_path.insert(pos, u.value().string());
  }

  /* TODO: Expand asterisk, exclamation mark. */

  return expanded_path;
}

std::optional<std::filesystem::path>
canonicalize_path(std::string_view path)
{
  std::filesystem::path actual_path{path};

  if (actual_path.is_relative() &&
      actual_path.string().find('/') != std::string::npos)
  {
    actual_path = std::filesystem::absolute(actual_path);
  }

  return actual_path.lexically_normal().make_preferred();
}

void
set_current_directory(const std::filesystem::path &path)
{
  std::filesystem::current_path(path);
}

std::filesystem::path
get_current_directory()
{
  return std::filesystem::current_path();
}

[[noreturn]] void
quit(i32 code)
{
  /* Cleanup for main proccess. */
  if (!is_child_process()) {
    if (toiletline::is_active()) {
      toiletline::exit();
    }
  }

  std::exit(code);
}

#define SANITIZED_EQUAL(s1, s2)                                                \
  sanitize_program_name(s1) == sanitize_program_name(s2)

/* TODO: Cache this. */
std::optional<std::filesystem::path>
search_program_path(std::string_view program_name)
{
  std::optional<std::string> maybe_path = get_environment_variable("PATH");
  if (!maybe_path)
    return std::nullopt;

  std::string dir_path;
  std::string path_var = maybe_path.value();

  for (const char &ch : path_var) {
    if (ch != PATH_DELIMITER) {
      dir_path += ch;
    } else {
      /* What the heck? A path in PATH that does not exist? Are you a Windows
       * user? */
      if (std::filesystem::exists(dir_path)) {
        std::filesystem::directory_iterator dir{dir_path};

        /* Search every file in the directory. */
        for (const std::filesystem::directory_entry &f : dir) {
          if (SANITIZED_EQUAL(f.path().filename().string(), program_name)) {
            return f.path();
          }
        }
      }
      dir_path.clear();
    }
  }

  return std::nullopt;
}

} /* namespace utils */

} /* namespace shit */
