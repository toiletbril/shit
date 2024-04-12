#include "Utils.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>

/* Thank got C++17 exists. */
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
shit_search_path_env(std::string_view program_name)
{
  std::optional<std::string> maybe_path = shit_get_env("PATH");
  INSIST(maybe_path, "PATH environment variable must exist");

  std::string path_var = maybe_path.value();

  std::string dir_path;
  for (const uchar ch : path_var) {
    if (ch != ':')
      dir_path += ch;
    else {
      std::filesystem::directory_iterator dir{dir_path};

      /* Search every file in the directory. */
      for (const std::filesystem::directory_entry &f : dir) {
        std::string path_program_name = f.path().filename();
        if (path_program_name == program_name) {
          return f.path();
        }
      }

      dir_path.clear();
    }
  }

  return std::nullopt;
}

#if defined __linux__ || defined BSD || defined __APPLE__
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
    if (execvp(path.c_str(), const_cast<char *const *>(real_args.data())) != 0)
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

#elif defined _WIN32 /* __linux__ || BSD || __APPLE__ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Only parent can execute some operations. */
static const DWORD PARENT_SHELL_PID = GetCurrentProcessId();

std::optional<std::string>
shit_get_env(std::string_view key)
{
  char  buffer[32767];
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
  std::vector<std::string> real_args;
  real_args.push_back(path.string());
  for (const std::string &arg : args) {
    real_args.push_back(arg);
  }

  std::string command_line;
  for (const std::string &arg : real_args) {
    command_line += "\"" + arg + "\" ";
  }

  STARTUPINFOA        si{};
  PROCESS_INFORMATION pi{};

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  if (CreateProcessA(path.string().c_str(), &command_line[0], NULL, NULL, FALSE,
                     0, NULL, NULL, &si, &pi) == 0)
  {
    throw ErrorWithLocation{location, "CreateProcessA failed"};
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return code;
}

bool
shit_process_is_child()
{
  return GetCurrentProcessId() != PARENT_SHELL_PID;
}

#endif /* _WIN32 */
