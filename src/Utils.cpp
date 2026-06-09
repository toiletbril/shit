#include "Utils.hpp"

#include "Builtin.hpp"
#include "Cli.hpp"
#include "Containers.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"

#include <cctype>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* TODO: Support background processes. */
/* TODO: Support setting environment variables. */

namespace shit {

namespace utils {

fn merge_tokens_to_string(const ArrayList<const Token *> &v) throws -> String
{
  String r{};
  for (const shit::Token *t : v) {
    ASSERT(t != nullptr);
    r += t->raw_string();
    if (t != v.back()) {
      r += ' ';
    }
  }
  return r;
}

fn execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async) throws
    -> i32
{
  if (!ec.is_builtin()) {
    /* The shell is about to exit with this command's status and it is the
       terminal external command of the run, so replace the shell process in
       place rather than fork, exec, and wait, the way dash execs the last
       command under EV_EXIT. A subshell or a background command keeps the fork,
       since its status does not become the shell's, and replace_process returns
       only when the exec itself fails, where it throws a located error. */
    /* An EXIT trap set earlier in this same chunk must still run, so the trap
       is rechecked here at run time rather than only when the chunk began. */
    if (!is_async && cxt.terminal_exec_allowed() && !cxt.in_subshell() &&
        !cxt.has_exit_trap())
    {
      LOG(verbosity::Debug,
          "execute_context replacing the shell with the terminal command '%s'",
          ec.program().c_str());
      /* The shell's buffered output lands before the descriptors move and the
         process is replaced. replace_process returns only by throwing, when the
         program resolved but could not be executed. The forked spawn this path
         replaces reports that case as a bare path and message on stderr and
         exits 127, so the in-place failure matches it rather than printing a
         located error or a goodbye. */
      flush();
      try {
        os::replace_process(steal(ec));
      } catch (const Error &error) {
        print_error(error.message() + "\n");
        quit(127, false);
      }
      unreachable();
    }

    /* The command word is kept for the job table before the context is moved
       into the spawn. */
    let const command = is_async ? String{ec.program().view()} : String{};

    let const p = os::execute_program(steal(ec));
    if (is_async) {
      cxt.set_last_background_pid(os::process_id_of(p));
      const i32 id = cxt.register_job(p, command);
      if (cxt.shell_is_interactive())
        shit::print_error("[" + int_to_text(id) + "] " +
                          uint_to_text(static_cast<u64>(os::process_id_of(p))) +
                          "\n");
      return 0;
    }

    return os::wait_and_monitor_process(p);
  }

  return execute_builtin(steal(ec), cxt);
}

fn execute_contexts_with_pipes(ArrayList<ExecContext> &&ecs, EvalContext &cxt,
                               bool is_async) throws -> i32
{
  ASSERT(ecs.count() > 1);

  i32 ret = 0;

  /* Every external stage is collected so all of them are reaped, not only the
     last. Otherwise a first stage like yes is left a zombie when the last stage
     exits. */
  ArrayList<os::process> children{};
  os::process last_child = SHIT_INVALID_PROCESS;
  os::descriptor last_stdin = SHIT_INVALID_FD;

  bool is_first = true;
  /* The pipeline status is the last stage's, builtin or external. A builtin
     stage runs in this process and yields its status at once, so the last
     stage's nature decides where the result comes from. Otherwise a builtin
     final stage, such as `false | read x`, would lose its status to the last
     external child the wait loop reaps. */
  bool last_stage_is_builtin = false;
  i32 last_builtin_status = 0;

  for (ExecContext &ec : ecs) {
    Maybe<os::Pipe> pipe;

    let const is_last = (&ec == &ecs.back());

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
      let const child = os::execute_program(steal(ec));
      children.push(child);
      last_child = child;
      if (is_last) last_stage_is_builtin = false;
    } else {
      /* A builtin runs in this process, so its status stands in for the stage.
       */
      ret = execute_builtin(steal(ec), cxt);
      if (is_last) {
        last_stage_is_builtin = true;
        last_builtin_status = ret;
      }
    }

    is_first = false;
  }

  if (is_async) {
    if (last_child != SHIT_INVALID_PROCESS) {
      cxt.set_last_background_pid(os::process_id_of(last_child));
      const i32 id = cxt.register_job(last_child, "pipeline");
      if (cxt.shell_is_interactive())
        shit::print_error(
            "[" + int_to_text(id) + "] " +
            uint_to_text(static_cast<u64>(os::process_id_of(last_child))) +
            "\n");
    }
    return ret;
  }

  /* Wait for every stage so none lingers as a zombie. The pipeline status is
     the last stage's. When that stage is an external child the wait that reaps
     it supplies the result, and when it is a builtin the status it already
     returned stands, so the external wait must not overwrite it. */
  for (const os::process child : children) {
    const i32 status = os::wait_and_monitor_process(child);
    if (!last_stage_is_builtin && child == last_child) ret = status;
  }

  if (last_stage_is_builtin) ret = last_builtin_status;

  return ret;
}

/* The offset of the first occurrence of needle at or after start, or
   NOT_FOUND_INDEX when no occurrence remains. The bytes carry no null
   terminator, so the match is a plain byte scan rather than a C string search.
 */
static pure fn find_subview(StringView haystack, StringView needle,
                            usize start) wontthrow -> usize
{
  if (needle.length == 0)
    return start <= haystack.length ? start : NOT_FOUND_INDEX;
  if (needle.length > haystack.length) return NOT_FOUND_INDEX;

  ASSERT(haystack.data != nullptr);
  ASSERT(needle.data != nullptr);

  for (usize i = start; i + needle.length <= haystack.length; i++) {
    if (std::memcmp(haystack.data + i, needle.data, needle.length) == 0)
      return i;
  }

  return NOT_FOUND_INDEX;
}

fn string_replace(String &s, const StringView to_replace,
                  const StringView replace_with) throws -> void
{
  String result{};
  result.reserve(s.count());

  let const source = s.view();
  usize i = 0;
  usize previous = 0;

  for (;;) {
    previous = i;
    const usize match = find_subview(source, to_replace, i);
    if (match == NOT_FOUND_INDEX) break;

    ASSERT(match >= previous, "match cannot precede the search start");
    result.append(source.substring_of_length(previous, match - previous));
    result.append(replace_with);

    i = match + to_replace.length;
  }

  result.append(source.substring(previous));
  s = steal(result);
}

fn lowercase_string(StringView s) throws -> String
{
  String l{};
  l.reserve(s.count());
  for (usize i = 0; i < s.count(); i++)
    l.push(static_cast<char>(std::tolower(s[i])));
  return l;
}

pure fn is_posix_reserved_word(StringView word) wontthrow -> bool
{
  static const StringView RESERVED_WORDS[] = {
      "!",    "{",  "}",   "case", "do", "done", "elif",  "else",
      "esac", "fi", "for", "if",   "in", "then", "until", "while",
  };
  for (const StringView reserved : RESERVED_WORDS)
    if (word == reserved) return true;
  return false;
}

static pure fn is_ascii_whitespace(char c) wontthrow -> bool
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}

/* Turn an accumulated magnitude and sign into a saturating signed result. The
   per-base parsers share this so only the digit loop stays base-specific. */
static pure fn saturate_signed_magnitude(u64 magnitude, bool is_negative,
                                         bool has_overflowed) wontthrow -> i64
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

static fn not_an_integer_error(StringView text) throws -> Error
{
  return Error{"'" + text + "' is not a valid integer"};
}

fn uint_to_text(u64 value) throws -> String
{
  /* The digits are written into a fixed buffer from the least significant end,
     since a u64 never needs more than twenty decimal digits, then copied out in
     order. No allocation happens until the result String is built. */
  char buffer[20];
  usize offset = sizeof(buffer);
  do {
    ASSERT(offset > 0, "decimal digits cannot exceed the buffer");
    buffer[--offset] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value > 0);
  return String{
      StringView{buffer + offset, sizeof(buffer) - offset}
  };
}

fn int_to_text(i64 value) throws -> String
{
  if (value >= 0) return uint_to_text(static_cast<u64>(value));
  /* Negating in u64 avoids the overflow that -INT64_MIN would hit in i64. */
  const u64 magnitude = ~static_cast<u64>(value) + 1;
  String result{"-"};
  result.append(uint_to_text(magnitude));
  return result;
}

fn format_minutes_seconds(double seconds) throws -> String
{
  /* A time report subtracts two child rusage samples, so a sample that goes
     backwards yields a small negative duration. bash never prints a negative
     time, so a negative input clamps to zero rather than printing a doubled sign
     like -0m-0.001s, which the separate minutes and remainder would otherwise
     produce. */
  if (seconds < 0.0) seconds = 0.0;
  const i64 minutes = static_cast<i64>(seconds) / 60;
  const double remainder = seconds - static_cast<double>(minutes * 60);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%ldm%.3fs", static_cast<long>(minutes),
                remainder);
  return String{buffer};
}

/* A newline offset table cached on one source, so the line lookup is a binary
   search over the newlines rather than a scan of the prefix. The shell reads
   $LINENO against a single script source at a time, so one cached entry keyed
   on the source pointer and length serves every read in that script. */
class LineNumberCache
{
public:
  LineNumberCache() : m_newline_offsets(heap_allocator()) {}

  /* Build the newline table for this source when it differs from the cached
     one, so a repeated read against the same script reuses the table. */
  fn ensure_built_for(StringView source) throws -> void
  {
    if (m_source_data == source.data && m_source_length == source.count())
      return;

    m_source_data = source.data;
    m_source_length = source.count();
    m_newline_offsets.clear();

    for (usize i = 0; i < source.count(); i++)
      if (source[i] == '\n') m_newline_offsets.push(i);
  }

  fn invalidate() wontthrow -> void
  {
    m_source_data = nullptr;
    m_source_length = 0;
    m_newline_offsets.clear();
  }

  /* The count of newlines at a byte offset strictly less than the position. */
  pure fn newlines_before(usize position) const wontthrow -> usize
  {
    usize low = 0;
    usize high = m_newline_offsets.count();
    while (low < high) {
      const usize mid = low + (high - low) / 2;
      if (m_newline_offsets[mid] < position)
        low = mid + 1;
      else
        high = mid;
    }
    return low;
  }

private:
  const char *m_source_data{nullptr};
  usize m_source_length{0};
  ArrayList<usize> m_newline_offsets;
};

static LineNumberCache LINE_NUMBER_CACHE{};

fn line_number_at(StringView source, usize position) throws -> usize
{
  LINE_NUMBER_CACHE.ensure_built_for(source);
  /* The first line is line 1, and each newline strictly before the byte starts
     a new line, so the line number is one more than the newline count. */
  return LINE_NUMBER_CACHE.newlines_before(position) + 1;
}

fn invalidate_line_number_cache() wontthrow -> void
{
  LINE_NUMBER_CACHE.invalidate();
}

fn parse_decimal_integer(StringView text) throws -> ErrorOr<i64>
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
    const u64 digit = static_cast<u64>(text.data[offset] - '0');
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

fn parse_octal_integer(StringView text) throws -> ErrorOr<i64>
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
    const u64 digit = static_cast<u64>(text.data[offset] - '0');
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

fn parse_hexadecimal_integer(StringView text) throws -> ErrorOr<i64>
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
    const char current = text.data[offset];
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

fn find_pos_in_vec(const ArrayList<String> &suffixes,
                   StringView wanted) wontthrow -> usize
{
  for (usize i = 0; i < suffixes.count(); i++) {
    if (suffixes[i] == wanted) return i;
  }
  return NOT_FOUND_INDEX;
}

fn canonicalize_path(StringView path) throws -> Maybe<Path>
{
  Path candidate{path};

  if (candidate.is_relative() && path.find_character('/').has_value()) {
    candidate = candidate.to_absolute();
  }

  candidate = candidate.normalized();

  /* If there's no extension, we may have to add it ourselves. The ending dot is
     stripped by the path normalization, so a name written with a trailing dot
     is left as typed. */
  const bool ends_with_dot =
      path.length > 0 && path.data[path.length - 1] == '.';
  if (candidate.extension().is_empty() && !ends_with_dot) {
    usize suffix_index = 0;
    while (!candidate.exists() && suffix_index < os::OMITTED_SUFFIXES.count()) {
      const String &suffix = os::OMITTED_SUFFIXES[suffix_index++];
      candidate = candidate.with_extension(suffix.view());
    }
  }

  if (!candidate.exists()) return shit::None;

  return candidate;
}

/* Inspiration taken from https://github.com/tsoding/glob.h :3
 * This fragment is under MIT License (c) Alexey Kutepov <reximkut@gmail.com> */
static pure fn is_glob_char_active(const ArrayList<bool> &glob_active,
                                   usize index) wontthrow -> bool
{
  return index < glob_active.count() && glob_active[index];
}

fn glob_matches(StringView glob, StringView str,
                const ArrayList<bool> &glob_active, usize mask_offset) throws
    -> bool
{
  usize s = 0;
  usize g = 0;

  while (g < glob.count() && s < str.count()) {
    ASSERT(g < glob.count() && s < str.count());

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
      if (g + 1 >= glob.count()) return true;
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

      /* A bracket member, a class terminator ], a negating ! or ^, and a range
         '-' carry their special meaning only when the byte is an active glob
         character. A quoted or escaped ] inside the class is a literal member,
         not the terminator, and a quoted member byte never opens a range, so the
         scan consults the same per-byte mask the rest of the matcher reads. */
      let const is_active = [&](usize index) wontthrow -> bool {
        return is_glob_char_active(glob_active, mask_offset + index);
      };
      let const is_close_at = [&](usize index) wontthrow -> bool {
        return glob[index] == ']' && is_active(index);
      };

      /* The unsigned value of a byte, so a high byte at or above 0x80 compares
         as itself rather than as a negative char in the range and equality
         tests. */
      let const byte_at = [](StringView view, usize index) wontthrow -> u8 {
        return static_cast<u8>(view[index]);
      };

      /* A bracket with no closing ] is not a character class, so the [ is a
         literal character, as POSIX specifies. A ] right after [ or [^ is a
         member, so the scan for the closing ] starts past it. */
      usize close_scan = g + 1;
      if (close_scan < glob.count() &&
          (glob[close_scan] == '!' || glob[close_scan] == '^') &&
          is_active(close_scan))
        close_scan++;
      if (close_scan < glob.count() && is_close_at(close_scan)) close_scan++;
      bool has_closing_bracket = false;
      for (; close_scan < glob.count(); close_scan++) {
        if (is_close_at(close_scan)) {
          has_closing_bracket = true;
          break;
        }
      }
      if (!has_closing_bracket) {
        if (byte_at(glob, g) != byte_at(str, s)) return false;
        g++;
        s++;
        break;
      }

      g++; /* skip [ */
      if (g >= glob.count()) GLOB_GROUP_ERR();

      /* POSIX sh negates a class with a leading '!'. The '^' form is kept as a
         common extension. The negation applies only to an active byte, so a
         quoted ! or ^ at the front is a literal member. */
      if ((glob[g] == '!' || glob[g] == '^') && is_active(g)) {
        g++;
        should_negate = true;

        if (g >= glob.count()) GLOB_GROUP_ERR();
      }

      u8 prev_glob_ch = byte_at(glob, g);
      is_matched |= (prev_glob_ch == byte_at(str, s));
      g++;

      while (g < glob.count() && !is_close_at(g)) {
        if (glob[g] == '-' && is_active(g)) {
          g++;
          if (g >= glob.count()) GLOB_GROUP_ERR();

          if (is_close_at(g)) {
            is_matched |= ('-' == byte_at(str, s));
          } else {
            is_matched |=
                (prev_glob_ch <= byte_at(str, s) && byte_at(str, s) <= byte_at(glob, g));
            prev_glob_ch = byte_at(glob, g);
            g++;
          }
        } else {
          prev_glob_ch = byte_at(glob, g);
          is_matched |= (prev_glob_ch == byte_at(str, s));
          g++;
        }
      }

      if (g >= glob.count() || !is_close_at(g)) GLOB_GROUP_ERR();
      if (should_negate) is_matched = !is_matched;
      if (!is_matched) return false;

      g++;
      s++;
    } break;

    default:
      if (glob[g++] != str[s++]) return false;
    }
  }

  if (s >= str.count()) {
    while (g < glob.count() && glob[g] == '*' &&
           is_glob_char_active(glob_active, mask_offset + g))
    {
      g++;
    }

    if (g >= glob.count()) return true;
  }

  return false;
}

/* The shell is at a real interactive prompt. quit gates the goodbye on this so
   a script, a -c, or a subshell exits silently the way dash does. */
static bool SHELL_IS_INTERACTIVE = false;

fn set_shell_is_interactive(bool is_interactive) wontthrow -> void
{
  SHELL_IS_INTERACTIVE = is_interactive;
}

[[noreturn]] fn quit(i32 code, bool should_goodbye) throws -> void
{
  const u8 actual_code = static_cast<u8>(code);

  if (!os::is_child_process()) {
    if (toiletline::is_active()) {
      try {
        toiletline::exit();
      } catch (const Error &e) {
        /* TODO: A wild bug appeared! */
        show_message(e.to_string());
      }
    }

    if (should_goodbye && SHELL_IS_INTERACTIVE) {
      String code_str{};
      if (code != 0) {
        code_str += " (Code ";
        code_str += uint_to_text(actual_code);
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
static HashMap<ArrayList<Path>> PATH_CACHE{heap_allocator()};

/* A cd, a PATH assignment, and hash -r set this so the next lookup drops the
   cache and re-resolves, the way dash rehashes lazily. While it is false a hit
   returns the stored path with no stat. */
static bool PATH_CACHE_IS_STALE = false;

static Maybe<String> MAYBE_PATH = os::get_environment_variable("PATH");

/* Append one resolved absolute path under a program name, creating the list on
   the first hit. */
static fn cache_resolved_path(StringView name, const Path &full_path) throws
    -> void
{
  PATH_CACHE.get_or_create(name, ArrayList<Path>{}).push(full_path);
}

fn clear_path_map() throws -> void
{
  LOG(verbosity::Debug,
      "clear_path_map dropping %zu cached program resolutions",
      PATH_CACHE.count());
  MAYBE_PATH = os::get_environment_variable("PATH");
  PATH_CACHE.clear();
  PATH_CACHE_IS_STALE = false;
}

fn invalidate_path_cache() throws -> void
{
  /* The cache is not cleared here, since a cd or a PATH change is followed by
     few lookups in a script. The stale flag defers the clear to the next lookup
     so a run that never resolves a command again pays nothing. */
  LOG(verbosity::Debug,
      "invalidate_path_cache marking the program cache stale");
  PATH_CACHE_IS_STALE = true;
}

fn set_path_for_resolution(Maybe<String> path) throws -> void
{
  LOG(verbosity::Debug,
      "set_path_for_resolution pointing the search at the shell's PATH value");
  MAYBE_PATH = steal(path);
  PATH_CACHE_IS_STALE = true;
}

/* Split PATH into its directory components. The last component carries no
   trailing delimiter, so a plain delimiter scan drops it and the directory is
   never searched. POSIX treats an empty component as the current directory. */
static fn split_path_dirs(StringView path_var) throws -> ArrayList<String>
{
  ArrayList<String> dirs{};
  String current{};

  for (usize i = 0; i < path_var.length; i++) {
    const char ch = path_var.data[i];
    if (ch == os::PATH_DELIMITER) {
      dirs.push(current.is_empty() ? String{"."} : current);
      current.clear();
    } else {
      current.push(ch);
    }
  }
  dirs.push(current.is_empty() ? String{"."} : current);

  return dirs;
}

fn initialize_path_map() throws -> void
{
  if (!MAYBE_PATH) return;

  for (const String &dir_string : split_path_dirs(*MAYBE_PATH)) {
    const Path directory{dir_string.view()};

    /* read_directory returns None for a missing or unreadable directory, so the
       path is skipped without a separate exists check. */
    let const entries = Path::read_directory(directory);
    if (!entries) continue;

    /* Cache every file in the directory under its name without an omitted
       extension, pointing at its full path. */
    for (const String &entry_name : *entries) {
      String name{entry_name};
      os::erase_extension_and_get_its_index(name);

      let full_path = directory;
      full_path.push_component(entry_name.view());
      cache_resolved_path(name.view(), full_path);
    }
  }
}

/* Stat dir/name along PATH until a match, the way dash advances PATH and stats
   each candidate once. The first hit ends the scan and is cached, so a cold
   miss costs at most one stat per PATH directory up to the match rather than a
   full directory read. With find_all the scan does not stop and collects every
   match for which -a, and it does not write the cache, since a partial
   single-result entry would later hide the other matches from which -a. */
static fn resolve_along_path(StringView program_name, bool find_all) throws
    -> ArrayList<Path>
{
  /* The search reads MAYBE_PATH, which the shell keeps in step with its PATH
     variable, so a plain PATH=... assignment that the store holds but the
     environment does not still drives the order. */
  if (!MAYBE_PATH) return ArrayList<Path>{};

  ArrayList<Path> result{};

  /* The cache key is the program name without an omitted extension, the same
     key the lookup uses. */
  String key{program_name};
  os::erase_extension_and_get_its_index(key);

  for (const String &dir_string : split_path_dirs(*MAYBE_PATH)) {
    const Path directory{dir_string.view()};

    let full_path = directory;
    full_path.push_component(program_name);
    String full_path_str{full_path.text()};

    /* This file already has an extesion specified? */
    if (os::ext_index explicit_ext =
            os::erase_extension_and_get_its_index(full_path_str);
        explicit_ext == 0)
    {
      for (usize ext_index = 0; ext_index < os::OMITTED_SUFFIXES.count();
           ext_index++)
      {
        const String &suffix = os::OMITTED_SUFFIXES[ext_index];
        const Path try_path{(full_path.text() + suffix.view()).view()};

        if (try_path.exists()) {
          result.push(try_path);
          if (!find_all) {
            cache_resolved_path(key.view(), try_path);
            return result;
          }
        }
      }
    } else if (full_path.exists()) {
      result.push(full_path);
      if (!find_all) {
        cache_resolved_path(key.view(), full_path);
        return result;
      }
    }
  }

  return result;
}

hot fn search_program_path(StringView program_name, bool find_all) throws
    -> ArrayList<Path>
{
  /* A cd, a PATH change, or hash -r left the cache stale, so it is dropped here
     before the lookup re-resolves against the current filesystem. */
  if (PATH_CACHE_IS_STALE) {
    LOG(verbosity::Debug,
        "search_program_path clearing stale cache before resolving '%.*s'",
        (int) program_name.length, program_name.data);
    PATH_CACHE.clear();
    PATH_CACHE_IS_STALE = false;
  }

  String sp{program_name};

  const os::ext_index typed_extension =
      os::erase_extension_and_get_its_index(sp);

  /* which -a wants every match, so it skips the cache and scans PATH in full.
   */
  if (find_all) return resolve_along_path(program_name, true);

  /* A name typed with an explicit extension is matched exactly by the search,
     so the extension-stripped cache key would resolve the wrong file. The cache
     is consulted only when no extension was typed, which on POSIX is always. A
     hit returns the stored absolute path with no stat, the way dash returns a
     hashed location. */
  if (typed_extension == 0) {
    if (const ArrayList<Path> *const cached = PATH_CACHE.find(sp.view());
        cached != nullptr && cached->count() != 0)
    {
      ArrayList<Path> result{};
      result.push((*cached)[0]);
      return result;
    }
  }

  return resolve_along_path(program_name, false);
}

fn read_entire_file(StringView path) throws -> Maybe<String>
{
  let const file = os::open_file_descriptor(path, os::file_open_mode::Read);
  if (!file) return None;

  String contents{};
  char buffer[4096];
  for (;;) {
    Maybe<usize> read_count = os::read_fd(*file, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    contents.append(StringView{buffer, *read_count});
  }

  os::close_fd(*file);

  return contents;
}

fn read_entire_standard_input() throws -> String
{
  String contents{};
  char buffer[4096];
  for (;;) {
    Maybe<usize> read_count = os::read_fd(SHIT_STDIN, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    contents.append(StringView{buffer, *read_count});
  }
  return contents;
}

fn read_line_from_fd(os::descriptor fd, bool &was_newline_terminated) throws
    -> Maybe<String>
{
  String line{};
  bool read_any_byte = false;
  for (;;) {
    u8 one_byte = 0;
    Maybe<usize> read_count = os::read_fd(fd, &one_byte, 1);
    if (!read_count || *read_count == 0) break;
    read_any_byte = true;
    if (one_byte == '\n') {
      was_newline_terminated = true;
      return line;
    }
    line.push(one_byte);
  }

  /* The loop fell out at end of input, so no newline ended the line. The read
     builtin maps an unterminated final line to a non-zero status while still
     assigning the bytes it read, the way dash does. */
  was_newline_terminated = false;

  if (!read_any_byte) return None;

  return line;
}

} /* namespace utils */

} /* namespace shit */
