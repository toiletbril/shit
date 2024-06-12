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
#include <variant>

/* TODO: Support background processes. */
/* TODO: Support setting environment variables. */

namespace shit {

namespace utils {

i32
execute_context(const ExecContext &&ec)
{
  if (std::holds_alternative<std::filesystem::path>(ec.kind)) {
    return os::wait_and_monitor_process(os::execute_program(ec));
  } else {
    return execute_builtin(ec);
  }

  SHIT_UNREACHABLE();
}

i32
execute_contexts_with_pipes(std::vector<ExecContext> &ecs)
{
  SHIT_ASSERT(ecs.size() > 1);

  i32 ret = 1;

  os::process    last_child = SHIT_INVALID_PROCESS;
  os::descriptor last_stdin = SHIT_INVALID_FD;

  bool is_first = true;

  for (ExecContext &ec : ecs) {
    std::optional<os::Pipe> pipe;

    bool is_last = &ec == &ecs.back();

    if (!is_last) {
      pipe = os::make_pipe();
      if (!pipe) {
        throw ErrorWithLocation{ec.location, "Could not open a pipe"};
      }
      ec.out = pipe->stdout_write;
    }

    if (!is_first) {
      ec.in = last_stdin;
    }

    if (std::holds_alternative<std::filesystem::path>(ec.kind)) {
      last_child = os::execute_program(ec);
    } else {
      ret = execute_builtin(ec);

      if (ec.in)
        os::close_fd(*ec.in);
      if (ec.out)
        os::close_fd(*ec.out);
    }

    /* Unused handle. */
    os::close_fd(pipe->stdin_read);

    is_first = false;
    last_stdin = pipe->stdout_read;
  }

  if (last_child != SHIT_INVALID_FD) {
    ret = os::wait_and_monitor_process(last_child);
  }

  return ret;
}

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

    std::optional<std::filesystem::path> u = os::get_home_directory();
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
  if (!os::is_child_process()) {
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
static std::optional<std::string> MAYBE_PATH =
    os::get_environment_variable("PATH");

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
  MAYBE_PATH = os::get_environment_variable("PATH");
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
    if (ch != os::PATH_DELIMITER) {
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
        PATH_CACHE[fs] = {dir_index, os::sanitize_program_name(fs)};
      }
    }

    dir_string.clear();
  }
}

std::optional<std::filesystem::path>
search_and_cache(const std::string &program_name)
{
  MAYBE_PATH = os::get_environment_variable("PATH");
  if (!MAYBE_PATH)
    return std::nullopt;

  std::string dir_string;
  std::string path_var = *MAYBE_PATH;

  for (const char &ch : path_var) {
    if (ch != os::PATH_DELIMITER) {
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
      if (usize explicit_ext = os::sanitize_program_name(full_path_str);
          explicit_ext == 0)
      {
        for (usize ext_index = 0; ext_index < os::OMITTED_SUFFIXES.size();
             ext_index++)
        {
          std::string try_path =
              full_path.string() + os::OMITTED_SUFFIXES[ext_index];

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
    expanded_args.push_back(simple_shell_expand(arg).value_or(arg));
  }

  return expanded_args;
}

/* TODO: Some directories have precedence over the others. */
std::optional<std::filesystem::path>
search_program_path(const std::string &program_name)
{
  std::string sp{program_name};

  usize s_ext = os::sanitize_program_name(sp);

  if (auto p = PATH_CACHE.find(sp); p != PATH_CACHE.end()) {
    auto [dir, ext] = p->second;
    std::filesystem::path try_path = PATH_CACHE_DIRS[dir];

    if (s_ext > 0) {
      try_path /= program_name;
    } else {
      std::filesystem::path file_name = p->first + '.';
      try_path /= file_name.concat(os::OMITTED_SUFFIXES[ext]);
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
