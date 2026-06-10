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

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace shit {

namespace utils {

fn merge_tokens_to_string(const ArrayList<const Token *> &v) throws -> String
{
  let r = String{};
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
    /* Mimicry runs a shell script in-process in the matching mode instead of
       launching the shell. A background command keeps its fork, since an
       in-process subshell cannot run in the background. */
    if (cxt.mimicry() && !is_async) {
      if (Maybe<MimicMode> mode = detect_mimic_shell(ec.program_path());
          mode.has_value())
      {
        LOG(verbosity::Debug, "execute_context mimicking the shell for '%s'",
            ec.program().c_str());
        /* The terminal command the shell exits with needs no isolation, the same
           condition the replace path below uses, so its run skips the snapshot. */
        let const isolated = !(cxt.terminal_exec_allowed() &&
                               !cxt.in_subshell() && !cxt.has_exit_trap());
        return cxt.run_mimicked_script(ec, *mode, isolated);
      }
    }

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
  let children = ArrayList<os::process>{};
  os::process last_child = SHIT_INVALID_PROCESS;
  os::descriptor last_stdin = SHIT_INVALID_FD;

  /* Each stage's status is recorded against its position, so pipefail can report
     the rightmost stage that failed and the plain case can read the last stage.
     A builtin stage yields its status at once and an external one's status
     arrives from the wait below, tracked by the parallel child-to-stage list. */
  let const stage_count = ecs.count();
  let stage_status = ArrayList<i32>{};
  stage_status.reserve(stage_count);
  for (usize i = 0; i < stage_count; i++)
    stage_status.push(0);
  let child_stage = ArrayList<usize>{};

  bool is_first = true;
  usize stage_index = 0;

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
      child_stage.push(stage_index);
      last_child = child;
    } else {
      /* A builtin runs in this process, so its status stands in for the stage.
       */
      ret = execute_builtin(steal(ec), cxt);
      stage_status[stage_index] = ret;
    }

    is_first = false;
    stage_index++;
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

  /* Wait for every stage so none lingers as a zombie, recording each external
     stage's status against its position. */
  for (usize i = 0; i < children.count(); i++)
    stage_status[child_stage[i]] = os::wait_and_monitor_process(children[i]);

  /* pipefail reports the rightmost stage that failed, or zero when every stage
     succeeded. Otherwise the pipeline reports the last stage alone. */
  if (cxt.pipefail()) {
    for (usize i = stage_count; i > 0; i--)
      if (stage_status[i - 1] != 0)
        return stage_status[i - 1];
    return 0;
  }

  return stage_status[stage_count - 1];
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
  let result = String{};
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
  let l = String{};
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

fn uint_to_text(u64 value, Allocator allocator) throws -> String
{
  /* The digits are written into a fixed buffer from the least significant end,
     since a u64 never needs more than twenty decimal digits, then copied out in
     order. The result String draws from the caller's allocator, so a transient
     conversion comes from the scratch arena rather than the heap. */
  char buffer[20];
  usize offset = sizeof(buffer);
  do {
    ASSERT(offset > 0, "decimal digits cannot exceed the buffer");
    buffer[--offset] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value > 0);
  return String{
      allocator, StringView{buffer + offset, sizeof(buffer) - offset}
  };
}

fn int_to_text(i64 value, Allocator allocator) throws -> String
{
  if (value >= 0) return uint_to_text(static_cast<u64>(value), allocator);
  /* Negating in u64 avoids the overflow that -INT64_MIN would hit in i64. */
  const u64 magnitude = ~static_cast<u64>(value) + 1;
  let result = String{allocator, "-"};
  result.append(uint_to_text(magnitude, allocator));
  return result;
}

fn int_to_text_into(i64 value, char *buffer, usize buffer_size) wontthrow
    -> StringView
{
  /* The digits are written from the least significant end of the buffer, the
     same scheme uint_to_text uses, then a leading minus is prepended. A u64
     never needs more than twenty digits, so twenty-one bytes hold any i64. */
  ASSERT(buffer_size >= 21, "the buffer must hold a sign and twenty digits");
  const bool is_negative = value < 0;
  u64 magnitude =
      is_negative ? ~static_cast<u64>(value) + 1 : static_cast<u64>(value);
  usize offset = buffer_size;
  do {
    buffer[--offset] = static_cast<char>('0' + magnitude % 10);
    magnitude /= 10;
  } while (magnitude > 0);
  if (is_negative) buffer[--offset] = '-';
  return StringView{buffer + offset, buffer_size - offset};
}

fn format_minutes_seconds(double seconds) throws -> String
{
  /* A time report subtracts two child rusage samples, so a sample that goes
     backwards yields a small negative duration. bash never prints a negative
     time, so a negative input clamps to zero rather than printing a doubled
     sign like -0m-0.001s, which the separate minutes and remainder would
     otherwise produce. */
  if (seconds < 0.0) seconds = 0.0;
  const i64 minutes = static_cast<i64>(seconds) / 60;
  const double remainder = seconds - static_cast<double>(minutes * 60);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%ldm%.3fs", static_cast<long>(minutes),
                remainder);
  return String{buffer};
}

fn format_time_report_posix(double real_seconds, double user_seconds,
                            double system_seconds) throws -> String
{
  char buffer[64];
  let report = String{};
  std::snprintf(buffer, sizeof(buffer), "real %.2f\n",
                real_seconds < 0.0 ? 0.0 : real_seconds);
  report += buffer;
  std::snprintf(buffer, sizeof(buffer), "user %.2f\n",
                user_seconds < 0.0 ? 0.0 : user_seconds);
  report += buffer;
  std::snprintf(buffer, sizeof(buffer), "sys %.2f\n",
                system_seconds < 0.0 ? 0.0 : system_seconds);
  report += buffer;
  return report;
}

fn format_time_report_pretty(double real_seconds, double user_seconds,
                             double system_seconds) throws -> String
{
  /* The cpu busy percent, the share of the wall time the user and system cpu
     together account for, the way the bench summary reports it. */
  const double cpu_percent =
      real_seconds > 0.0
          ? (user_seconds + system_seconds) / real_seconds * 100.0
          : 0.0;
  char buffer[64];
  let report = String{};
  report += "\n";
  report += "  real   " + format_minutes_seconds(real_seconds) + "\n";
  report += "  user   " + format_minutes_seconds(user_seconds) + "\n";
  report += "  sys    " + format_minutes_seconds(system_seconds) + "\n";
  std::snprintf(buffer, sizeof(buffer), "  cpu    %.0f%%\n", cpu_percent);
  report += buffer;
  return report;
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

/* Advance offset past any leading ASCII whitespace, shared by the integer
   parsers which trim before and after the digit run. */
static fn skip_ascii_whitespace(StringView text, usize &offset) wontthrow -> void
{
  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;
}

pure fn split_name_value_arg(StringView arg) wontthrow -> name_value_arg
{
  let const equals = arg.find_character('=');
  if (!equals.has_value()) return name_value_arg{arg, None};
  return name_value_arg{arg.substring_of_length(0, *equals),
                        arg.substring(*equals + 1)};
}

fn parse_decimal_integer(StringView text) throws -> ErrorOr<i64>
{
  usize offset = 0;
  skip_ascii_whitespace(text, offset);

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

  skip_ascii_whitespace(text, offset);
  if (!has_digits || offset != text.length) return not_an_integer_error(text);
  return saturate_signed_magnitude(magnitude, is_negative, has_overflowed);
}

fn parse_octal_integer(StringView text) throws -> ErrorOr<i64>
{
  usize offset = 0;
  skip_ascii_whitespace(text, offset);

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

  skip_ascii_whitespace(text, offset);
  if (!has_digits || offset != text.length) return not_an_integer_error(text);
  return saturate_signed_magnitude(magnitude, is_negative, has_overflowed);
}

fn parse_hexadecimal_integer(StringView text) throws -> ErrorOr<i64>
{
  usize offset = 0;
  skip_ascii_whitespace(text, offset);

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

  skip_ascii_whitespace(text, offset);
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
  let candidate = Path{path};

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

namespace {

/* One alternative of a bash extended-glob group, a slice of the glob and the
   mask offset that slice begins at, so the recursive matcher reads the same
   per-byte activity. */
struct extglob_alternative
{
  StringView pattern;
  usize mask_offset;
};

hot fn extglob_active(const ArrayList<bool> &mask, usize index) wontthrow
    -> bool
{
  return index < mask.count() ? mask[index] : true;
}

/* True when glob at index opens an extended-glob group, one of ?, *, +, @, or !
   immediately followed by (. The caller has opted into extglob, so the group
   structure is read from the text rather than the metacharacter mask, which
   only distinguishes a leaf star or bracket from a quoted literal. */
fn extglob_opens_group(StringView glob, usize index) wontthrow -> bool
{
  if (index + 1 >= glob.count()) return false;
  const char op = glob[index];
  if (op != '?' && op != '*' && op != '+' && op != '@' && op != '!')
    return false;
  return glob[index + 1] == '(';
}

/* The index of the ) that closes the group whose ( sits at glob[1], tracking
   nested groups by text. Returns glob.count() when the group is unbalanced. */
fn extglob_group_close(StringView glob) wontthrow -> usize
{
  usize depth = 0;
  for (usize i = 1; i < glob.count(); i++) {
    if (glob[i] == '(')
      depth++;
    else if (glob[i] == ')') {
      depth--;
      if (depth == 0) return i;
    }
  }
  return glob.count();
}

fn extglob_full_match(StringView glob, StringView str,
                      const ArrayList<bool> &mask, usize mask_offset) throws
    -> bool;

/* Match min_reps or more repetitions of one of the alternatives against the
   front of str, then the suffix against the rest. The min drops to zero after
   the first repetition, so a + needs one and a * needs none. */
fn extglob_match_repetition(const ArrayList<extglob_alternative> &alternatives,
                            StringView suffix, usize suffix_offset,
                            StringView str, const ArrayList<bool> &mask,
                            usize min_reps) throws -> bool
{
  if (min_reps == 0 && extglob_full_match(suffix, str, mask, suffix_offset))
    return true;
  for (const extglob_alternative &alternative : alternatives) {
    for (usize length = 1; length <= str.count(); length++) {
      if (!extglob_full_match(alternative.pattern,
                              str.substring_of_length(0, length), mask,
                              alternative.mask_offset))
        continue;
      const usize next_min = min_reps > 0 ? min_reps - 1 : 0;
      if (extglob_match_repetition(alternatives, suffix, suffix_offset,
                                   str.substring(length), mask, next_min))
        return true;
    }
  }
  return false;
}

fn extglob_full_match(StringView glob, StringView str,
                      const ArrayList<bool> &mask, usize mask_offset) throws
    -> bool
{
  if (glob.is_empty()) return str.is_empty();

  const bool active = extglob_active(mask, mask_offset);
  const char head = glob[0];

  /* An extended-glob group such as @(a|b), *(a|b), or !(a) drives the match
     through the alternatives split on the top-level |. */
  if (extglob_opens_group(glob, 0)) {
    const usize close = extglob_group_close(glob);
    if (close < glob.count()) {
      const StringView content = glob.substring_of_length(2, close - 2);
      const usize content_offset = mask_offset + 2;
      const StringView suffix = glob.substring(close + 1);
      const usize suffix_offset = mask_offset + close + 1;

      let alternatives = ArrayList<extglob_alternative>{heap_allocator()};
      usize depth = 0;
      usize start = 0;
      for (usize i = 0; i <= content.count(); i++) {
        const bool boundary =
            i == content.count() || (content[i] == '|' && depth == 0);
        if (boundary) {
          alternatives.push({content.substring_of_length(start, i - start),
                             content_offset + start});
          start = i + 1;
        } else if (content[i] == '(')
          depth++;
        else if (content[i] == ')')
          depth--;
      }

      switch (head) {
      case '*':
        return extglob_match_repetition(alternatives, suffix, suffix_offset,
                                        str, mask, 0);
      case '+':
        return extglob_match_repetition(alternatives, suffix, suffix_offset,
                                        str, mask, 1);
      case '?':
      case '@':
        for (const extglob_alternative &alternative : alternatives) {
          for (usize length = head == '?' ? 0 : 1; length <= str.count();
               length++)
          {
            if (extglob_full_match(alternative.pattern,
                                   str.substring_of_length(0, length), mask,
                                   alternative.mask_offset) &&
                extglob_full_match(suffix, str.substring(length), mask,
                                   suffix_offset))
              return true;
          }
        }
        /* A ? group also matches zero occurrences, so the suffix may follow
           with nothing consumed. */
        return head == '?' &&
               extglob_full_match(suffix, str, mask, suffix_offset);
      case '!':
        /* A negated group consumes a prefix that none of the alternatives
           match, then the suffix matches the rest. */
        for (usize length = 0; length <= str.count(); length++) {
          bool any_alternative_matches = false;
          for (const extglob_alternative &alternative : alternatives) {
            if (extglob_full_match(alternative.pattern,
                                   str.substring_of_length(0, length), mask,
                                   alternative.mask_offset))
            {
              any_alternative_matches = true;
              break;
            }
          }
          if (!any_alternative_matches &&
              extglob_full_match(suffix, str.substring(length), mask,
                                 suffix_offset))
            return true;
        }
        return false;
      default: break;
      }
    }
  }

  /* A trailing * matches the rest of the string, so it is taken without trying
     every split. */
  if (active && head == '*') {
    for (usize eaten = 0; eaten <= str.count(); eaten++) {
      if (extglob_full_match(glob.substring(1), str.substring(eaten), mask,
                             mask_offset + 1))
        return true;
    }
    return false;
  }

  if (str.is_empty()) return false;

  if (active && head == '?')
    return extglob_full_match(glob.substring(1), str.substring(1), mask,
                              mask_offset + 1);

  if (active && head == '[') {
    /* Reuse the iterative matcher for a single bracket class by matching one
       character, then continue with the rest of the glob and the string. */
    usize span = 1;
    while (span < glob.count() &&
           !(glob[span] == ']' && extglob_active(mask, mask_offset + span)))
      span++;
    if (span < glob.count()) {
      span++; /* past the ] */
      const bool class_matched =
          glob_matches(glob.substring_of_length(0, span),
                       str.substring_of_length(0, 1), mask, mask_offset);
      if (!class_matched) return false;
      return extglob_full_match(glob.substring(span), str.substring(1), mask,
                                mask_offset + span);
    }
  }

  if (str[0] != head) return false;
  return extglob_full_match(glob.substring(1), str.substring(1), mask,
                            mask_offset + 1);
}

} /* namespace */

fn glob_matches(StringView glob, StringView str,
                const ArrayList<bool> &glob_active, usize mask_offset,
                bool extglob) throws -> bool
{
  /* The extended-glob grammar needs backtracking over alternatives and
     repetition, so it runs in a separate recursive matcher. It is taken only
     when extglob is on and the pattern actually holds a group, so a plain glob
     keeps the iterative matcher below, unchanged, and pays nothing. */
  if (extglob) {
    for (usize i = 0; i + 1 < glob.count(); i++) {
      const char c = glob[i];
      if ((c == '?' || c == '*' || c == '+' || c == '@' || c == '!') &&
          glob[i + 1] == '(')
        return extglob_full_match(glob, str, glob_active, mask_offset);
    }
  }

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
         not the terminator, and a quoted member byte never opens a range, so
         the scan consults the same per-byte mask the rest of the matcher reads.
       */
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
            is_matched |= (prev_glob_ch <= byte_at(str, s) &&
                           byte_at(str, s) <= byte_at(glob, g));
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
/* The one context quit reads the interactive state and the memory-report flag
   from. A pointer rather than mirrored globals keeps the state on the context,
   and a null pointer, the state before the context exists, reads as a
   non-interactive shell with the report off. */
static const EvalContext *QUIT_CONTEXT = nullptr;

fn set_quit_context(const EvalContext *context) wontthrow -> void
{
  QUIT_CONTEXT = context;
}

/* The granular memory report, the live bump bytes and the reserved capacity of
   each arena, then the malloc heap in use. The arena capacity counts the blocks
   the bump allocator holds, while the heap figure counts the String buffers and
   other long-lived allocations the arenas do not own. */
cold fn print_memory_report() wontthrow -> void
{
  if (AST_ARENA != nullptr)
    std::fprintf(stderr, "AST arena: used %zu, reserved %zu, blocks %zu\n",
                 AST_ARENA->bytes_used(), AST_ARENA->bytes_capacity(),
                 AST_ARENA->block_count());
  if (FUNCTION_ARENA != nullptr)
    std::fprintf(stderr, "Function arena: used %zu, reserved %zu, blocks %zu\n",
                 FUNCTION_ARENA->bytes_used(), FUNCTION_ARENA->bytes_capacity(),
                 FUNCTION_ARENA->block_count());
#if defined(__GLIBC__)
  const struct mallinfo2 info = mallinfo2();
  std::fprintf(stderr,
               "Malloc heap: in use %zu, total arena %zu, mmapped %zu\n",
               static_cast<usize>(info.uordblks),
               static_cast<usize>(info.arena), static_cast<usize>(info.hblkhd));
#endif
}

[[noreturn]] fn quit(i32 code, bool should_goodbye) throws -> void
{
  if (QUIT_CONTEXT != nullptr && QUIT_CONTEXT->memory_stats_enabled())
    print_memory_report();

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

    if (should_goodbye && QUIT_CONTEXT != nullptr &&
        QUIT_CONTEXT->shell_is_interactive())
    {
      let code_str = String{};
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
  let dirs = ArrayList<String>{};
  let current = String{};

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
    let const directory = Path{dir_string.view()};

    /* read_directory returns None for a missing or unreadable directory, so the
       path is skipped without a separate exists check. */
    let const entries = Path::read_directory(directory);
    if (!entries) continue;

    /* Cache every file in the directory under its name without an omitted
       extension, pointing at its full path. */
    for (const String &entry_name : *entries) {
      let name = entry_name.clone();
      os::erase_extension_and_get_its_index(name);

      let full_path = directory.clone();
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

  let result = ArrayList<Path>{};

  /* The cache key is the program name without an omitted extension, the same
     key the lookup uses. */
  let key = String{program_name};
  os::erase_extension_and_get_its_index(key);

  for (const String &dir_string : split_path_dirs(*MAYBE_PATH)) {
    let const directory = Path{dir_string.view()};

    let full_path = directory.clone();
    full_path.push_component(program_name);
    let full_path_str = full_path.text().clone();

    /* This file already has an extesion specified? */
    if (os::ext_index explicit_ext =
            os::erase_extension_and_get_its_index(full_path_str);
        explicit_ext == 0)
    {
      for (usize ext_index = 0; ext_index < os::OMITTED_SUFFIXES.count();
           ext_index++)
      {
        const String &suffix = os::OMITTED_SUFFIXES[ext_index];
        let const try_path = Path{(full_path.text() + suffix.view()).view()};

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

  let sp = String{program_name};

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
      let result = ArrayList<Path>{};
      result.push((*cached)[0]);
      return result;
    }
  }

  return resolve_along_path(program_name, false);
}

/* The rolling distance rows are fixed-width stack arrays, so a candidate name
   longer than this is treated as too far rather than indexed past the row. */
constexpr usize OSA_ROW_WIDTH = 256;

/* The optimal-string-alignment distance, the edit distance that also counts an
   adjacent transposition as one edit, so a typo such as gti for git scores one
   rather than two. Bounded by max_distance, returning max_distance + 1 once the
   best possible result on the current row already exceeds it, so a far-off
   candidate costs little. */
static pure fn bounded_osa_distance(StringView a, StringView b,
                                    usize max_distance) wontthrow -> usize
{
  const usize la = a.length;
  const usize lb = b.length;
  if (la > lb ? la - lb > max_distance : lb - la > max_distance)
    return max_distance + 1;
  if (la == 0) return lb;
  if (lb == 0) return la;
  /* The rolling rows are indexed up to lb, so a candidate longer than the row
     width is rejected before the rows are reserved. */
  if (lb + 1 > OSA_ROW_WIDTH) return max_distance + 1;

  usize previous_previous[OSA_ROW_WIDTH];
  usize previous[OSA_ROW_WIDTH];
  usize current[OSA_ROW_WIDTH];

  for (usize j = 0; j <= lb; j++)
    previous[j] = j;
  for (usize i = 1; i <= la; i++) {
    current[0] = i;
    usize row_best = current[0];
    for (usize j = 1; j <= lb; j++) {
      const usize cost = a[i - 1] == b[j - 1] ? 0 : 1;
      usize value = previous[j] + 1;
      if (current[j - 1] + 1 < value) value = current[j - 1] + 1;
      if (previous[j - 1] + cost < value) value = previous[j - 1] + cost;
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
          previous_previous[j - 2] + 1 < value)
      {
        value = previous_previous[j - 2] + 1;
      }
      current[j] = value;
      if (value < row_best) row_best = value;
    }
    if (row_best > max_distance) return max_distance + 1;
    for (usize j = 0; j <= lb; j++) {
      previous_previous[j] = previous[j];
      previous[j] = current[j];
    }
  }
  return previous[lb];
}

fn suggest_command(StringView name, const ArrayList<String> &local_names) throws
    -> Maybe<String>
{
  if (name.is_empty()) return None;

  /* A typo is usually one or two edits, so the search stays within two and a
     shorter name allows only one, which keeps a wild miss silent. */
  const usize max_distance = name.length <= 3 ? 1 : 2;
  usize best_distance = max_distance + 1;
  bool best_is_anagram = false;
  let best = String{};

  /* Same length and same character multiset, so the candidate is a pure
     transposition of the typed name, the most likely typo. Used to break a tie
     in favor of git over gtf for the input gti. */
  let const is_anagram = [](StringView a, StringView b) wontthrow -> bool {
    if (a.length != b.length) return false;
    i32 counts[256] = {0};
    for (usize i = 0; i < a.length; i++) {
      counts[static_cast<u8>(a[i])]++;
      counts[static_cast<u8>(b[i])]--;
    }
    for (i32 count : counts)
      if (count != 0) return false;
    return true;
  };

  let const consider = [&](StringView candidate) throws -> void {
    if (candidate.is_empty() || candidate == name) return;
    const usize distance = bounded_osa_distance(name, candidate, max_distance);
    if (distance > best_distance) return;
    const bool anagram = is_anagram(name, candidate);
    if (distance < best_distance || (anagram && !best_is_anagram)) {
      best_distance = distance;
      best_is_anagram = anagram;
      best = String{candidate};
    }
  };

  for (const String &local : local_names)
    consider(local.view());
  for (const String &builtin : builtin_names())
    consider(builtin.view());
  if (MAYBE_PATH) {
    for (const String &dir_string : split_path_dirs(*MAYBE_PATH)) {
      if (Maybe<ArrayList<String>> entries =
              Path::read_directory(Path{dir_string.view()}))
      {
        for (const String &entry : *entries)
          consider(entry.view());
      }
    }
  }

  if (best_distance > max_distance) return None;
  return best;
}

fn read_entire_file(StringView path) throws -> Maybe<String>
{
  let const file = os::open_file_descriptor(path, os::file_open_mode::Read);
  if (!file) return None;

  let contents = String{};
  char buffer[4096];
  for (;;) {
    Maybe<usize> read_count = os::read_fd(*file, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    contents.append(StringView{buffer, *read_count});
  }

  os::close_fd(*file);

  return contents;
}

fn detect_mimic_shell(const Path &program) throws -> Maybe<MimicMode>
{
  let const file =
      os::open_file_descriptor(program.text().view(), os::file_open_mode::Read);
  if (!file) return None;
  char buffer[256];
  let const read_count = os::read_fd(*file, buffer, sizeof(buffer));
  os::close_fd(*file);
  if (!read_count || *read_count < 3) return None;

  let const head = StringView{buffer, *read_count};
  if (!head.starts_with("#!")) return None;

  /* The shebang ends at the first newline, and only its first line is read. */
  usize line_end = 2;
  while (line_end < head.length && head[line_end] != '\n')
    line_end++;
  let const line = head.substring_of_length(2, line_end - 2);

  /* The basename of a whitespace-delimited token, dropping any directory path. */
  let const basename_of = [](StringView token) -> StringView {
    usize slash = token.length;
    for (usize i = 0; i < token.length; i++)
      if (token[i] == '/') slash = i;
    return slash == token.length ? token
                                 : token.substring(slash + 1);
  };
  /* Walk the line token by token, splitting on spaces and tabs. */
  usize i = 0;
  let const next_token = [&]() -> StringView {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    usize const start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;
    return line.substring_of_length(start, i - start);
  };

  StringView shell = basename_of(next_token());
  /* The /usr/bin/env form names the shell as the next token, after any env
     options, so the first non-option token is taken. */
  if (shell == "env") {
    for (;;) {
      let const token = next_token();
      if (token.length == 0) return None;
      if (token[0] == '-') continue;
      shell = basename_of(token);
      break;
    }
  }

  if (shell == "sh" || shell == "dash") return MimicMode::Posix;
  if (shell == "bash") return MimicMode::Bash;
  if (shell == "shit") return MimicMode::Default;
  return None;
}

fn read_entire_standard_input() throws -> String
{
  let contents = String{};
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
  let line = String{};
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
