#include "Utils.hpp"

#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Lexer.hpp"
#include "Toiletline.hpp"

#include <array>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>

/* TODO: Support background processes. */

namespace shit {

namespace utils {

#if defined __linux__ || defined BSD || defined __APPLE__ || __COSMOPOLITAN__

#if defined __COSMOPOLITAN__
#include <cosmo.h>
#endif

#include <cerrno>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr const uchar PATH_DELIMITER = ':';

/* Only parent can execute some operations. */
static const pid_t PARENT_SHELL_PID = getpid();

static i32
call_checked_impl(i32 ret, const std::string &&func)
{
  if (ret == -1) {
    throw shit::Error{func + " failed: " + last_system_error_message()};
  }

  return ret;
}

#define call_checked(f) call_checked_impl(f, std::string{#f})

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

static void
reset_signal_handlers()
{
  sigset_t sm;
  sigfillset(&sm);
  sigprocmask(SIG_UNBLOCK, &sm, nullptr);
}

static std::unordered_map<pid_t, std::tuple<int, int>> PIPE_CONSUMER_FDS{};

static void
close_consumer_stdin(int s, siginfo_t *info, void *context)
{
  SHIT_UNUSED(context);
  SHIT_ASSERT(s == SIGCHLD);

  auto [si, so] = PIPE_CONSUMER_FDS[info->si_pid];

  if (si != -1) {
    call_checked(close(si));
  }

  if (so != -1) {
    call_checked(close(so));
  }
}

void
set_default_signal_handlers()
{
  /* Ignore bullshit. */
  sigset_t sm = make_sigset(SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGSTOP, SIGTSTP);
  sigprocmask(SIG_BLOCK, &sm, nullptr);
}

static void
set_pipe_handlers()
{
  /* Close pipe for the consumers when their producer exits. */
  struct sigaction sa = {};
  sa.sa_flags = SA_SIGINFO;
  sa.sa_mask = make_sigset(SIGCHLD);
  sa.sa_sigaction = close_consumer_stdin;

  sigaction(SIGCHLD, &sa, NULL);
}

static void
reset_pipe_handlers()
{
  struct sigaction sa = {};
  sa.sa_mask = make_sigset(SIGCHLD);
  sa.sa_handler = SIG_DFL;
  sigaction(SIGCHLD, &sa, NULL);
}

std::string
last_system_error_message()
{
  return std::string{strerror(errno)};
}

std::optional<std::string>
get_environment_variable(const std::string &key)
{
  const char *e = std::getenv(key.c_str());
  return (e != nullptr) ? std::optional(std::string{e}) : std::nullopt;
}

static std::vector<const char *>
create_os_args(const std::string &program, const std::vector<std::string> &args)
{
  std::vector<const char *> os_args;

  /* argv[0] is the program itself. */
  os_args.push_back(program.c_str());

  /* Then actual arguments. */
  for (const std::string &arg : args) {
    os_args.push_back(arg.c_str());
  }

  /* And then nullptr at the end. */
  os_args.push_back(nullptr);

  return os_args;
}

static i32
wait_for_process(pid_t pid)
{
  SHIT_ASSERT(pid >= 0);

  i32 status{};

  while (call_checked(waitpid(pid, &status, WNOHANG) != pid)) {
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
    call_checked(kill(pid, SIGKILL) == -1);
  } else if (!WIFEXITED(status)) {
    /* Process was destroyed by otherworldly forces. */
    throw shit::Error{"???: " + last_system_error_message()};
  } else {
    /* We exited normally. */
    return WEXITSTATUS(status);
  }

  SHIT_UNREACHABLE();
}

[[noreturn]] static void
do_exec(const ExecContext &ec)
{
  std::vector<const char *> os_args = create_os_args(ec.m_program, ec.m_args);

  /* Cleanse the corruption of the holy child from evil signal spirits. */
  reset_signal_handlers();

  if (execv(ec.m_path.c_str(), const_cast<char *const *>(os_args.data())) == -1)
  {
    throw shit::Error{last_system_error_message()};
  }

  SHIT_UNREACHABLE();
}

i32
execute_program(const ExecContext &&ec)
{
  pid_t child_pid = call_checked(fork());

  if (child_pid == 0) {
    do_exec(ec);
  }

  return wait_for_process(child_pid);
}

i32
execute_program_sequence_with_pipes(const std::vector<ExecContext> &ecs)
{
  SHIT_ASSERT(ecs.size() > 1);

  i32   last_stdin = -1;
  pid_t last_pid = -1;

  bool is_first_producer = true;

  set_pipe_handlers();

  for (const ExecContext &ec : ecs) {
    i32 pipefds[2] = {-1, -1};

    bool is_last_consumer = &ec == &ecs.back();

    if (!is_last_consumer) {
      call_checked(pipe2(pipefds, O_CLOEXEC));
    }

    pid_t child_pid = call_checked(fork());

    if (child_pid == 0) {
      if (!is_first_producer) {
        call_checked(dup2(last_stdin, STDIN_FILENO));
      }

      if (!is_last_consumer) {
        call_checked(dup2(pipefds[1], STDOUT_FILENO));
      }

      do_exec(ec);
    }

    is_first_producer = false;

    PIPE_CONSUMER_FDS[child_pid] = {pipefds[0], pipefds[1]};

    last_stdin = pipefds[0];
    last_pid = child_pid;
  }

  i32 ret = wait_for_process(last_pid);

  reset_pipe_handlers();

  return ret;
}

bool
is_child_process()
{
  return getpid() != PARENT_SHELL_PID;
}

/* Cosmopolitan binaries can be run on both Linux and Windows. This will be
 * replaced by a runtime check. */
#if !defined __COSMOPOLITAN__
bool
sanitize_program_name(std::string &program_name)
{
  /* POSIX does not really make use of extensions for executable files. */
  SHIT_UNUSED(program_name);
  return false;
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

static void
print_lf(int s)
{
  SHIT_UNUSED(s);
  std::cout << std::endl;
  signal(SIGINT, print_lf);
}

void
set_default_signal_handlers()
{
  signal(SIGTERM, SIG_IGN);
  signal(SIGINT, print_lf);
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
      reinterpret_cast<LPSTR>(&errno_str), 0, );

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
get_environment_variable(const std::string &key)
{
  char buffer[WIN32_MAX_ENV_SIZE] = {0};
  if (GetEnvironmentVariableA(key.c_str(), buffer, sizeof(buffer)) == 0) {
    return std::nullopt;
  }
  return std::string{buffer};
}

i32
execute_program(const ExecContext &&ec)
{
  std::string command_line;

  /* TODO: Remove CVE and escape quotes. */
  command_line += '"';
  command_line += ec.m_program;
  command_line += '"';

  if (args.size() > 0) {
    for (usize i = 0; i < ec.m_args.size(); i++) {
      command_line += ' ';
      command_line += '"' + ec.m_args[i] + '"';
    }
  }

  PROCESS_INFORMATION pi{};
  STARTUPINFOA        si{.cb = sizeof(si)};

  if (CreateProcessA(path.string().c_str(), command_line.data(), nullptr,
                     nullptr, FALSE, 0, nullptr, nullptr, &si, &pi) == 0)
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

i32
execute_program_sequence_with_pipes(std::vector<ExecContext> &&ecs)
{
  throw shit::Error{"Not implemented (Utils)"};
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

#endif /* _WIN32 */

#if defined _WIN32 || defined __COSMOPOLITAN__

const static std::set<std::string> OMITTED_EXTENSIONS = {
    "exe",
    "com",
    "scr",
    "bat",
};

bool
sanitize_program_name(std::string &program_name)
{
#if defined __COSMOPOLITAN__
  if (IsWindows())
#endif
  {
    usize extension_pos = program_name.rfind(".");

    if (extension_pos != std::string::npos &&
        extension_pos + 3 < program_name.length())
    {
      std::string extension = program_name.substr(extension_pos + 1);

      if (auto e = OMITTED_EXTENSIONS.find(extension);
          e != OMITTED_EXTENSIONS.end()) {
        program_name.erase(program_name.begin() + extension_pos,
                           program_name.end());
        return true;
      }
    }
  }

  return false;
}

#endif /* _WIN32 || __COSMOPOLITAN__ */

std::optional<std::string>
simple_shell_expand(const std::string &path)
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
canonicalize_path(const std::string &path)
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
      try {
        toiletline::exit();
      } catch (Error &e) {
        /* TODO: A wild bug appeared! */
        show_error(e.to_string());
      }
    }
  }

  std::exit(code);
}

static std::vector<std::string> PATH_CACHE_EXTS{};
static std::vector<std::string> PATH_CACHE_DIRS{};

/* Program name without extension maps into indexes i and j for
 * PATH_DIRS and PATH_EXTENSIONS, so the path can be recreated as:
 * `PATH_DIRS[i] / (program_name + PATH_EXTENSIONS[j])` */
static std::unordered_map<std::string, std::tuple<usize, usize>> PATH_CACHE{};

/* FIXME: Cosmopolitan sucks and does not support std::filesystem::path in
 * unordered_map :c. This would be better off std::string. */
static std::optional<std::string> MAYBE_PATH = get_environment_variable("PATH");

template <class C>
static usize
cache_path_into(std::vector<C> &cache, std::string &&p)
{
  bool  found = false;
  usize n = 0;
  for (usize i = 0; i < cache.size(); i++) {
    if (cache[i] == p) {
      found = true;
      n = i;
      break;
    }
  }
  if (!found) {
    n = cache.size();
    cache.push_back(p);
  }
  return n;
}

void
clear_path_map()
{
  MAYBE_PATH = get_environment_variable("PATH");

  PATH_CACHE.clear();
  PATH_CACHE_DIRS.clear();

  PATH_CACHE_EXTS.clear();
  /* First extension entry is empty. */
  PATH_CACHE_EXTS.push_back("");
}

void
initialize_path_map()
{
  if (!MAYBE_PATH)
    return;

  std::string dir_string;
  std::string path_var = *MAYBE_PATH;

  for (const char &ch : path_var) {
    if (ch != PATH_DELIMITER) {
      dir_string += ch;
    } else {
      /* What the heck? A path in PATH that does not exist? Are you a Windows
       * user? */
      if (std::filesystem::exists(dir_string)) {
        std::filesystem::path               dir_path{dir_string};
        std::filesystem::directory_iterator dir{dir_path};

        usize dir_index =
            cache_path_into(PATH_CACHE_DIRS, std::move(dir_string));

        /* Initialize every file in the directory. */
        for (const std::filesystem::directory_entry &f : dir) {
          std::string fs = f.path().filename().string();

          usize ext_index = (sanitize_program_name(fs))
                                ? cache_path_into(PATH_CACHE_EXTS,
                                                  f.path().extension().string())
                                : 0;

          PATH_CACHE[fs] = {dir_index, ext_index};
        }
      }

      dir_string.clear();
    }
  }
}

std::optional<std::filesystem::path>
search_and_cache(const std::string &program_name)
{
  MAYBE_PATH = get_environment_variable("PATH");
  if (!MAYBE_PATH)
    return std::nullopt;

  std::string dir_string;
  std::string path_var = *MAYBE_PATH;

  for (const char &ch : path_var) {
    if (ch != PATH_DELIMITER) {
      dir_string += ch;
    } else if (std::filesystem::exists(dir_string)) {
      std::filesystem::path dir_path{dir_string};

      /* Cache the directory if it was not present before. */
      bool  found = false;
      usize dir_index = 0;

      for (usize dir_i = 0; dir_i < PATH_CACHE_DIRS.size(); dir_i++) {
        if (PATH_CACHE_DIRS[dir_i] == dir_string) {
          found = true;
          dir_index = dir_i;
          break;
        }
      }

      if (!found) {
        dir_index = cache_path_into(PATH_CACHE_DIRS, std::move(dir_string));
      }

      /* Actually try to find the file. */
      std::filesystem::path p = dir_path / program_name;
      for (usize ext_index = 0; ext_index < PATH_CACHE_EXTS.size(); ext_index++)
      {
        if (std::filesystem::path try_path =
                p.concat(PATH_CACHE_EXTS[ext_index]);
            std::filesystem::exists(try_path))
        {
          PATH_CACHE[program_name] = {dir_index, ext_index};
          return try_path;
        }
      }

      dir_string.clear();
    }
  }

  return std::nullopt;
}

/* TODO: Some directories have precedence over the others. */
std::optional<std::filesystem::path>
search_program_path(const std::string &program_name)
{
  std::string sp{program_name};

  bool s = sanitize_program_name(sp);

  if (auto p = PATH_CACHE.find(sp); p != PATH_CACHE.end()) {
    auto [dir, ext] = p->second;
    std::filesystem::path try_path = PATH_CACHE_DIRS[dir];

    if (s) {
      try_path /= program_name;
    } else {
      std::filesystem::path file_name = p->first;
      try_path /= file_name.concat(PATH_CACHE_EXTS[ext]);
    }

    /* Does this path still exist? */
    if (std::filesystem::exists(try_path))
      return try_path;
    else
      PATH_CACHE.erase(program_name);
  } else {
    /* We don't have cache? Newly added file? Try to search and cache it. */
    if (std::optional<std::filesystem::path> p = search_and_cache(program_name);
        p.has_value())
    {
      return *p;
    }
  }

  return std::nullopt;
}

} /* namespace utils */

} /* namespace shit */
