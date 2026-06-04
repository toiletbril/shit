#include "Utils.hpp"

#include "Builtin.hpp"
#include "Cli.hpp"
#include "Containers.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"

#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <list>
#include <optional>
#include <unordered_map>

/* FIXME: std::filesystem::exists() is VERY slow. */

/* TODO: Support background processes. */
/* TODO: Support setting environment variables. */

namespace shit {

namespace utils {

std::string
merge_tokens_to_string(const ArrayList<const Token *> &v)
{
  std::string r{};
  for (const shit::Token *t : v) {
    r += t->raw_string();
    if (t != v.back()) {
      r += ' ';
    }
  }
  return r;
}

i32
execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async)
{
  if (!ec.is_builtin()) {
    os::process p = os::execute_program(std::move(ec));
    if (is_async) {
      cxt.set_last_background_pid(os::process_id_of(p));
      return 0;
    }
    return os::wait_and_monitor_process(p);
  } else {
    return execute_builtin(std::move(ec), cxt);
  }

  SHIT_UNREACHABLE();
}

i32
execute_contexts_with_pipes(std::vector<ExecContext> &&ecs, EvalContext &cxt,
                            bool is_async)
{
  SHIT_ASSERT(ecs.size() > 1);

  i32 ret = 0;

  /* Every external stage is collected so all of them are reaped, not only the
     last. Otherwise a first stage like yes is left a zombie when the last stage
     exits. */
  std::vector<os::process> children{};
  os::process last_child = SHIT_INVALID_PROCESS;
  os::descriptor last_stdin = SHIT_INVALID_FD;

  bool is_first = true;

  for (ExecContext &ec : ecs) {
    Maybe<os::Pipe> pipe;

    bool is_last = (&ec == &ecs.back());

    if (!is_last) {
      pipe = os::make_pipe();
      if (!pipe) {
        throw ErrorWithLocation{ec.source_location(), "Could not open a pipe"};
      }
      /* An explicit > redirect on the stage takes its standard output, so the
         pipe end goes unused and closes at once. */
      if (!ec.out_fd)
        ec.out_fd = pipe->out;
      else
        os::close_fd(pipe->out);
    }

    if (!is_first) {
      if (!ec.in_fd)
        ec.in_fd = last_stdin;
      else
        os::close_fd(last_stdin);
    }
    if (!is_last) {
      last_stdin = pipe->in;
    }

    if (!ec.is_builtin()) {
      os::process child = os::execute_program(std::move(ec));
      children.push_back(child);
      last_child = child;
    } else {
      /* A builtin runs in this process, so its status stands in for the stage.
       */
      ret = execute_builtin(std::move(ec), cxt);
    }

    is_first = false;
  }

  if (is_async) {
    if (last_child != SHIT_INVALID_PROCESS)
      cxt.set_last_background_pid(os::process_id_of(last_child));
    return ret;
  }

  /* Wait for every stage. The pipeline status is the last stage's, so a stage
     that is the last external child sets the result. */
  for (os::process child : children) {
    i32 status = os::wait_and_monitor_process(child);
    if (child == last_child) ret = status;
  }

  return ret;
}

void
string_replace(std::string &s, const std::string_view to_replace,
               const std::string_view replace_with)
{
  std::string b{};
  b.reserve(s.size());

  std::size_t i{0};
  std::size_t p{0};

  for (;;) {
    p = i;
    i = s.find(to_replace, i);
    if (i == std::string::npos) break;
    b.append(s, p, i - p);
    b += replace_with;
    i += to_replace.size();
  }

  b.append(s, p, s.size() - p);
  s.swap(b);
}

std::string
lowercase_string(std::string_view s)
{
  std::string l{};
  l.reserve(s.length());
  for (usize i = 0; i < s.length(); i++)
    l += std::tolower(s[i]);
  return l;
}

Maybe<std::filesystem::path>
canonicalize_path(const std::string &path)
{
  std::filesystem::path p{path};

  if (p.is_relative() && p.string().find('/') != std::string::npos) {
    p = std::filesystem::absolute(p);
  }

  p = p.lexically_normal();

  /* If there's no extension, we may have to add it ourselves. */
  if (p.extension().empty() &&
      path.back() != '.' /* fs::path strips the ending dot. TODO: test */)
  {
    size_t i = 0;
    while (!std::filesystem::exists(p) && i < os::OMITTED_SUFFIXES.size())
      p.replace_extension(os::OMITTED_SUFFIXES[i++]);
  }

  if (!std::filesystem::exists(p)) return shit::nothing;

  return p;
}

void
set_current_directory(const std::filesystem::path &path)
{
  try {
    std::filesystem::current_path(path);
  } catch (const std::filesystem::filesystem_error &err) {
    SHIT_UNUSED(err);
    throw shit::Error{os::last_system_error_message()};
  }
}

std::filesystem::path
get_current_directory()
{
  try {
    return std::filesystem::current_path();
  } catch (const std::filesystem::filesystem_error &err) {
    SHIT_UNUSED(err);
    throw shit::Error{os::last_system_error_message()};
  }
}

/* Inspiration taken from https://github.com/tsoding/glob.h :3
 * This fragment is under MIT License (c) Alexey Kutepov <reximkut@gmail.com> */
static bool
is_glob_char_active(const ArrayList<bool> &glob_active, usize index)
{
  return index < glob_active.size() && glob_active[index];
}

bool
glob_matches(std::string_view glob, std::string_view str,
             const ArrayList<bool> &glob_active, usize mask_offset)
{
  usize s = 0;
  usize g = 0;

  while (g < glob.length() && s < str.length()) {
    if (!is_glob_char_active(glob_active, mask_offset + g)) {
      if (glob[g++] != str[s++])
        return false;
      else
        continue;
    }

    switch (glob[g]) {
    case '?': {
      g++;
      s++;
    } break;

    case '*': {
      /* A star at the end of the glob matches the entire rest of the string, so
         there is no need to try every split. This keeps a plain * component,
         the common case, linear in the string instead of quadratic. */
      if (g + 1 >= glob.length()) return true;
      if (glob_matches(glob.substr(g + 1), str.substr(s), glob_active,
                       mask_offset + g + 1))
      {
        return true;
      }
      s++;
    } break;

    case '[': {
      bool is_matched = false;
      bool should_negate = false;

      /* clang-format off */
#define GLOB_GROUP_ERR()                                                       \
  throw ErrorWithLocationAndDetails{                                           \
      {0, 0},                               \
      "Unclosed '[' group",                                                    \
      {0, 1},                                                \
      "expected ] here"                                                        \
  };
      /* clang-format on */

      /* A bracket with no closing ] is not a character class, so the [ is a
         literal character, as POSIX specifies. A ] right after [ or [^ is a
         member, so the scan for the closing ] starts past it. */
      usize close_scan = g + 1;
      if (close_scan < glob.length() && glob[close_scan] == '^') close_scan++;
      if (close_scan < glob.length() && glob[close_scan] == ']') close_scan++;
      bool has_closing_bracket = false;
      for (; close_scan < glob.length(); close_scan++) {
        if (glob[close_scan] == ']') {
          has_closing_bracket = true;
          break;
        }
      }
      if (!has_closing_bracket) {
        if (glob[g] != str[s]) return false;
        g++;
        s++;
        break;
      }

      g++; /* skip [ */
      if (g >= glob.length()) GLOB_GROUP_ERR();

      if (glob[g] == '^') {
        g++;
        should_negate = true;

        if (g >= glob.length()) GLOB_GROUP_ERR();
      }

      char prev_glob_ch = glob[g++];
      is_matched |= (prev_glob_ch == str[s]);

      while (g < glob.length() && glob[g] != ']') {
        if (glob[g] == '-') {
          g++;
          if (g >= glob.length()) GLOB_GROUP_ERR();

          if (glob[g] == ']') {
            is_matched |= ('-' == str[s]);
          } else {
            is_matched |= (prev_glob_ch <= str[s] && str[s] <= glob[g]);
            prev_glob_ch = glob[g++];
          }
        } else {
          prev_glob_ch = glob[g++];
          is_matched |= (prev_glob_ch == str[s]);
        }
      }

      if (g >= glob.length() || glob[g] != ']') GLOB_GROUP_ERR();
      if (should_negate) is_matched = !is_matched;
      if (!is_matched) return false;

      g++;
      s++;
    } break;

    default:
      if (glob[g++] != str[s++]) return false;
    }
  }

  if (s >= str.length()) {
    while (g < glob.length() && glob[g] == '*' &&
           is_glob_char_active(glob_active, mask_offset + g))
    {
      g++;
    }

    if (g >= glob.length()) return true;
  }

  return false;
}

[[noreturn]] void
quit(i32 code, bool should_goodbye)
{
  u8 actual_code = static_cast<u8>(code);

  /* Cleanup for main proccess. */
  if (!os::is_child_process()) {
    if (toiletline::is_active()) {
      try {
        toiletline::exit();
      } catch (const Error &e) {
        /* TODO: A wild bug appeared! */
        show_message(e.to_string());
      }
    }

    if (should_goodbye) {
      std::string code_str =
          (code != 0) ? " (Code " + std::to_string(actual_code) + ")" : "";
      show_message("Goodbye :c" + code_str);
    }
  }

  std::exit(actual_code);
}

using DirIndex = usize;

static std::vector<std::string> PATH_CACHE_DIRS{};

/* Program name without extension maps into indexes i and j for
 * PATH_DIRS and PATH_EXTENSIONS, so the path can be recreated as:
 * `PATH_DIRS[i] / (program_name + PATH_EXTENSIONS[j])` */
static std::unordered_map<std::string,
                          std::list<std::tuple<DirIndex, os::ExtIndex>>>
    PATH_CACHE{};

static std::optional<std::string> MAYBE_PATH =
    os::get_environment_variable("PATH");

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

/* Split PATH into its directory components. The last component carries no
   trailing delimiter, so a plain delimiter scan drops it and the directory is
   never searched. POSIX treats an empty component as the current directory. */
static std::vector<std::string>
split_path_dirs(const std::string &path_var)
{
  std::vector<std::string> dirs{};
  std::string current{};

  for (const char &ch : path_var) {
    if (ch == os::PATH_DELIMITER) {
      dirs.push_back(current.empty() ? "." : current);
      current.clear();
    } else {
      current += ch;
    }
  }
  dirs.push_back(current.empty() ? "." : current);

  return dirs;
}

void
initialize_path_map()
{
  if (!MAYBE_PATH) return;

  for (std::string &dir_string : split_path_dirs(*MAYBE_PATH)) {
    try {
      /* What the heck? A path in PATH that does not exist? Are you a
       * Windows user? */
      if (std::filesystem::exists(dir_string)) {
        std::filesystem::directory_iterator di{dir_string};

        usize dir_index =
            cache_path_into(PATH_CACHE_DIRS, std::move(dir_string));

        /* Initialize every file in the directory. */
        for (const std::filesystem::directory_entry &f : di) {
          std::string fs = f.path().filename().string();
          PATH_CACHE[fs].push_back(
              {dir_index, os::erase_extension_and_get_its_index(fs)});
        }
      }
    } catch (const std::filesystem::filesystem_error &e) {
#if 0
      shit::show_message(
          "Unable to read '" + e.path1().string() +
          "' while reading PATH: " + os::last_system_error_message());
#else
      SHIT_UNUSED(e);
#endif
    }
  }
}

std::list<std::filesystem::path>
search_and_cache(const std::string &program_name)
{
  MAYBE_PATH = os::get_environment_variable("PATH");
  if (!MAYBE_PATH) return {};

  std::list<std::filesystem::path> result{};

  for (std::string &dir_string : split_path_dirs(*MAYBE_PATH)) {
    bool is_valid_dir = false;

    try {
      is_valid_dir = std::filesystem::exists(dir_string);
    } catch (const std::filesystem::filesystem_error &e) {
#if 0
      shit::show_message(
          "Unable to read '" + dir_string +
          "' while reading PATH: " + os::last_system_error_message());
#else
      SHIT_UNUSED(e);
#endif
    }

    if (!is_valid_dir) continue;

    std::filesystem::path dir_path{dir_string};

    /* Cache the directory if it was not present before. */
    bool found = false;
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
    std::string full_path_str = full_path.string();

    /* This file already has an extesion specified? */
    if (os::ExtIndex explicit_ext =
            os::erase_extension_and_get_its_index(full_path_str);
        explicit_ext == 0)
    {
      for (usize ext_index = 0; ext_index < os::OMITTED_SUFFIXES.size();
           ext_index++)
      {
        std::string try_path =
            full_path.string() + os::OMITTED_SUFFIXES[ext_index];

        if (std::filesystem::exists(try_path)) {
          PATH_CACHE[program_name].push_back({dir_index, ext_index});
          result.emplace_back(try_path);
        }
      }
    } else if (std::filesystem::exists(full_path)) {
      PATH_CACHE[program_name].push_back({dir_index, explicit_ext});
      result.emplace_back(full_path);
    }
  }

  return result;
}

std::filesystem::path
make_absolute_path_from_cache(const std::filesystem::path &program_name,
                              DirIndex d, os::ExtIndex e)
{
  std::filesystem::path full_path = PATH_CACHE_DIRS[d] / program_name;
  if (e != 0) full_path.replace_extension(os::OMITTED_SUFFIXES[e]);
  return full_path;
}

/* TODO: Optimization. */
std::list<std::filesystem::path>
search_program_path(const std::string &program_name)
{
  std::string sp{program_name};
  std::list<std::filesystem::path> result{};

  os::ExtIndex ext = os::erase_extension_and_get_its_index(sp);

  if (auto cache_entry = PATH_CACHE.find(sp); cache_entry != PATH_CACHE.end()) {
    auto &prefixes = cache_entry->second;
    auto p = prefixes.begin();

    while (p != prefixes.end()) {
      auto &[dir, cache_ext] = *p;

      std::filesystem::path tp = make_absolute_path_from_cache(
          program_name, dir, (ext == 0) ? cache_ext : ext);

      if (std::filesystem::exists(tp)) {
        result.emplace_back(tp);
        ++p;
      } else {
        p = prefixes.erase(p);
      }
    }
  }

  if (result.empty()) {
    result = search_and_cache(program_name);
  }

  return result;
}

} /* namespace utils */

} /* namespace shit */
