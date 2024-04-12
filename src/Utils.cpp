#include "Utils.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>

#if defined __linux__ || defined BSD || defined __APPLE__
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr const uchar SHIT_PATH_DELIMITER = ':';

/* Only parent can execute some operations. */
static const pid_t PARENT_SHELL_PID = getpid();

std::optional<std::string>
shit_get_env(std::string_view key)
{
  const char *e = std::getenv(key.data());
  return (e != nullptr) ? std::optional(std::string{e}) : std::nullopt;
}

i32
shit_exec(usize location, const std::filesystem::path &path,
          const std::vector<std::string> &args)
{
  std::vector<const char *> real_args;

  /* argv[0] is the program path itself. */
  real_args.push_back(path.c_str());
  /* Then actual arguments. */
  for (const std::string &arg : args) {
    real_args.push_back(arg.c_str());
  }
  /* And then NULL at the end. */
  real_args.push_back(nullptr);

  i32   status = 256;
  pid_t pid = fork();

  if (pid == -1) {
    throw ErrorWithLocation(location,
                            "fork() failed: " + std::string{strerror(errno)});
  } else if (pid == 0) {
    if (execv(path.c_str(), const_cast<char *const *>(real_args.data())) != 0)
      throw ErrorWithLocation(location, std::string{strerror(errno)});
  } else {
    if (waitpid(pid, &status, 0) == -1) {
      throw ErrorWithLocation(location, "waitpid() failed: " +
                                            std::string{strerror(errno)});
    }
    return status;
  }

  UNREACHABLE();
}

bool
shit_process_is_child()
{
  return getpid() != PARENT_SHELL_PID;
}

std::string_view
shit_sanitize_program_name(std::string_view program_name)
{
  return program_name;
}

std::optional<std::string>
shit_get_current_user()
{
  struct passwd *pw = getpwuid(getuid());
  if (pw != nullptr)
    return std::string(pw->pw_name);
  else
    return std::nullopt;
}

std::optional<std::filesystem::path>
shit_get_home_dir()
{
  return shit_get_env("HOME");
}

#elif defined _WIN32 /* __linux__ || BSD || __APPLE__ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

constexpr const uchar SHIT_PATH_DELIMITER = ';';

/* Only parent can execute some operations. */
static const DWORD PARENT_SHELL_PID = GetCurrentProcessId();

std::string
win32_get_last_error()
{
  DWORD  code = GetLastError();
  LPVOID lp_msg;
  DWORD  dw = FormatMessage(
       FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
           FORMAT_MESSAGE_IGNORE_INSERTS,
       NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lp_msg,
       0, NULL);

  if (dw == 0) {
    return "Error failed so hard that FormatMessage() failed too";
  }

  std::string m{static_cast<char *>(lp_msg)};
  LocalFree(lp_msg);

  return m;
}

constexpr usize SHIT_WIN32_MAX_ENV_SIZE = 32767;

std::optional<std::string>
shit_get_env(std::string_view key)
{
  char  buffer[SHIT_WIN32_MAX_ENV_SIZE];
  DWORD result = GetEnvironmentVariableA(key.data(), buffer, sizeof(buffer));
  if (result == 0) {
    return std::nullopt;
  }
  return std::string(buffer);
}

i32
shit_exec(usize location, const std::filesystem::path &path,
          const std::vector<std::string> &args)
{
  std::string command_line;

  command_line += '"';
  command_line += path.string();
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

  /* Here we throw the literal error, since the program may not exist. */
  if (CreateProcessA(path.string().c_str(), command_line.data(), NULL, NULL,
                     FALSE, 0, NULL, NULL, &si, &pi) == 0)
  {
    throw ErrorWithLocation{location, win32_get_last_error()};
  }

  /* These things should not fail at all, so we include the function name. */
  if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) {
    throw ErrorWithLocation{location, "WaitForSingleObject() failed: " +
                                          win32_get_last_error()};
  }

  DWORD code = 1;
  if (GetExitCodeProcess(pi.hProcess, &code) == 0) {
    throw ErrorWithLocation{location, "GetExitCodeProcess() failed: " +
                                          win32_get_last_error()};
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return code;
}

bool
shit_process_is_child()
{
  return GetCurrentProcessId() != PARENT_SHELL_PID;
}

std::string_view
shit_sanitize_program_name(std::string_view program_name)
{
  usize extension_pos = program_name.find_last_of('.');
  if (extension_pos == std::string::npos)
    return program_name;
  return program_name.substr(0, extension_pos);
}

std::optional<std::string>
shit_get_current_user()
{
  DWORD size = 0;
  GetUserName(NULL, &size);
  if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    std::vector<char> buffer{size};
    if (GetUserName(buffer.data(), &size)) {
      return std::string{buffer.data(), size - 1};
    }
  }
  return std::nullopt;
}

std::optional<std::filesystem::path>
shit_get_home_dir()
{
  return shit_get_env("USERPROFILE");
}

#endif /* _WIN32 */

/* Thank god C++17 exists. */
std::optional<std::filesystem::path>
shit_canonicalize_path(const std::string_view &path)
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
    std::optional<std::string> u = shit_get_home_dir();
    if (!u)
      return std::nullopt;
    expanded_path.insert(pos, u.value());
  }

  std::filesystem::path actual_path{expanded_path};
  if (actual_path.is_relative() && expanded_path.find('/') != std::string::npos)
    actual_path = std::filesystem::absolute(actual_path);

  return actual_path.lexically_normal().make_preferred();
}

void
shit_current_directory_set(const std::filesystem::path &path)
{
  std::filesystem::current_path(path);
}

std::filesystem::path
shit_current_directory()
{
  return std::filesystem::current_path();
}

[[noreturn]] void
shit_exit(i32 code)
{
  std::exit(code);
}

std::optional<std::filesystem::path>
shit_search_for_program(std::string_view program_name)
{
  std::optional<std::string> maybe_path = shit_get_env("PATH");
  INSIST(maybe_path, "PATH environment variable must exist");

  std::string path_var = maybe_path.value();

  std::string dir_path;
  for (const uchar ch : path_var) {
    if (ch != SHIT_PATH_DELIMITER)
      dir_path += ch;
    else {
      /* What the heck? A path in PATH that does not exist? Are you a Windows
       * user? */
      if (std::filesystem::exists(dir_path)) {
        std::filesystem::directory_iterator dir{dir_path};

        /* Search every file in the directory. */
        for (const std::filesystem::directory_entry &f : dir) {
          if (shit_sanitize_program_name(f.path().filename().string()) ==
              shit_sanitize_program_name(program_name))
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
