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
#include <optional>

/* FIXME: std::filesystem::exists() is VERY slow. */

/* TODO: Support background processes. */
/* TODO: Support setting environment variables. */

namespace shit {

namespace utils {

String
merge_tokens_to_string(const ArrayList<const Token *> &v)
{
  String r{};
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
    /* The command word is kept for the job table before the context is moved
       into the spawn. */
    std::string command =
        is_async ? std::string{ec.program().c_str(), ec.program().size()}
                 : std::string{};

    os::process p = os::execute_program(std::move(ec));
    if (is_async) {
      cxt.set_last_background_pid(os::process_id_of(p));
      int id = cxt.register_job(p, command);
      if (cxt.shell_is_interactive())
        shit::print_to_standard_error("[" + std::to_string(id) + "] " +
                                      std::to_string(os::process_id_of(p)) +
                                      "\n");
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
  ArrayList<os::process> children{};
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
      children.push(child);
      last_child = child;
    } else {
      /* A builtin runs in this process, so its status stands in for the stage.
       */
      ret = execute_builtin(std::move(ec), cxt);
    }

    is_first = false;
  }

  if (is_async) {
    if (last_child != SHIT_INVALID_PROCESS) {
      cxt.set_last_background_pid(os::process_id_of(last_child));
      int id = cxt.register_job(last_child, "pipeline");
      if (cxt.shell_is_interactive())
        shit::print_to_standard_error(
            "[" + std::to_string(id) + "] " +
            std::to_string(os::process_id_of(last_child)) + "\n");
    }
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
string_replace(std::string &s, const StringView to_replace,
               const StringView replace_with)
{
  std::string b{};
  b.reserve(s.size());

  std::size_t i{0};
  std::size_t p{0};

  for (;;) {
    p = i;
    /* The view carries no null terminator, so the bytes are matched through the
       pointer and length overload of find. */
    i = s.find(to_replace.data, i, to_replace.length);
    if (i == std::string::npos) break;
    b.append(s, p, i - p);
    b.append(replace_with.data, replace_with.length);
    i += to_replace.length;
  }

  b.append(s, p, s.size() - p);
  s.swap(b);
}

String
lowercase_string(StringView s)
{
  String l{};
  l.reserve(s.size());
  for (usize i = 0; i < s.size(); i++)
    l.push(static_cast<char>(std::tolower(s[i])));
  return l;
}

static bool
is_ascii_whitespace(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}

/* Turn an accumulated magnitude and sign into a saturating signed result. The
   per-base parsers share this so only the digit loop stays base-specific. */
static i64
saturate_signed_magnitude(u64 magnitude, bool is_negative, bool has_overflowed)
{
  if (is_negative) {
    if (has_overflowed || magnitude > static_cast<u64>(INT64_MAX) + 1)
      return INT64_MIN;
    return -static_cast<i64>(magnitude);
  }
  if (has_overflowed || magnitude > static_cast<u64>(INT64_MAX))
    return INT64_MAX;
  return static_cast<i64>(magnitude);
}

static Error
not_an_integer_error(StringView text)
{
  return Error{"'" + std::string{text.data, text.length} +
               "' is not a valid integer"};
}

ErrorOr<i64>
parse_decimal_integer(StringView text)
{
  usize offset = 0;
  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;

  bool is_negative = false;
  if (offset < text.length &&
      (text.data[offset] == '+' || text.data[offset] == '-'))
  {
    is_negative = text.data[offset] == '-';
    offset++;
  }

  u64 magnitude = 0;
  bool has_digits = false;
  bool has_overflowed = false;
  while (offset < text.length && text.data[offset] >= '0' &&
         text.data[offset] <= '9')
  {
    u64 digit = static_cast<u64>(text.data[offset] - '0');
    has_digits = true;
    if (magnitude > (UINT64_MAX - digit) / 10)
      has_overflowed = true;
    else
      magnitude = magnitude * 10 + digit;
    offset++;
  }

  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;
  if (!has_digits || offset != text.length) return not_an_integer_error(text);
  return saturate_signed_magnitude(magnitude, is_negative, has_overflowed);
}

ErrorOr<i64>
parse_octal_integer(StringView text)
{
  usize offset = 0;
  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;

  bool is_negative = false;
  if (offset < text.length &&
      (text.data[offset] == '+' || text.data[offset] == '-'))
  {
    is_negative = text.data[offset] == '-';
    offset++;
  }

  u64 magnitude = 0;
  bool has_digits = false;
  bool has_overflowed = false;
  while (offset < text.length && text.data[offset] >= '0' &&
         text.data[offset] <= '7')
  {
    u64 digit = static_cast<u64>(text.data[offset] - '0');
    has_digits = true;
    if (magnitude > (UINT64_MAX - digit) / 8)
      has_overflowed = true;
    else
      magnitude = magnitude * 8 + digit;
    offset++;
  }

  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;
  if (!has_digits || offset != text.length) return not_an_integer_error(text);
  return saturate_signed_magnitude(magnitude, is_negative, has_overflowed);
}

ErrorOr<i64>
parse_hexadecimal_integer(StringView text)
{
  usize offset = 0;
  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;

  bool is_negative = false;
  if (offset < text.length &&
      (text.data[offset] == '+' || text.data[offset] == '-'))
  {
    is_negative = text.data[offset] == '-';
    offset++;
  }

  /* A leading 0x is the conventional hexadecimal marker and is consumed before
     the digits, as std::stoll with base 16 accepts it. */
  if (offset + 1 < text.length && text.data[offset] == '0' &&
      (text.data[offset + 1] == 'x' || text.data[offset + 1] == 'X'))
  {
    offset += 2;
  }

  u64 magnitude = 0;
  bool has_digits = false;
  bool has_overflowed = false;
  for (; offset < text.length; offset++) {
    char current = text.data[offset];
    u64 digit = 0;
    if (current >= '0' && current <= '9')
      digit = static_cast<u64>(current - '0');
    else if (current >= 'a' && current <= 'f')
      digit = static_cast<u64>(current - 'a' + 10);
    else if (current >= 'A' && current <= 'F')
      digit = static_cast<u64>(current - 'A' + 10);
    else
      break;

    has_digits = true;
    if (magnitude > (UINT64_MAX - digit) / 16)
      has_overflowed = true;
    else
      magnitude = magnitude * 16 + digit;
  }

  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;
  if (!has_digits || offset != text.length) return not_an_integer_error(text);
  return saturate_signed_magnitude(magnitude, is_negative, has_overflowed);
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

  if (!std::filesystem::exists(p)) return shit::None;

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
glob_matches(StringView glob, StringView str,
             const ArrayList<bool> &glob_active, usize mask_offset)
{
  usize s = 0;
  usize g = 0;

  while (g < glob.size() && s < str.size()) {
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
      if (g + 1 >= glob.size()) return true;
      if (glob_matches(glob.substring(g + 1), str.substring(s), glob_active,
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
      if (close_scan < glob.size() && glob[close_scan] == '^') close_scan++;
      if (close_scan < glob.size() && glob[close_scan] == ']') close_scan++;
      bool has_closing_bracket = false;
      for (; close_scan < glob.size(); close_scan++) {
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
      if (g >= glob.size()) GLOB_GROUP_ERR();

      if (glob[g] == '^') {
        g++;
        should_negate = true;

        if (g >= glob.size()) GLOB_GROUP_ERR();
      }

      char prev_glob_ch = glob[g++];
      is_matched |= (prev_glob_ch == str[s]);

      while (g < glob.size() && glob[g] != ']') {
        if (glob[g] == '-') {
          g++;
          if (g >= glob.size()) GLOB_GROUP_ERR();

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

      if (g >= glob.size() || glob[g] != ']') GLOB_GROUP_ERR();
      if (should_negate) is_matched = !is_matched;
      if (!is_matched) return false;

      g++;
      s++;
    } break;

    default:
      if (glob[g++] != str[s++]) return false;
    }
  }

  if (s >= str.size()) {
    while (g < glob.size() && glob[g] == '*' &&
           is_glob_char_active(glob_active, mask_offset + g))
    {
      g++;
    }

    if (g >= glob.size()) return true;
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
      String code_str{};
      if (code != 0) {
        code_str += " (Code ";
        code_str += std::to_string(actual_code);
        code_str += ')';
      }
      show_message("Goodbye :c" + code_str);
    }
  }

  std::exit(actual_code);
}

/* The program name without its extension maps to every absolute path where it
   was found. The resolved path is stored directly, so a lookup returns it
   without rebuilding from a directory index. The resizable map carries a packed
   key per slot, so a lookup rejects a mismatch in two words before the byte
   compare. */
static HashMap<ArrayList<String>> PATH_CACHE{heap_allocator()};

static std::optional<std::string> MAYBE_PATH =
    os::get_environment_variable("PATH");

/* Append one resolved absolute path under a program name, creating the list on
   the first hit. */
static void
cache_resolved_path(StringView name, const std::string &full_path)
{
  PATH_CACHE.get_or_create(name, ArrayList<String>{})
      .push(String{heap_allocator(),
                   StringView{full_path.data(), full_path.size()}});
}

void
clear_path_map()
{
  MAYBE_PATH = os::get_environment_variable("PATH");
  PATH_CACHE.clear();
}

/* Split PATH into its directory components. The last component carries no
   trailing delimiter, so a plain delimiter scan drops it and the directory is
   never searched. POSIX treats an empty component as the current directory. */
static ArrayList<String>
split_path_dirs(const std::string &path_var)
{
  ArrayList<String> dirs{};
  String current{};

  for (const char &ch : path_var) {
    if (ch == os::PATH_DELIMITER) {
      dirs.push(current.empty() ? String{"."} : current);
      current.clear();
    } else {
      current += ch;
    }
  }
  dirs.push(current.empty() ? String{"."} : current);

  return dirs;
}

void
initialize_path_map()
{
  if (!MAYBE_PATH) return;

  for (const String &dir_string : split_path_dirs(*MAYBE_PATH)) {
    try {
      /* What the heck? A path in PATH that does not exist? Are you a
       * Windows user? */
      if (std::filesystem::exists(dir_string.c_str())) {
        /* Cache every file in the directory under its name without an omitted
           extension, pointing at its full path. */
        for (const std::filesystem::directory_entry &f :
             std::filesystem::directory_iterator{dir_string.c_str()})
        {
          std::string name = f.path().filename().string();
          os::erase_extension_and_get_its_index(name);
          cache_resolved_path(StringView{name.data(), name.size()},
                              f.path().string());
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

ArrayList<std::filesystem::path>
search_and_cache(const std::string &program_name)
{
  MAYBE_PATH = os::get_environment_variable("PATH");
  if (!MAYBE_PATH) return ArrayList<std::filesystem::path>{};

  ArrayList<std::filesystem::path> result{};

  for (const String &dir_string : split_path_dirs(*MAYBE_PATH)) {
    bool is_valid_dir = false;

    try {
      is_valid_dir = std::filesystem::exists(dir_string.c_str());
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

    /* The cache key is the program name without an omitted extension, the same
       key the lookup uses. */
    std::string key{program_name};
    os::erase_extension_and_get_its_index(key);

    std::filesystem::path full_path =
        std::filesystem::path{dir_string.c_str()} / program_name;
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
          cache_resolved_path(StringView{key.data(), key.size()}, try_path);
          result.push(std::filesystem::path{try_path});
        }
      }
    } else if (std::filesystem::exists(full_path)) {
      cache_resolved_path(StringView{key.data(), key.size()},
                          full_path.string());
      result.push(std::move(full_path));
    }
  }

  return result;
}

ArrayList<std::filesystem::path>
search_program_path(const std::string &program_name)
{
  std::string sp{program_name};
  ArrayList<std::filesystem::path> result{};

  os::ExtIndex typed_extension = os::erase_extension_and_get_its_index(sp);

  /* A name typed with an explicit extension is matched exactly by the search,
     so the extension-stripped cache key would resolve the wrong file. The cache
     is consulted only when no extension was typed, which on POSIX is always. */
  if (typed_extension == 0) {
    if (ArrayList<String> *cached = const_cast<ArrayList<String> *>(
            PATH_CACHE.find(StringView{sp.data(), sp.size()})))
    {
      ArrayList<String> kept{};
      for (usize i = 0; i < cached->size(); i++) {
        const String &path_string = (*cached)[i];
        std::filesystem::path tp{
            std::string{path_string.c_str(), path_string.size()}};
        if (std::filesystem::exists(tp)) {
          result.push(std::filesystem::path{tp});
          kept.push(String{heap_allocator(),
                           StringView{path_string.c_str(), path_string.size()}});
        }
      }
      /* Drop entries that no longer exist, so a later lookup does not stat them
         again. The directory exists check is slow, so this keeps the cache from
         growing with dead paths. */
      if (kept.size() != cached->size()) *cached = std::move(kept);
      if (result.size() != 0) return result;
    }
  }

  return search_and_cache(program_name);
}

Maybe<std::string>
read_entire_file(const std::string &path)
{
  Maybe<os::descriptor> file =
      os::open_file_descriptor(path, os::FileOpenMode::Read);
  if (!file) return None;

  std::string contents{};
  char buffer[4096];
  for (;;) {
    Maybe<usize> read_count = os::read_fd(*file, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    contents.append(buffer, *read_count);
  }
  os::close_fd(*file);
  return contents;
}

std::string
read_entire_standard_input()
{
  std::string contents{};
  char buffer[4096];
  for (;;) {
    Maybe<usize> read_count = os::read_fd(SHIT_STDIN, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    contents.append(buffer, *read_count);
  }
  return contents;
}

Maybe<std::string>
read_line_from_fd(os::descriptor fd)
{
  std::string line{};
  bool read_any_byte = false;
  for (;;) {
    char one_byte = 0;
    Maybe<usize> read_count = os::read_fd(fd, &one_byte, 1);
    if (!read_count || *read_count == 0) break;
    read_any_byte = true;
    if (one_byte == '\n') return line;
    line += one_byte;
  }
  if (!read_any_byte) return None;
  return line;
}

} /* namespace utils */

} /* namespace shit */
