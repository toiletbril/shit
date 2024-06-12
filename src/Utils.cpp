#include "Utils.hpp"

#include "Builtin.hpp"
#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Lexer.hpp"
#include "Toiletline.hpp"

#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>

/* TODO: Support background processes. */
/* TODO: Support setting environment variables. */

namespace shit {

namespace utils {

template <class T>
static usize
find_pos_in_vec(const std::vector<T> &v, const T &p)
{
  for (usize i = 0; i < v.size(); i++) {
    if (v[i] == p) {
      return i;
    }
  }
  return std::string::npos;
}

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

static constexpr uchar PATH_DELIMITER = ':';

/* Only parent can execute some operations. */
static const pid_t PARENT_SHELL_PID = getpid();

/* TODO: Get rid of this for non-Windows enviroments. */
const static std::vector<std::string> OMITTED_SUFFIXES = {""};

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

void
set_default_signal_handlers()
{
  /* Ignore bullshit. */
  sigset_t sm = make_sigset(SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGSTOP, SIGTSTP);
  sigprocmask(SIG_BLOCK, &sm, nullptr);
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

  while (call_checked(waitpid(pid, &status, WNOHANG)) != pid) {
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
    call_checked(kill(pid, SIGKILL));
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
execute_program(const ExecContext &ec)
{
  std::vector<const char *> os_args = create_os_args(ec.program, ec.args);

  /* Cleanse the corruption of the holy child from evil signal spirits. */
  reset_signal_handlers();

  /* TODO: If execv() failed, try to execute the path as a shell script. */
  if (execv(std::get<std::filesystem::path>(ec.kind).c_str(),
            const_cast<char *const *>(os_args.data())) == -1)
  {
    throw shit::ErrorWithLocation{ec.location, last_system_error_message()};
  }

  SHIT_UNREACHABLE();
}

i32
execute_context(const ExecContext &&ec)
{
  i32 ret = -1;

  if (std::holds_alternative<std::filesystem::path>(ec.kind)) {
    pid_t child_pid = call_checked(fork());

    if (child_pid == 0) {
      /* Child. */
      if (ec.in) {
        call_checked(dup2(*ec.in, STDIN_FILENO));
        call_checked(close(*ec.in));
      }
      if (ec.out) {
        call_checked(dup2(*ec.out, STDOUT_FILENO));
        call_checked(close(*ec.out));
      }

      execute_program(ec);
    }

    /* Parent. */
    if (ec.in)
      call_checked(close(*ec.in));
    if (ec.out)
      call_checked(close(*ec.out));

    ret = wait_for_process(child_pid);
  } else {
    ret = execute_builtin(ec);

    if (ec.in)
      call_checked(close(*ec.in));
    if (ec.out)
      call_checked(close(*ec.out));
  }

  return ret;
}

i32
execute_contexts_with_pipes(std::vector<ExecContext> &ecs)
{
  SHIT_ASSERT(ecs.size() > 1);

  i32   ret = -1;
  i32   last_stdin = -1;
  pid_t last_pid = -1;

  bool is_first = true;

  for (ExecContext &ec : ecs) {
    i32 pipefds[2] = {-1, -1};

    bool is_last = &ec == &ecs.back();

    /* We need N - 1 pipes for N commands. The first command uses terminal's
     * stdin and pipe's stdout. i th process will use i - 1 th pipe's stdin and
     * i-th pipe's stdout. The last process will use only i - 1 pipe's
     * stdin. */
    if (!is_last) {
      call_checked(pipe(pipefds));
      ec.out = pipefds[1];
    }

    if (!is_first) {
      ec.in = last_stdin;
    }

    /* Builtin or an actual program? */
    if (std::holds_alternative<std::filesystem::path>(ec.kind)) {
      pid_t child_pid = call_checked(fork());

      /* TODO: Make call_checked() not leak fds on error. */
      if (child_pid == 0) {
        /* Child. Close reading end, dup2() previous reading end and a new
         * writing end to ourselves. */
        if (!is_last) {
          call_checked(close(pipefds[0]));
        }

        if (ec.in) {
          call_checked(dup2(*ec.in, STDIN_FILENO));
          call_checked(close(*ec.in));
        }
        if (ec.out) {
          call_checked(dup2(*ec.out, STDOUT_FILENO));
          call_checked(close(*ec.out));
        }

        execute_program(ec);
      }

      last_pid = child_pid;
    } else {
      /* Builtin. Everything is done in a single process, so just close the new
       * reading end. */
      /* TODO: Broken pipe. */
      ret = execute_builtin(ec);
    }

    /* Parent. Close the writing end and the last reading end. There shoudn't be
     * any loose ends remaining. When the child exits, pipes will be deleted. */
    if (!is_last)
      call_checked(close(pipefds[1]));
    if (!is_first)
      call_checked(close(last_stdin));

    is_first = false;
    last_stdin = pipefds[0];
  }

  ret = (last_pid != -1) ? wait_for_process(last_pid) : ret;

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
static bool
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

#elif defined _WIN32 /* __linux__ || BSD || __APPLE__ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static constexpr uchar PATH_DELIMITER = ';';

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
get_environment_variable(const std::string &key)
{
  char buffer[WIN32_MAX_ENV_SIZE] = {0};
  if (GetEnvironmentVariableA(key.c_str(), buffer, sizeof(buffer)) == 0) {
    return std::nullopt;
  }
  return std::string{buffer};
}

i32
execute_context(const ExecContext &&ec)
{
  i32 ret = -1;

  if (std::holds_alternative<std::filesystem::path>(ec.kind)) {
    std::string command_line;

    /* TODO: Remove CVE and escape quotes. */
    command_line += '"';
    command_line += ec.program;
    command_line += '"';

    if (ec.args.size() > 0) {
      for (usize i = 0; i < ec.args.size(); i++) {
        command_line += ' ';
        command_line += '"' + ec.args[i] + '"';
      }
    }

    PROCESS_INFORMATION pi{};
    STARTUPINFOA        si{.cb = sizeof(si)};

    if (CreateProcessA(
            std::get<std::filesystem::path>(ec.kind).string().c_str(),
            command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &si, &pi) == 0)
    {
      throw Error{last_system_error_message()};
    }

    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) {
      throw Error{"WaitForSingleObject() failed: " +
                  last_system_error_message()};
    }

    DWORD code;
    if (GetExitCodeProcess(pi.hProcess, &code) == 0) {
      throw Error{"GetExitCodeProcess() failed: " +
                  last_system_error_message()};
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    ret = code;
  } else {
    ret = execute_builtin(ec);
  }

  return ret;
}

/* TODO: */
i32
execute_contexts_with_pipes(std::vector<ExecContext> &ecs)
{
  SHIT_UNUSED(ecs);
  throw shit::ErrorWithLocation{ecs[1].location,
                                "Pipes are not implemented (Utils)"};
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

ExecContext
make_exec_context(const std::string              &program,
                  const std::vector<std::string> &args, usize location)
{
  std::variant<shit::Builtin::Kind, std::filesystem::path> exec_kind;

  std::optional<Builtin::Kind>         bk;
  std::optional<std::filesystem::path> p;

  /* This isn't a path? */
  if (program.find('/') == std::string::npos) {
    bk = search_builtin(program);

    if (!bk) {
      /* Not a builtin, try to search PATH. */
      p = utils::search_program_path(program);
    }
  } else {
    /* This is a path. */
    /* TODO: Sanitize extensions here too. */
    p = utils::canonicalize_path(program);
  }

  /* Builtins take precedence over programs. */
  if (!bk) {
    if (p)
      exec_kind = *p;
    else
      throw ErrorWithLocation{location, "Program '" + program + "' not found"};
  } else {
    exec_kind = *bk;
  }

  return {exec_kind, program, args, location};
}

const static std::vector<std::string> OMITTED_SUFFIXES = {
    /* First extension entry should be empty. */
    "", ".exe", ".com", ".scr", ".bat",
};

constexpr static usize MIN_SUFFIX_LEN = 3;

static usize
sanitize_program_name(std::string &program_name)
{
#if defined __COSMOPOLITAN__
  if (IsWindows())
#endif
  {
    usize extension_pos = program_name.rfind(".");

    if (extension_pos != std::string::npos &&
        extension_pos + MIN_SUFFIX_LEN < program_name.length())
    {
      std::string extension = program_name.substr(extension_pos);

      if (usize i = find_pos_in_vec(OMITTED_SUFFIXES, extension);
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
quit(i32 code, bool should_goodbye)
{
  if (should_goodbye) {
    show_error("Goodbye.");
  }

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

static std::vector<std::string> PATH_CACHE_DIRS{};

/* Program name without extension maps into indexes i and j for
 * PATH_DIRS and PATH_EXTENSIONS, so the path can be recreated as:
 * `PATH_DIRS[i] / (program_name + PATH_EXTENSIONS[j])` */
static std::unordered_map<std::string, std::tuple<usize, usize>> PATH_CACHE{};

/* FIXME: Cosmopolitan sucks and does not support std::filesystem::path in
 * unordered_map :c. This would be better off std::string. */
static std::optional<std::string> MAYBE_PATH = get_environment_variable("PATH");

template <class T>
static usize
cache_path_into(std::vector<T> &cache, T &&p)
{
  usize n = find_pos_in_vec(cache, p);
  if (n == std::string::npos) {
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
      continue;
    }

    /* What the heck? A path in PATH that does not exist? Are you a Windows
     * user? */
    if (std::filesystem::exists(dir_string)) {
      std::filesystem::path               dir_path{dir_string};
      std::filesystem::directory_iterator dir{dir_path};

      usize dir_index = cache_path_into(PATH_CACHE_DIRS, std::move(dir_string));

      /* Initialize every file in the directory. */
      for (const std::filesystem::directory_entry &f : dir) {
        std::string fs = f.path().filename().string();
        PATH_CACHE[fs] = {dir_index, sanitize_program_name(fs)};
      }
    }

    dir_string.clear();
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
      continue;
    }

    if (std::filesystem::exists(dir_string)) {
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
      std::filesystem::path full_path = dir_path / program_name;
      std::string           full_path_str = full_path.string();

      /* This file already has an extesion specified? */
      if (usize explicit_ext = sanitize_program_name(full_path_str);
          explicit_ext == 0)
      {
        for (usize ext_index = 0; ext_index < OMITTED_SUFFIXES.size();
             ext_index++)
        {
          std::string try_path =
              full_path.string() + OMITTED_SUFFIXES[ext_index];

          if (std::filesystem::exists(try_path)) {
            PATH_CACHE[program_name] = {dir_index, ext_index};
            return try_path;
          }
        }
      } else if (std::filesystem::exists(full_path)) {
        PATH_CACHE[program_name] = {dir_index, explicit_ext};
        return full_path;
      }
    }

    dir_string.clear();
  }

  return std::nullopt;
}

std::vector<std::string>
simple_shell_expand_args(const std::vector<std::string> &args)
{
  std::vector<std::string> expanded_args{};
  expanded_args.reserve(args.size());

  for (const std::string &arg : args) {
    expanded_args.push_back(utils::simple_shell_expand(arg).value_or(arg));
  }

  return expanded_args;
}

/* TODO: Some directories have precedence over the others. */
std::optional<std::filesystem::path>
search_program_path(const std::string &program_name)
{
  std::string sp{program_name};

  usize s_ext = sanitize_program_name(sp);

  if (auto p = PATH_CACHE.find(sp); p != PATH_CACHE.end()) {
    auto [dir, ext] = p->second;
    std::filesystem::path try_path = PATH_CACHE_DIRS[dir];

    if (s_ext > 0) {
      try_path /= program_name;
    } else {
      std::filesystem::path file_name = p->first + '.';
      try_path /= file_name.concat(OMITTED_SUFFIXES[ext]);
    }

    /* Does this path still exist? */
    if (!std::filesystem::exists(try_path)) {
      PATH_CACHE.erase(program_name);
    } else {
      return try_path;
    }
  }

  /* We don't have cache? Newly added file? PATH changed? Try to search and
   * cache the program. */
  if (std::optional<std::filesystem::path> p = search_and_cache(program_name);
      p.has_value())
  {
    return p.value();
  }

  return std::nullopt;
}

} /* namespace utils */

} /* namespace shit */
