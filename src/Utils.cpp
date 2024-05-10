#include "Utils.hpp"

#include "Toiletline.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>

namespace shit {

namespace utils {

#if defined __linux__ || defined BSD || defined __APPLE__
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr const uchar PATH_DELIMITER = ':';

/* Only parent can execute some operations. */
static const pid_t PARENT_SHELL_PID = getpid();

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

  i32   status = 256;
  pid_t pid = fork();

  if (pid == -1) {
    throw Error("fork() failed: " + last_system_error_message());
  } else if (pid == 0) {
    if (execv(path.c_str(), const_cast<char *const *>(real_args.data())) == -1)
      throw shit::Error{last_system_error_message()};
  } else {
    if (waitpid(pid, &status, 0) == -1 || !WIFEXITED(status))
      throw shit::Error{"waitpid() failed: " + last_system_error_message()};

    return WEXITSTATUS(status);
  }

  SHIT_UNREACHABLE();
}

bool
is_child_process()
{
  return getpid() != PARENT_SHELL_PID;
}

std::string_view
sanitize_program_name(std::string_view program_name)
{
  return program_name;
}

std::optional<std::string>
get_current_user()
{
  struct passwd *pw = getpwuid(getuid());
  if (pw != nullptr)
    return std::string(pw->pw_name);
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

constexpr const uchar PATH_DELIMITER = ';';

/* Only parent can execute some operations. */
static const DWORD PARENT_SHELL_PID = GetCurrentProcessId();

std::string
last_system_error_message()
{
  DWORD  code = GetLastError();
  LPVOID lp_msg;
  DWORD  dw = FormatMessage(
       FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
           FORMAT_MESSAGE_IGNORE_INSERTS,
       NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lp_msg,
       0, NULL);

  if (dw == 0) {
    return "(Error message couldn't be proccessed due to FormatMessage() fail)";
  }

  std::string m{static_cast<char *>(lp_msg)};
  LocalFree(lp_msg);

  return m;
}

constexpr usize WIN32_MAX_ENV_SIZE = 32767;

std::optional<std::string>
get_environment_variable(std::string_view key)
{
  char  buffer[WIN32_MAX_ENV_SIZE];
  DWORD result = GetEnvironmentVariableA(key.data(), buffer, sizeof(buffer));
  if (result == 0) {
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

  /* TODO: remove CVE */
  command_line += '"';
  command_line += program;
  command_line += '"';

  if (args.size() > 0) {
    for (usize i = 0; i < args.size(); i++) {
      command_line += ' ';
      command_line += '"' + args[i] + '"';
    }
  }

  STARTUPINFOA        si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

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

std::string_view
sanitize_program_name(std::string_view program_name)
{
  usize extension_pos = program_name.rfind(".exe");
  if (extension_pos == std::string::npos)
    return program_name;
  return program_name.substr(0, extension_pos);
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

std::optional<std::string>
expand_path(const std::string_view &path)
{
  std::string expanded_path{path};

  /* Expand tilde. */
  usize pos{std::string::npos};
  while ((pos = expanded_path.find('~')) != std::string::npos) {
    if (expanded_path.length() < pos + 1 ||
        expanded_path[pos + 1] != std::filesystem::path::preferred_separator)
    {
      break;
    }
    expanded_path.erase(pos, 1);
    /* TODO: expand different users */
    std::optional<std::filesystem::path> u = get_home_directory();
    if (!u)
      return std::nullopt;
    expanded_path.insert(pos, u.value().string());
  }

  /* TODO: expand asterisk and etc */

  return expanded_path;
}

std::optional<std::filesystem::path>
canonicalize_path(const std::string_view &path)
{
  std::filesystem::path actual_path{path};
  if (actual_path.is_relative() &&
      actual_path.string().find('/') != std::string::npos)
    actual_path = std::filesystem::absolute(actual_path);
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
    if (toiletline::is_active())
      toiletline::exit();
  }

  std::exit(code);
}

/* TODO: cache this. */
std::optional<std::filesystem::path>
search_program_path(std::string_view program_name)
{
  std::optional<std::string> maybe_path = get_environment_variable("PATH");
  SHIT_ASSERT(maybe_path, "PATH environment variable must exist");

  std::string path_var = maybe_path.value();

  std::string dir_path;
  for (const char &ch : path_var) {
    if (ch != PATH_DELIMITER)
      dir_path += ch;
    else {
      /* What the heck? A path in PATH that does not exist? Are you a Windows
       * user? */
      if (std::filesystem::exists(dir_path)) {
        std::filesystem::directory_iterator dir{dir_path};

        /* Search every file in the directory. */
        for (const std::filesystem::directory_entry &f : dir) {
          if (sanitize_program_name(f.path().filename().string()) ==
              sanitize_program_name(program_name))
          {
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
