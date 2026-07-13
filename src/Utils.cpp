#include "Utils.hpp"

#include "Builtin.hpp"
#include "Cli.hpp"
#include "Containers.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Shitbox.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"

namespace shit {

namespace utils {

fn merge_tokens_to_string(const ArrayList<const Token *> &tokens) throws
    -> String
{
  let result = String{heap_allocator()};
  result.reserve(64);
  for (usize i = 0; i < tokens.count(); i++) {
    let const token = tokens[i];
    ASSERT(token != nullptr);
    result += token->raw_string();
    if (i + 1 < tokens.count()) {
      result += ' ';
    }
  }
  return result;
}

fn execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async) throws
    -> i32
{
  if (ec.is_builtin()) {
    LOG(Debug, "dispatching the builtin '%s'", ec.program().c_str());
    return execute_builtin(steal(ec), cxt);
  }

  /* The terminal external command may replace the shell in place when it is the
     last command, not in a subshell, and no EXIT trap is pending. */
  let const can_replace_shell =
      cxt.terminal_exec_allowed() && !cxt.in_subshell() && !cxt.has_exit_trap();

  /* Mimicry runs the script in-process, a background command keeps its fork.
   */
  if (cxt.mimicry() && !is_async) {
    if (Maybe<mimic_mood> mode = ec.program_path().detect_mimic_shell();
        mode.has_value())
    {
      LOG(Debug, "execute_context mimicking the shell for '%s'",
          ec.program().c_str());
#if SHIT_PLATFORM_IS POSIX
      if (cxt.shell_is_interactive() && os::shell_has_controlling_terminal()) {
        let const command = String{ec.program().view()};

        /* The child blocks on this pipe until the parent hands off the
           terminal, so it never touches the terminal before the handoff. */
        let const sync_pipe = os::make_pipe();

        shit::flush();
        let const child = os::fork_job_process();
        if (os::process_id_of(child) == 0) {
          if (sync_pipe.has_value()) {
            /* The child drops its write end so the read unblocks on EOF if
               the parent dies before the handoff. */
            os::close_fd(sync_pipe->out);
            char handoff_byte = 0;
            (void) os::read_fd(sync_pipe->in, &handoff_byte, 1);
            os::close_fd(sync_pipe->in);
          }
          i32 status = 1;
          try {
            status = cxt.run_mimicked_script(ec, *mode, false);
          } catch (const ErrorBase &error) {
            const String *source = cxt.current_source();
            show_message(error.to_string(source != nullptr ? source->view()
                                                           : StringView{}));
            status = static_cast<i32>(error.command_status());
          } catch (...) {}
          os::exit_process_immediately(status);
        }

        if (sync_pipe.has_value()) os::close_fd(sync_pipe->in);
        os::give_controlling_terminal_to(child);
        if (sync_pipe.has_value()) {
          (void) os::write_fd(sync_pipe->out, "x", 1);
          os::close_fd(sync_pipe->out);
        }

        let was_stopped = false;
        const i32 status = os::wait_and_monitor_process(child, &was_stopped);
        os::reclaim_controlling_terminal();

        if (was_stopped) {
          const i32 id = cxt.register_stopped_job(child, command, status);
          cxt.notify_stopped_job(id, command.view());
        }
        return status;
      }
#endif
      return cxt.run_mimicked_script(ec, *mode, !can_replace_shell);
    }
  }

  /* The terminal external command replaces the shell in place, the way dash
     execs the last command under EV_EXIT. The EXIT trap is rechecked at run
     time here, since one set earlier in this chunk must still run. */
  if (!is_async && can_replace_shell) {
    LOG(Debug,
        "execute_context replacing the shell with the terminal command '%s'",
        ec.program().c_str());
    flush();
    try {
      os::replace_process(steal(ec));
    } catch (const ExecFormatError &) {
      LOG(Debug, "swallowed an exec format error, running the "
                 "file as a shell script in place");
      /* replace_process already placed the redirections, so the descriptors
         are cleared to avoid reapplying the now-closed ones. */
      ec.in_fd.reset();
      ec.out_fd.reset();
      ec.err_fd.reset();
      const mimic_mood mode = cxt.mood();
      quit(cxt.run_mimicked_script(ec, mode, !can_replace_shell), false);
    } catch (const ErrorWithLocation &error) {
      /* Resolved but unexecutable exits 126, missing exits 127. */
      const String *source = cxt.current_source();
      show_message(
          error.to_string(source != nullptr ? source->view() : StringView{}));
      quit(126, false);
    } catch (const Error &error) {
      print_error(error.message() + "\n");
      quit(127, false);
    }
    unreachable();
  }

  LOG(Debug, "spawning the external command '%s'%s", ec.program().c_str(),
      is_async ? " in the background" : "");

  /* An interactive foreground command runs in its own process group and holds
     the terminal, so it dies on its own Ctrl-C. */
  const bool is_foreground_job = !is_async && cxt.shell_is_interactive() &&
                                 os::shell_has_controlling_terminal();

  let command = String{heap_allocator()};
  if (is_async || is_foreground_job) {
    for (usize i = 0; i < ec.args().count(); i++) {
      if (i > 0) command += ' ';
      command += ec.args()[i].view();
    }
    if (is_async) command += " &";
  }

  os::process p = SHIT_INVALID_PROCESS;
  try {
    p = os::execute_program(steal(ec), !is_async,
                            /*new_process_group=*/is_foreground_job);
  } catch (const ExecFormatError &) {
    LOG(Debug, "swallowed an exec format error, running the "
               "file as a shell script in this process");
    const mimic_mood mode = cxt.mood();
    return cxt.run_mimicked_script(ec, mode, !can_replace_shell);
  }
  if (is_async) {
    cxt.set_last_background_pid(os::process_id_of(p));
    const i32 id = cxt.register_job(p, command);
    if (cxt.shell_is_interactive())
      shit::print_error("[" + String::from(id, heap_allocator()) + "] " +
                        String::from(static_cast<u64>(os::process_id_of(p)),
                                     heap_allocator()) +
                        "\n");
    return 0;
  }

  LOG(Debug, "waiting for the foreground child to finish");
  if (is_foreground_job) os::give_controlling_terminal_to(p);
  let was_stopped = false;
  const i32 foreground_status = os::wait_and_monitor_process(
      p, is_foreground_job ? &was_stopped : nullptr);
  if (is_foreground_job) os::reclaim_controlling_terminal();
  if (was_stopped) {
    const i32 id = cxt.register_stopped_job(p, command, foreground_status);
    cxt.notify_stopped_job(id, command.view());
  }
  return foreground_status;
}

fn execute_contexts_with_pipes(ArrayList<ExecContext> &&ecs, EvalContext &cxt,
                               bool is_async) throws -> i32
{
  ASSERT(ecs.count() > 1);

  LOG(Debug, "running a pipeline of %zu stages%s", ecs.count(),
      is_async ? " in the background" : "");

  i32 ret = 0;

  /* Every external stage is collected so all of them are reaped, not only the
     last. Otherwise a first stage like yes is left a zombie when the last stage
     exits. */
  let children = ArrayList<os::process>{heap_allocator()};
  os::process last_child = SHIT_INVALID_PROCESS;
  os::descriptor last_stdin = SHIT_INVALID_FD;

  /* Each stage's status is recorded against its position, so pipefail can
     report the rightmost stage that failed and the plain case can read the last
     stage. A builtin stage yields its status at once and an external one's
     status arrives from the wait below, tracked by the parallel child-to-stage
     list. */
  let const stage_count = ecs.count();
  let stage_status = ArrayList<i32>{heap_allocator()};
  stage_status.reserve(stage_count);
  for (usize i = 0; i < stage_count; i++)
    stage_status.push(0);
  let child_stage = ArrayList<usize>{heap_allocator()};

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
      /* An explicit > takes the stage's stdout, so the pipe end closes unused.
       */
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

    if (ec.is_unresolved()) {
      /* Unresolved runs nothing, its slot carries 127 for pipefail. */
      stage_status[stage_index] = 127;
      ec.close_fds();
    } else if (!ec.is_builtin()) {
      let const child = os::execute_program(steal(ec));
      children.push(child);
      child_stage.push(stage_index);
      last_child = child;
    } else if (!is_last) {
#if SHIT_PLATFORM_IS POSIX
      /* A non-last builtin stage forks, an in-process run deadlocks when it
         fills the pipe buffer before its consumer starts. */
      const os::process child =
          os::fork_compound_stage(ec.in_fd, ec.out_fd, ec.err_fd);
      if (child == 0) {
        /* fork_compound_stage already placed the pipe ends, so the context's
           descriptors are cleared. */
        ec.in_fd = shit::None;
        ec.out_fd = shit::None;
        ec.err_fd = shit::None;
        /* A forked builtin never execs, so it closes the read end by hand or a
           write never sees the reader leave. */
        if (last_stdin != SHIT_INVALID_FD) os::close_fd(last_stdin);
        cxt.set_in_pipeline_stage(true);
        cxt.enter_subshell();
        i32 child_status = 0;
        try {
          child_status = execute_builtin(steal(ec), cxt);
        } catch (const BrokenPipeExit &) {
          child_status = SHIT_BROKEN_PIPE_EXIT_STATUS;
        } catch (const ErrorWithLocation &e) {
          const String *source = cxt.current_source();
          shit::show_message(
              e.to_string(source != nullptr ? source->view() : StringView{}));
          child_status = 1;
        } catch (const Error &e) {
          shit::show_message(e.to_string());
          child_status = 1;
        } catch (...) {
          child_status = 1;
        }
        shit::flush();
        os::exit_process_immediately(child_status);
      }
      /* The parent keeps no copy of the pipe ends, or the reader never sees the
         writer close. */
      ec.close_fds();
      children.push(child);
      child_stage.push(stage_index);
      last_child = child;
#else
      /* Windows has no fork, a non-last builtin stage runs in process and can
         block when it fills the pipe buffer. */
      cxt.set_in_pipeline_stage(true);
      defer { cxt.set_in_pipeline_stage(false); };
      ret = execute_builtin(steal(ec), cxt);
      stage_status[stage_index] = ret;
#endif
    } else {
      /* The last builtin stage runs in this process so a cd affects the shell.
         The flag makes exec spawn a child rather than replace the shell. */
      cxt.set_in_pipeline_stage(true);
      defer { cxt.set_in_pipeline_stage(false); };
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
            "[" + String::from(id, heap_allocator()) + "] " +
            String::from(static_cast<u64>(os::process_id_of(last_child)),
                         heap_allocator()) +
            "\n");
    }
    return ret;
  }

  for (usize i = 0; i < children.count(); i++)
    stage_status[child_stage[i]] = os::wait_and_monitor_process(children[i]);

  let pipe_status = ArrayList<String>{heap_allocator()};
  pipe_status.reserve(stage_count);
  for (usize i = 0; i < stage_count; i++)
    pipe_status.push(String::from(stage_status[i], heap_allocator()));
  cxt.set_indexed_array("PIPESTATUS", steal(pipe_status));

  /* pipefail reports the rightmost failing stage, otherwise the last stage. */
  if (cxt.pipefail()) {
    for (usize i = stage_count; i > 0; i--)
      if (stage_status[i - 1] != 0) return stage_status[i - 1];
    return 0;
  }

  return stage_status[stage_count - 1];
}

pure fn strip_sig_prefix(StringView name) wontthrow -> StringView
{
  if (name.starts_with("SIG")) return name.substring(3);
  return name;
}

fn split_lines(StringView text) throws -> ArrayList<StringView>
{
  let lines = ArrayList<StringView>{heap_allocator()};
  usize line_count = 1;
  for (usize i = 0; i < text.length; i++)
    if (text[i] == '\n') line_count++;
  lines.reserve(line_count);

  usize line_start_position = 0;
  for (usize i = 0; i <= text.length; i++) {
    if (i != text.length && text[i] != '\n') continue;

    lines.push(
        text.substring_of_length(line_start_position, i - line_start_position));
    line_start_position = i + 1;
  }
  return lines;
}

fn format_unix_timestamp(i64 unix_time, const char *format) throws -> String
{
  time_t when = static_cast<time_t>(unix_time);
  struct tm *local = ::localtime(&when);
  if (local == nullptr) return String{heap_allocator()};

  char buffer[128];
  usize written = ::strftime(buffer, sizeof(buffer), format, local);
  return String{
      StringView{buffer, written}
  };
}

pure fn is_posix_reserved_word(StringView word) wontthrow -> bool
{
  static const StringView RESERVED_WORDS[] = {
      "!",    "{",  "}",   "case", "do", "done", "elif",  "else",
      "esac", "fi", "for", "if",   "in", "then", "until", "while",
  };
  for (let const reserved : RESERVED_WORDS)
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
    return static_cast<i64>(~magnitude + 1u);
  }
  if (has_overflowed || magnitude > static_cast<u64>(INT64_MAX))
    return INT64_MAX;
  return static_cast<i64>(magnitude);
}

static fn not_an_integer_error(StringView text) throws -> Error
{
  return Error{"'" + text + "' is not a valid integer"};
}

fn int_to_text_into(i64 value, char *buffer, usize buffer_size) wontthrow
    -> StringView
{
  /* The digits are written from the least significant end of the buffer, the
     same scheme String::from uses, then a leading minus is prepended. A u64
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
  /* An rusage subtraction can go backwards, a negative clamps to zero to avoid
     a doubled sign like -0m-0.001s. */
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
  let report = String{heap_allocator()};
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
                             double system_seconds, u64 peak_rss_bytes) throws
    -> String
{
  const double cpu_percent =
      real_seconds > 0.0
          ? (user_seconds + system_seconds) / real_seconds * 100.0
          : 0.0;
  char buffer[64];
  let report = String{heap_allocator()};
  report += "\n";
  report += "  real   " + format_minutes_seconds(real_seconds) + "\n";
  report += "  user   " + format_minutes_seconds(user_seconds) + "\n";
  report += "  sys    " + format_minutes_seconds(system_seconds) + "\n";
  std::snprintf(buffer, sizeof(buffer), "  cpu    %.0f%%\n", cpu_percent);
  report += buffer;

  if (peak_rss_bytes > 0)
    report += "  rss    " +
              shitbox::format_human_size(peak_rss_bytes, heap_allocator()) +
              "\n";

  return report;
}

fn format_time_report_custom(StringView format, double real_seconds,
                             double user_seconds, double system_seconds) throws
    -> String
{
  let report = String{heap_allocator()};

  for (usize i = 0; i < format.length; i++) {
    if (format[i] != '%') {
      report.push(format[i]);
      continue;
    }

    i++;
    if (i >= format.length) {
      report.push('%');
      break;
    }
    if (format[i] == '%') {
      report.push('%');
      continue;
    }

    /* A precision digit and the l flag may precede the conversion, %3lR is
       three digits in minutes form, precision clamped to six. */
    usize precision = 3;
    if (format[i] >= '0' && format[i] <= '9') {
      precision = static_cast<usize>(format[i] - '0');
      if (precision > 6) precision = 6;
      i++;
    }

    bool is_long_format = false;
    if (i < format.length && format[i] == 'l') {
      is_long_format = true;
      i++;
    }

    if (i >= format.length) {
      report.push('%');
      break;
    }

    char buffer[64];
    let const code = format[i];

    double value = 0.0;
    switch (code) {
    case 'R': value = real_seconds; break;
    case 'U': value = user_seconds; break;
    case 'S': value = system_seconds; break;

    case 'P': {
      const double cpu_percent =
          real_seconds > 0.0
              ? (user_seconds + system_seconds) / real_seconds * 100.0
              : 0.0;
      std::snprintf(buffer, sizeof(buffer), "%.2f", cpu_percent);
      report += buffer;
      continue;
    }

    default:
      report.push('%');
      report.push(code);
      continue;
    }

    if (value < 0.0) value = 0.0;

    if (is_long_format) {
      const i64 minutes = static_cast<i64>(value) / 60;
      const double remainder = value - static_cast<double>(minutes * 60);
      std::snprintf(buffer, sizeof(buffer), "%ldm%.*fs",
                    static_cast<long>(minutes), static_cast<int>(precision),
                    remainder);
    } else {
      std::snprintf(buffer, sizeof(buffer), "%.*f", static_cast<int>(precision),
                    value);
    }
    report += buffer;
  }

  report.push('\n');
  return report;
}

/* A newline offset table cached on one source, keyed on the source pointer and
   length, so a $LINENO lookup is a binary search over the newlines. */
class LineNumberCache
{
public:
  LineNumberCache() : m_newline_offsets(heap_allocator()) {}

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

static fn skip_ascii_whitespace(StringView text, usize &offset) wontthrow
    -> void
{
  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;
}

struct parsed_integer_magnitude
{
  u64 magnitude;
  bool is_negative;
  bool has_overflowed;
};

static fn parse_decimal_magnitude(StringView text) throws
    -> ErrorOr<parsed_integer_magnitude>
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
  /* A u64 holds up to nineteen decimal digits with no overflow, so the first
     nineteen accumulate without the per-digit overflow division, which the hot
     arithmetic read path runs millions of times on small numbers. The division
     guard runs only once a twentieth digit can push past the type's range. */
  usize digit_count = 0;
  while (offset < text.length && text.data[offset] >= '0' &&
         text.data[offset] <= '9')
  {
    const u64 digit = static_cast<u64>(text.data[offset] - '0');
    has_digits = true;
    digit_count++;
    if (digit_count <= 19) {
      magnitude = magnitude * 10 + digit;
    } else if (magnitude > (UINT64_MAX - digit) / 10) {
      has_overflowed = true;
    } else {
      magnitude = magnitude * 10 + digit;
    }
    offset++;
  }

  skip_ascii_whitespace(text, offset);
  if (!has_digits || offset != text.length) return not_an_integer_error(text);

  return parsed_integer_magnitude{magnitude, is_negative, has_overflowed};
}

fn parse_decimal_i64(StringView text, bool *out_of_range) throws -> ErrorOr<i64>
{
  let const parsed = TRY(parse_decimal_magnitude(text));

  if (out_of_range != nullptr)
    *out_of_range = parsed.has_overflowed ||
                    parsed.magnitude > static_cast<u64>(INT64_MAX) +
                                           (parsed.is_negative ? 1u : 0u);

  return saturate_signed_magnitude(parsed.magnitude, parsed.is_negative,
                                   parsed.has_overflowed);
}

fn parse_decimal_u64(StringView text) throws -> ErrorOr<u64>
{
  let const parsed = TRY(parse_decimal_magnitude(text));
  if (parsed.is_negative || parsed.has_overflowed)
    return Error{"integer value out of range"};
  return parsed.magnitude;
}

fn parse_decimal_f64(const String &text) throws -> ErrorOr<f64>
{
  let const start = text.c_str();
  char *end = nullptr;
  errno = 0;
  let const parsed_value = ::strtold(start, &end);
  if (end == start || end != start + text.length()) {
    return Error{"invalid number"};
  }

  let digits = start;
  while (*digits == ' ' || *digits == '\t' || *digits == '\n' ||
         *digits == '\r' || *digits == '\f' || *digits == '\v')
  {
    digits++;
  }
  if (*digits == '+' || *digits == '-') {
    digits++;
  }
  if (digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    return Error{"invalid number"};
  }

  let const magnitude = __builtin_fabsl(parsed_value);
  if (errno == ERANGE &&
      (magnitude == 0.0L || !__builtin_isfinite(parsed_value)))
  {
    return Error{"number value out of range"};
  }
  if (__builtin_isfinite(parsed_value) &&
      (magnitude > static_cast<long double>(__DBL_MAX__) ||
       (magnitude != 0.0L &&
        magnitude < static_cast<long double>(__DBL_DENORM_MIN__))))
  {
    return Error{"number value out of range"};
  }

  return static_cast<f64>(parsed_value);
}

fn format_f64(f64 value, Allocator allocator) throws -> String
{
  char buffer[32];
  let const length = ::snprintf(buffer, sizeof(buffer), "%.17g", value);
  if (length < 0 || static_cast<usize>(length) >= sizeof(buffer)) {
    return String{allocator};
  }

  return String{
      allocator, StringView{buffer, static_cast<usize>(length)}
  };
}

fn parse_timeout_seconds_to_nanos(StringView text) throws -> ErrorOr<i64>
{
  usize offset = 0;
  skip_ascii_whitespace(text, offset);

  u64 whole_seconds = 0;
  bool has_overflowed = false;
  bool has_digits = false;
  while (offset < text.length && text.data[offset] >= '0' &&
         text.data[offset] <= '9')
  {
    let const digit = static_cast<u64>(text.data[offset] - '0');
    if (whole_seconds > (UINT64_MAX - digit) / 10)
      has_overflowed = true;
    else
      whole_seconds = whole_seconds * 10 + digit;
    has_digits = true;
    offset++;
  }

  i64 fractional_nanos = 0;
  if (offset < text.length && text.data[offset] == '.') {
    offset++;
    i64 digit_scale = 100'000'000;
    while (offset < text.length && text.data[offset] >= '0' &&
           text.data[offset] <= '9')
    {
      fractional_nanos += (text.data[offset] - '0') * digit_scale;
      digit_scale /= 10;
      has_digits = true;
      offset++;
    }
  }

  skip_ascii_whitespace(text, offset);
  if (!has_digits || offset != text.length)
    return Error{"'" + text + "' is not a valid timeout"};

  /* A whole-seconds part too large for the signed nanosecond result saturates
     to the maximum rather than overflowing. */
  constexpr u64 max_whole_seconds = INT64_MAX / 1'000'000'000;
  constexpr i64 max_fractional_nanos = INT64_MAX % 1'000'000'000;
  if (has_overflowed || whole_seconds > max_whole_seconds ||
      (whole_seconds == max_whole_seconds &&
       fractional_nanos > max_fractional_nanos))
  {
    return static_cast<i64>(INT64_MAX);
  }

  return static_cast<i64>(whole_seconds) * 1'000'000'000 + fractional_nanos;
}

static pure fn digit_value_in_base(char c, u32 radix) wontthrow -> i32
{
  u32 value;
  if (c >= '0' && c <= '9')
    value = static_cast<u32>(c - '0');
  else if (c >= 'a' && c <= 'z')
    value = static_cast<u32>(c - 'a') + 10;
  else if (c >= 'A' && c <= 'Z')
    value = static_cast<u32>(c - 'A') + 10;
  else
    return -1;

  return value < radix ? static_cast<i32>(value) : -1;
}

static fn parse_magnitude_in_base(StringView text, int_base base) throws
    -> ErrorOr<parsed_integer_magnitude>
{
  let const radix = static_cast<u32>(base);
  usize offset = 0;
  skip_ascii_whitespace(text, offset);

  bool is_negative = false;
  if (offset < text.length &&
      (text.data[offset] == '+' || text.data[offset] == '-'))
  {
    is_negative = text.data[offset] == '-';
    offset++;
  }

  if (base == int_base::hex && offset + 1 < text.length &&
      text.data[offset] == '0' &&
      (text.data[offset + 1] == 'x' || text.data[offset + 1] == 'X'))
  {
    offset += 2;
  } else if (base == int_base::binary && offset + 1 < text.length &&
             text.data[offset] == '0' &&
             (text.data[offset + 1] == 'b' || text.data[offset + 1] == 'B'))
  {
    offset += 2;
  }

  u64 magnitude = 0;
  bool has_digits = false;
  bool has_overflowed = false;
  while (offset < text.length) {
    let const digit = digit_value_in_base(text.data[offset], radix);
    if (digit < 0) break;
    has_digits = true;
    if (magnitude > (UINT64_MAX - static_cast<u64>(digit)) / radix)
      has_overflowed = true;
    else
      magnitude = magnitude * radix + static_cast<u64>(digit);
    offset++;
  }

  skip_ascii_whitespace(text, offset);
  if (!has_digits || offset != text.length) return not_an_integer_error(text);

  return parsed_integer_magnitude{magnitude, is_negative, has_overflowed};
}

fn parse_integer_in_base(StringView text, int_base base,
                         bool *out_of_range) throws -> ErrorOr<i64>
{
  let const parsed = TRY(parse_magnitude_in_base(text, base));

  if (out_of_range != nullptr)
    *out_of_range = parsed.has_overflowed ||
                    parsed.magnitude > static_cast<u64>(INT64_MAX) +
                                           (parsed.is_negative ? 1u : 0u);

  return saturate_signed_magnitude(parsed.magnitude, parsed.is_negative,
                                   parsed.has_overflowed);
}

fn parse_integer_in_base_u64(StringView text, int_base base) throws
    -> ErrorOr<u64>
{
  let const parsed = TRY(parse_magnitude_in_base(text, base));
  if (parsed.is_negative || parsed.has_overflowed)
    return Error{"integer value out of range"};
  return parsed.magnitude;
}

fn expand_leading_tilde_path(StringView name) throws -> Maybe<String>
{
  if (name.is_empty() || name[0] != '~') return None;

  let const slash = name.find_character('/');
  let const user = slash.has_value() ? name.substring_of_length(1, *slash - 1)
                                     : name.substring(1);
  Maybe<Path> home =
      user.is_empty() ? os::get_home_directory() : os::get_home_for_user(user);
  if (!home.has_value()) return None;

  let expanded = *home;
  if (slash.has_value()) expanded.push_component(name.substring(*slash + 1));
  return String{expanded.text().view()};
}

fn decode_ansi_c_escapes(String &out, StringView body) throws -> void
{
  let const do_hex_value = [](char h) -> i32 {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    return -1;
  };

  let do_emit_codepoint = [&](u32 cp) throws {
    if (cp < 0x80) {
      out.push(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push(static_cast<char>(0xC0 | (cp >> 6)));
      out.push(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push(static_cast<char>(0xE0 | (cp >> 12)));
      out.push(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push(static_cast<char>(0xF0 | (cp >> 18)));
      out.push(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  };

  usize i = 0;
  while (i < body.length) {
    const char c = body[i];
    i++;

    if (c != '\\') {
      out.push(c);
      continue;
    }
    if (i >= body.length) {
      out.push('\\');
      break;
    }

    const char e = body[i];
    i++;
    switch (e) {
    case 'n': out.push('\n'); break;
    case 't': out.push('\t'); break;
    case 'r': out.push('\r'); break;
    case 'a': out.push('\a'); break;
    case 'b': out.push('\b'); break;
    case 'f': out.push('\f'); break;
    case 'v': out.push('\v'); break;
    case 'e':
    case 'E': out.push('\x1b'); break;
    case '\\': out.push('\\'); break;
    case '\'': out.push('\''); break;
    case '"': out.push('"'); break;
    case '?': out.push('?'); break;
    case 'x': {
      i32 value = 0;
      i32 digit_count = 0;
      while (digit_count < 2 && i < body.length) {
        let const digit = do_hex_value(body[i]);
        if (digit < 0) break;
        value = value * 16 + digit;
        i++;
        digit_count++;
      }
      if (digit_count == 0) {
        out.push('\\');
        out.push('x');
      } else {
        out.push(static_cast<char>(value));
      }
    } break;
    case 'c': {
      if (i >= body.length) {
        out.push('\\');
        out.push('c');
        break;
      }
      const char target = body[i];
      i++;
      if (target == '\\' && i < body.length && body[i] == '\\') {
        i++;
      }
      const char upper = (target >= 'a' && target <= 'z')
                             ? static_cast<char>(target - 'a' + 'A')
                             : target;
      const u8 control = upper == '?'
                             ? static_cast<u8>(0x7fu)
                             : static_cast<u8>(static_cast<u8>(upper) & 0x1fu);
      out.push(static_cast<char>(control));
    } break;
    case 'u':
    case 'U': {
      const i32 max_digit_count = e == 'u' ? 4 : 8;
      u32 codepoint = 0;
      i32 digit_count = 0;
      while (digit_count < max_digit_count && i < body.length) {
        let const digit = do_hex_value(body[i]);
        if (digit < 0) break;
        codepoint = codepoint * 16 + static_cast<u32>(digit);
        i++;
        digit_count++;
      }
      if (digit_count == 0) {
        out.push('\\');
        out.push(e);
      } else {
        do_emit_codepoint(codepoint);
      }
    } break;
    default:
      if (e >= '0' && e <= '7') {
        i32 value = e - '0';
        i32 digit_count = 1;
        while (digit_count < 3 && i < body.length && body[i] >= '0' &&
               body[i] <= '7')
        {
          value = value * 8 + (body[i] - '0');
          i++;
          digit_count++;
        }
        out.push(static_cast<char>(value));
      } else {
        out.push('\\');
        out.push(e);
      }
      break;
    }
  }
}

fn append_ansi_c_quote_if_needed(String &out, StringView arg) throws -> bool
{
  if (arg.is_empty()) {
    out += "''";
    return true;
  }

  bool has_control_byte = false;
  for (usize i = 0; i < arg.length; i++) {
    let const byte = static_cast<unsigned char>(arg[i]);
    if (byte < 0x20 || byte == 0x7f) {
      has_control_byte = true;
      break;
    }
  }
  if (!has_control_byte) return false;

  out += "$'";
  for (usize i = 0; i < arg.length; i++) {
    let const character = arg[i];
    switch (character) {
    case '\a': out += "\\a"; break;
    case '\b': out += "\\b"; break;
    case '\t': out += "\\t"; break;
    case '\n': out += "\\n"; break;
    case '\v': out += "\\v"; break;
    case '\f': out += "\\f"; break;
    case '\r': out += "\\r"; break;
    case '\x1b': out += "\\E"; break;
    case '\'': out += "\\'"; break;
    case '\\': out += "\\\\"; break;
    default: {
      let const byte = static_cast<unsigned char>(character);
      if (byte < 0x20 || byte == 0x7f) {
        out.push('\\');
        out.push(static_cast<char>('0' + ((byte >> 6) & 7)));
        out.push(static_cast<char>('0' + ((byte >> 3) & 7)));
        out.push(static_cast<char>('0' + (byte & 7)));
      } else {
        out.push(character);
      }
      break;
    }
    }
  }
  out += "'";
  return true;
}

/* Inspiration taken from https://github.com/tsoding/glob.h :3
 * This fragment is under MIT License (c) Alexey Kutepov <reximkut@gmail.com> */
static pure fn is_glob_char_active(const Bitset &glob_active,
                                   usize index) wontthrow -> bool
{
  return index < glob_active.count() && glob_active[index];
}

namespace {

/* One alternative of a bash extended-glob group, a slice of the glob and the
   mask offset that slice begins at. */
struct extglob_alternative
{
  StringView pattern;
  usize mask_offset;
};

hot fn extglob_active(const Bitset &mask, usize index) wontthrow -> bool
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
  if (op != '?' && op != '*' && op != '+' && op != '@' && op != '!') {
    return false;
  }
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

fn extglob_full_match(StringView glob, StringView str, const Bitset &mask,
                      usize mask_offset) throws -> bool;

/* Match min_reps or more repetitions of one of the alternatives against the
   front of str, then the suffix against the rest. The min drops to zero after
   the first repetition, so a + needs one and a * needs none. */
fn extglob_match_repetition(const ArrayList<extglob_alternative> &alternatives,
                            StringView suffix, usize suffix_offset,
                            StringView str, const Bitset &mask,
                            usize min_reps) throws -> bool
{
  if (min_reps == 0 && extglob_full_match(suffix, str, mask, suffix_offset)) {
    return true;
  }
  for (let const &alternative : alternatives) {
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

fn extglob_full_match(StringView glob, StringView str, const Bitset &mask,
                      usize mask_offset) throws -> bool
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
        for (let const &alternative : alternatives) {
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
          for (let const &alternative : alternatives) {
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
          {
            return true;
          }
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

  if (active && head == '?') {
    return extglob_full_match(glob.substring(1), str.substring(1), mask,
                              mask_offset + 1);
  }

  if (active && head == '[') {
    /* Reuse the iterative matcher for a single bracket class by matching one
       character, then continue with the rest of the glob and the string. */
    usize span = 1;
    while (span < glob.count() &&
           !(glob[span] == ']' && extglob_active(mask, mask_offset + span)))
      span++;
    if (span < glob.count()) {
      span++;
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

/* The POSIX character classes a bracket accepts as [:name:], each name bound
   to its ctype predicate through a packed-key map so the glob hot path pays
   a word compare rather than a name chain. The wrappers pin the byte through
   unsigned char, the only argument range the ctype functions define. */
using posix_class_test = bool (*)(u8 byte);

constexpr static_string_entry<posix_class_test> POSIX_CLASS_ENTRIES[] = {
    {SSK("alnum"),  [](u8 byte) { return std::isalnum(byte) != 0; } },
    {SSK("alpha"),  [](u8 byte) { return std::isalpha(byte) != 0; } },
    {SSK("blank"),  [](u8 byte) { return std::isblank(byte) != 0; } },
    {SSK("cntrl"),  [](u8 byte) { return std::iscntrl(byte) != 0; } },
    {SSK("digit"),  [](u8 byte) { return std::isdigit(byte) != 0; } },
    {SSK("graph"),  [](u8 byte) { return std::isgraph(byte) != 0; } },
    {SSK("lower"),  [](u8 byte) { return std::islower(byte) != 0; } },
    {SSK("print"),  [](u8 byte) { return std::isprint(byte) != 0; } },
    {SSK("punct"),  [](u8 byte) { return std::ispunct(byte) != 0; } },
    {SSK("space"),  [](u8 byte) { return std::isspace(byte) != 0; } },
    {SSK("upper"),  [](u8 byte) { return std::isupper(byte) != 0; } },
    {SSK("xdigit"), [](u8 byte) { return std::isxdigit(byte) != 0; }},
};

constexpr StaticStringMap POSIX_CLASSES{POSIX_CLASS_ENTRIES};

/* Whether the byte belongs to the named class. An unknown name matches
   nothing, the way bash treats a class it does not know. */
fn byte_is_in_posix_class(StringView class_name, u8 byte) throws -> bool
{
  if (const Maybe<posix_class_test> test = POSIX_CLASSES.find(class_name);
      test.has_value())
    return (*test)(byte);
  return false;
}

} /* namespace */

fn glob_matches(StringView glob, StringView str, const Bitset &glob_active,
                usize mask_offset, bool extglob) throws -> bool
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
      {
        return extglob_full_match(glob, str, glob_active, mask_offset);
      }
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

      /* A [:name:] unit inside the bracket is a POSIX character class. The
         index past its closing ":]" comes back when one starts here, so both
         scans treat the unit atomically and its inner ] never closes the
         bracket. */
      let const class_end_past = [&](usize index) wontthrow -> Maybe<usize> {
        if (index + 1 >= glob.count() || glob[index] != '[' ||
            glob[index + 1] != ':' || !is_active(index))
          return None;
        for (usize scan = index + 2; scan + 1 < glob.count(); scan++) {
          if (glob[scan] == ':' && glob[scan + 1] == ']') return scan + 2;
          /* A ] before any ":]" means the [ was a plain member after all, the
             way [[:a] is a bracket holding [, :, and a. */
          if (glob[scan] == ']' && is_active(scan)) return None;
        }
        return None;
      };

      /* A bracket with no closing ] is not a character class, so the [ is a
         literal character, as POSIX specifies. A ] right after [ or [^ is a
         member, so the scan for the closing ] starts past it. */
      usize close_scan = g + 1;
      if (close_scan < glob.count() &&
          (glob[close_scan] == '!' || glob[close_scan] == '^') &&
          is_active(close_scan))
      {
        close_scan++;
      }
      if (close_scan < glob.count() && is_close_at(close_scan)) {
        close_scan++;
      }
      bool has_closing_bracket = false;
      while (close_scan < glob.count()) {
        if (Maybe<usize> past_class = class_end_past(close_scan);
            past_class.has_value())
        {
          close_scan = *past_class;
          continue;
        }
        if (is_close_at(close_scan)) {
          has_closing_bracket = true;
          break;
        }
        close_scan++;
      }
      if (!has_closing_bracket) {
        if (byte_at(glob, g) != byte_at(str, s)) return false;
        g++;
        s++;
        break;
      }

      g++;
      if (g >= glob.count()) GLOB_GROUP_ERR();

      /* POSIX sh negates a class with a leading '!'. The '^' form is kept as a
         common extension. The negation applies only to an active byte, so a
         quoted ! or ^ at the front is a literal member. */
      if ((glob[g] == '!' || glob[g] == '^') && is_active(g)) {
        g++;
        should_negate = true;

        if (g >= glob.count()) GLOB_GROUP_ERR();
      }

      /* The first member bypasses the close check, so a leading ] is a plain
         member, and a class never opens a range, so a - after one is a plain
         member too. */
      u8 prev_glob_ch = 0;
      bool is_first_member = true;
      bool prev_member_was_class = false;
      while (g < glob.count() && (is_first_member || !is_close_at(g))) {
        if (Maybe<usize> past_class = class_end_past(g); past_class.has_value())
        {
          let const class_name =
              glob.substring_of_length(g + 2, *past_class - g - 4);
          is_matched |= byte_is_in_posix_class(class_name, byte_at(str, s));
          g = *past_class;
          prev_member_was_class = true;
          is_first_member = false;
          continue;
        }
        if (!is_first_member && !prev_member_was_class && glob[g] == '-' &&
            is_active(g))
        {
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
        prev_member_was_class = false;
        is_first_member = false;
      }

      if (g >= glob.count() || !is_close_at(g)) {
        GLOB_GROUP_ERR();
      }
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

/* The one context quit reads the interactive state and the memory-report flag
   from, so quit gates the goodbye on a real interactive prompt and a script,
   a -c, or a subshell exits silently the way dash does. A null pointer, the
   state before the context exists, reads as a non-interactive shell with the
   report off. */
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
  os::malloc_heap_stats heap_stats{};
  if (os::read_malloc_heap_stats(heap_stats))
    std::fprintf(stderr,
                 "Malloc heap: in use %zu, total arena %zu, mmapped %zu\n",
                 heap_stats.bytes_in_use, heap_stats.arena_bytes,
                 heap_stats.mapped_bytes);
}

[[noreturn]] fn quit(i32 code, bool should_goodbye) throws -> void
{
  LOG(Info, "quitting with code %d", code);

  if (QUIT_CONTEXT != nullptr && QUIT_CONTEXT->memory_stats_enabled())
    print_memory_report();

  const u8 actual_code = static_cast<u8>(code);

  if (!os::is_child_process()) {
    if (toiletline::is_active()) {
      try {
        toiletline::exit();
      } catch (const Error &e) {
        show_message(e.to_string());
      }
    }

    if (should_goodbye && QUIT_CONTEXT != nullptr &&
        QUIT_CONTEXT->shell_is_interactive())
    {
      let code_str = String{heap_allocator()};
      if (code != 0) {
        code_str += " (Code ";
        code_str += String::from(actual_code, heap_allocator());
        code_str += ')';
      }
      show_message("Goodbye :c" + code_str);
    }
  }

  std::exit(actual_code);
}

struct cached_program_path
{
  Path path;
  os::program_extension extension{os::program_extension::None};
  bool is_runnable{false};
};

struct program_path_cache_entry
{
  ArrayList<cached_program_path> paths{heap_allocator()};
  Maybe<usize> bare_path_position{};
  bool is_complete{false};
};

static StringMap<program_path_cache_entry> PATH_CACHE{heap_allocator()};

struct cached_directory_listing
{
  i64 modification_time{0};
  u32 modification_nanoseconds{0};
  u64 size{0};
  bool is_valid{false};
  ArrayList<Path::directory_child> entries{heap_allocator()};
};

static StringMap<cached_directory_listing> DIR_LISTING_CACHE{heap_allocator()};

static fn clear_directory_listing_cache() throws -> void
{
  DIR_LISTING_CACHE.clear();
}

/* A cd and hash -r set this so the next lookup drops the cache and re-resolves,
   the way dash rehashes lazily. While it is false a hit returns the stored path
   with no stat. */
static bool PATH_CACHE_IS_STALE = false;

static ArrayList<String> PATH_COMMAND_NAMES{heap_allocator()};
static bool PATH_COMMAND_NAMES_IS_VALID = false;

static Maybe<String> MAYBE_PATH = os::get_environment_variable("PATH");

/* Defined below, declared here so the cache rebuild can split the current PATH
   into its deduplicated directories. */
static fn split_path_dirs(StringView path_var) throws -> ArrayList<String>;
static fn path_dirs() throws -> const ArrayList<String> &;

/* Append one resolved absolute path under a program name, creating the list on
   the first hit. */
static fn cache_resolved_path(StringView name, const Path &full_path,
                              os::program_extension extension, bool is_runnable,
                              bool is_bare_result, bool is_complete) throws
    -> void
{
  let &entry = PATH_CACHE.get_or_create(name, program_path_cache_entry{});
  if (is_bare_result && !entry.bare_path_position.has_value())
    entry.bare_path_position = entry.paths.count();
  entry.paths.push({full_path, extension, is_runnable});
  entry.is_complete |= is_complete;
}

static fn find_cached_program_path(const program_path_cache_entry &entry,
                                   os::program_extension wanted_extension,
                                   bool require_runnable) wontthrow
    -> const Path *
{
  if (wanted_extension == os::program_extension::None) {
    if (!entry.bare_path_position.has_value()) return nullptr;
    let const &cached = entry.paths[*entry.bare_path_position];
    if (require_runnable && !cached.is_runnable) return nullptr;
    return &cached.path;
  }

  for (let const &cached : entry.paths) {
    if (cached.extension != wanted_extension) continue;
    if (require_runnable && !cached.is_runnable) return nullptr;
    return &cached.path;
  }

  return nullptr;
}

fn read_directory_cached(const Path &directory,
                         bool should_invalidate_path_cache) throws
    -> const ArrayList<Path::directory_child> *
{
  let const key = directory.text().view();
  os::file_status status{};
  let const has_status = os::stat_path(key, status);
  const cached_directory_listing *cached = DIR_LISTING_CACHE.find(key);
  if (cached != nullptr && cached->is_valid && has_status &&
      cached->modification_time == status.modification_time &&
      cached->modification_nanoseconds == status.modification_nanoseconds &&
      cached->size == status.size)
  {
    return &cached->entries;
  }
  let const had_cached_listing = cached != nullptr;

  let entries = Path::read_directory_typed(directory);
  if (!entries.has_value()) return nullptr;

  for (let &child : *entries) {
    if (child.kind != Path::entry_kind::Unknown) continue;

    let full_path = directory.clone();
    full_path.push_component(child.name.view());
    if (full_path.is_directory())
      child.kind = Path::entry_kind::Directory;
    else if (full_path.is_regular_file())
      child.kind = Path::entry_kind::Regular;
    else
      child.kind = Path::entry_kind::Other;
  }

  cached_directory_listing fresh{};
  fresh.modification_time = has_status ? status.modification_time : 0;
  fresh.modification_nanoseconds =
      has_status ? status.modification_nanoseconds : 0;
  fresh.size = has_status ? status.size : 0;
  fresh.is_valid = has_status;
  fresh.entries = steal(*entries);
  DIR_LISTING_CACHE.set(key, steal(fresh));

  if (had_cached_listing && should_invalidate_path_cache &&
      path_dirs().find(key).has_value())
  {
    PATH_CACHE_IS_STALE = true;
    PATH_COMMAND_NAMES_IS_VALID = false;
  }

  return &DIR_LISTING_CACHE.find(key)->entries;
}

fn directory_entry_kind(const Path &directory,
                        const Path::directory_child &entry) throws
    -> Path::entry_kind
{
  if (entry.kind != Path::entry_kind::Symlink) return entry.kind;

  let full_path = directory.clone();
  full_path.push_component(entry.name.view());
  if (full_path.is_directory()) return Path::entry_kind::Directory;
  if (full_path.is_regular_file()) return Path::entry_kind::Regular;
  return Path::entry_kind::Other;
}

/* Rebuild the program cache in PATH order so the first copy of a name wins. */
static fn rebuild_path_cache() throws -> void
{
  PATH_CACHE.clear();
  PATH_CACHE_IS_STALE = false;
  PATH_COMMAND_NAMES.clear();
  PATH_COMMAND_NAMES_IS_VALID = false;
  if (!MAYBE_PATH.has_value()) {
    PATH_COMMAND_NAMES_IS_VALID = true;
    return;
  }

  let const path_dirs = split_path_dirs(*MAYBE_PATH);

  for (let const &dir_string : path_dirs) {
    let const directory = Path{dir_string.view()};
    let const entries = read_directory_cached(directory, false);
    if (entries == nullptr) continue;

    struct program_candidate
    {
      Path path;
      String normalized_name;
      os::program_name_info name_info;
      bool is_runnable{false};
    };

    let program_candidates = ArrayList<program_candidate>{heap_allocator()};
    program_candidates.reserve(entries->count());

    for (let const &entry : *entries) {
      let full_path = directory.clone();
      full_path.push_component(entry.name.view());
      if (entry.kind == Path::entry_kind::Symlink && !full_path.exists()) {
        continue;
      }
      let normalized_name = entry.name.clone();
      let const name_info = os::normalize_program_name(normalized_name);
      let const is_runnable =
          directory_entry_kind(directory, entry) == Path::entry_kind::Regular &&
          full_path.is_executable();
      program_candidates.push(
          {steal(full_path), steal(normalized_name), name_info, is_runnable});
    }

    for (let const &suffix : os::PROGRAM_SUFFIXES) {
      for (let const &program : program_candidates) {
        if (program.name_info.extension != suffix.extension) continue;

        let const stem = program.normalized_name.substring_of_length(
            0, program.name_info.stem_length);
        cache_resolved_path(stem, program.path, program.name_info.extension,
                            program.is_runnable, true, true);
        if (!program.is_runnable) continue;
        PATH_COMMAND_NAMES.push(String{program.normalized_name.view()});
        if (stem.length != program.normalized_name.length())
          PATH_COMMAND_NAMES.push(String{stem});
      }
    }
  }

  PATH_COMMAND_NAMES.sort();
  for (usize name_index = 1; name_index < PATH_COMMAND_NAMES.count();) {
    if (PATH_COMMAND_NAMES[name_index - 1].view() ==
        PATH_COMMAND_NAMES[name_index].view())
      PATH_COMMAND_NAMES.remove(name_index);
    else
      name_index++;
  }
  PATH_COMMAND_NAMES_IS_VALID = true;
}

fn clear_path_map() throws -> void
{
  LOG(Info, "clear_path_map dropping %zu cached program resolutions",
      PATH_CACHE.count());
  MAYBE_PATH = os::get_environment_variable("PATH");
  PATH_CACHE.clear();
  clear_directory_listing_cache();
  PATH_COMMAND_NAMES.clear();
  PATH_COMMAND_NAMES_IS_VALID = false;
  PATH_CACHE_IS_STALE = false;
}

fn invalidate_path_cache() throws -> void
{
  /* A cd or hash -r can change what a relative PATH entry resolves to or add a
     program to a directory, so the directory listings are dropped to force a
     fresh disk read, and PATH_CACHE is left to the stale flag, which defers its
     clear to the next lookup so a run that resolves no further command pays
     nothing. */
  LOG(Info, "invalidate_path_cache marking the program cache stale");
  clear_directory_listing_cache();
  PATH_CACHE_IS_STALE = true;
  PATH_COMMAND_NAMES.clear();
  PATH_COMMAND_NAMES_IS_VALID = false;
}

fn set_path_for_resolution(Maybe<String> path) throws -> void
{
  /* A subshell or a command substitution re-points the search at the restored
     PATH on every exit, so the common case is an unchanged value, which is a
     no-op rather than a rebuild. */
  if (MAYBE_PATH.has_value() == path.has_value() &&
      (!path.has_value() || MAYBE_PATH->view() == path->view()))
    return;

  MAYBE_PATH = steal(path);
  PATH_CACHE_IS_STALE = true;
  PATH_COMMAND_NAMES.clear();
  PATH_COMMAND_NAMES_IS_VALID = false;
}

/* Split PATH into its directory components. The last component carries no
   trailing delimiter, so a plain delimiter scan drops it and the directory is
   never searched. POSIX treats an empty component as the current directory. */
static fn split_path_dirs(StringView path_var) throws -> ArrayList<String>
{
  let dirs = ArrayList<String>{heap_allocator()};
  let current = String{heap_allocator()};

  /* A directory that appears more than once in PATH is kept only on its first
     occurrence, so the cache rebuild and the per-command resolve do not read it
     and re-resolve its files twice. The first occurrence keeps the search order
     unchanged. The directory count is small, so a linear check beats a set. */
  let const do_push_unique = [&](String dir) throws -> void {
    for (let const &seen : dirs)
      if (seen.view() == dir.view()) return;
    dirs.push(steal(dir));
  };

  for (usize i = 0; i < path_var.length; i++) {
    const char ch = path_var.data[i];
    if (ch == os::PATH_DELIMITER) {
      do_push_unique(current.is_empty() ? String{"."} : String{current.view()});
      current.clear();
    } else {
      current.push(ch);
    }
  }
  do_push_unique(current.is_empty() ? String{"."} : String{current.view()});

  return dirs;
}

/* The split PATH cached against the value it was split from, so a cold command
   resolution reuses the directory list rather than re-splitting and re-deduping
   PATH on every miss. The cache rebuilds when PATH changes, which a value
   comparison detects. */
static String CACHED_SPLIT_PATH_VALUE{heap_allocator()};
static ArrayList<String> CACHED_PATH_DIRS{heap_allocator()};
static bool CACHED_PATH_DIRS_VALID = false;

static fn path_dirs() throws -> const ArrayList<String> &
{
  if (!MAYBE_PATH.has_value()) {
    CACHED_PATH_DIRS = ArrayList<String>{heap_allocator()};
    CACHED_PATH_DIRS_VALID = false;
    return CACHED_PATH_DIRS;
  }

  if (!CACHED_PATH_DIRS_VALID ||
      CACHED_SPLIT_PATH_VALUE.view() != MAYBE_PATH->view())
  {
    CACHED_PATH_DIRS = split_path_dirs(*MAYBE_PATH);
    CACHED_SPLIT_PATH_VALUE = String{MAYBE_PATH->view()};
    CACHED_PATH_DIRS_VALID = true;
  }

  return CACHED_PATH_DIRS;
}

fn initialize_path_map() throws -> void
{
  LOG(Info, "scanning every PATH directory to seed the program cache");
  rebuild_path_cache();
}

static fn prepare_complete_path_cache() throws -> void
{
  if (PATH_CACHE_IS_STALE || !PATH_COMMAND_NAMES_IS_VALID)
    initialize_path_map();
}

fn path_command_names() throws -> const ArrayList<String> &
{
  prepare_complete_path_cache();

  return PATH_COMMAND_NAMES;
}

static pure fn path_command_name_lower_bound(const ArrayList<String> &names,
                                             StringView name) wontthrow -> usize
{
  usize lower = 0;
  usize upper = names.count();
  while (lower < upper) {
    let const middle = lower + (upper - lower) / 2;
    if (names[middle].view() < name)
      lower = middle + 1;
    else
      upper = middle;
  }

  return lower;
}

fn path_command_name_has_prefix(StringView prefix) throws -> bool
{
  let const &names = path_command_names();
  let normalized_prefix = String{prefix};
  let const name_info = os::normalize_program_name(normalized_prefix);
  let const stem =
      normalized_prefix.substring_of_length(0, name_info.stem_length);
  if (let const cached_entry = PATH_CACHE.find(stem);
      cached_entry != nullptr &&
      find_cached_program_path(*cached_entry, name_info.extension, false) !=
          nullptr &&
      find_cached_program_path(*cached_entry, name_info.extension, true) ==
          nullptr)
  {
    return false;
  }
  let const lower =
      path_command_name_lower_bound(names, normalized_prefix.view());

  return lower < names.count() &&
         names[lower].view().starts_with(normalized_prefix.view());
}

fn path_command_name_exists(StringView name) throws -> bool
{
  prepare_complete_path_cache();
  let normalized_name = String{name};
  let const name_info = os::normalize_program_name(normalized_name);
  let const stem =
      normalized_name.substring_of_length(0, name_info.stem_length);
  let const cached_entry = PATH_CACHE.find(stem);
  if (cached_entry == nullptr) return false;

  return find_cached_program_path(*cached_entry, name_info.extension, true) !=
         nullptr;
}

/* Stat dir/name along PATH until a match, the way dash stats each candidate
   once. The first hit ends the scan and is cached. With find_all the scan
   collects every match for which -a and does not write the cache, since a
   partial entry would later hide the other matches. */
static fn resolve_along_path(StringView program_name, bool find_all) throws
    -> ArrayList<Path>
{
  /* The search reads MAYBE_PATH, which the shell keeps in step with its PATH
     variable, so a plain PATH=... assignment that the store holds but the
     environment does not still drives the order. */
  if (!MAYBE_PATH.has_value()) return ArrayList<Path>{heap_allocator()};

  LOG(Debug, "statting candidates for '%.*s' along PATH%s",
      static_cast<int>(program_name.length), program_name.data,
      find_all ? ", collecting every match" : "");

  let result = ArrayList<Path>{heap_allocator()};

  /* The cache key is the program name without an omitted extension, the same
     key the lookup uses. */
  let normalized_name = String{program_name};
  let const name_info = os::normalize_program_name(normalized_name);
  let const key = normalized_name.substring_of_length(0, name_info.stem_length);

  for (let const &dir_string : path_dirs()) {
    let const directory = Path{dir_string.view()};

    let full_path = directory.clone();
    full_path.push_component(program_name);

    /* A name with an explicit extension is tried as is, while a bare name tries
       each omitted suffix in turn. */
    if (name_info.extension == os::program_extension::None) {
      for (let const &suffix : os::PROGRAM_SUFFIXES) {
        let const try_path = Path{(full_path.text() + suffix.text).view()};

        if (try_path.exists()) {
          result.push(try_path);
          if (!find_all) {
            let const is_runnable =
                try_path.is_regular_file() && try_path.is_executable();
            cache_resolved_path(key, try_path, suffix.extension, is_runnable,
                                true, false);
            return result;
          }
        }
      }
    } else if (full_path.exists()) {
      result.push(full_path);
      if (!find_all) {
        let const is_runnable =
            full_path.is_regular_file() && full_path.is_executable();
        cache_resolved_path(key, full_path, name_info.extension, is_runnable,
                            false, false);
        return result;
      }
    }
  }

  return result;
}

static fn prepare_path_cache_for_lookup() wontthrow -> void
{
  /* A cd, a PATH change, or hash -r left the cache stale, so it is dropped here
     before the lookup re-resolves against the current filesystem. */
  if (PATH_CACHE_IS_STALE) {
    LOG(Info, "search_program_path clearing stale cache before resolving");
    PATH_CACHE.clear();
    PATH_CACHE_IS_STALE = false;
    PATH_COMMAND_NAMES.clear();
    PATH_COMMAND_NAMES_IS_VALID = false;
  }
}

hot fn search_program_path(StringView program_name, bool find_all) throws
    -> ArrayList<Path>
{
  prepare_path_cache_for_lookup();

  /* which -a wants every match, so it skips the cache and scans PATH in full.
   */
  if (find_all) return resolve_along_path(program_name, true);

  let normalized_name = String{program_name};
  let const name_info = os::normalize_program_name(normalized_name);
  let const stem =
      normalized_name.substring_of_length(0, name_info.stem_length);

  if (const program_path_cache_entry *const cached = PATH_CACHE.find(stem);
      cached != nullptr)
  {
    let result = ArrayList<Path>{heap_allocator()};
    let const path =
        find_cached_program_path(*cached, name_info.extension, false);
    if (path != nullptr) {
      result.push(*path);
      return result;
    }
    if (cached->is_complete) return result;
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
  const usize a_length = a.length;
  const usize b_length = b.length;
  if (a_length > b_length ? a_length - b_length > max_distance
                          : b_length - a_length > max_distance)
    return max_distance + 1;
  if (a_length == 0) return b_length;
  if (b_length == 0) return a_length;
  /* The rolling rows are indexed up to b_length, so a candidate longer than the
     row width is rejected before the rows are reserved. */
  if (b_length + 1 > OSA_ROW_WIDTH) return max_distance + 1;

  usize previous_previous[OSA_ROW_WIDTH];
  usize previous[OSA_ROW_WIDTH];
  usize current[OSA_ROW_WIDTH];

  for (usize j = 0; j <= b_length; j++)
    previous[j] = j;
  for (usize i = 1; i <= a_length; i++) {
    current[0] = i;
    usize row_best = current[0];
    for (usize j = 1; j <= b_length; j++) {
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
    for (usize j = 0; j <= b_length; j++) {
      previous_previous[j] = previous[j];
      previous[j] = current[j];
    }
  }
  return previous[b_length];
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
  let best = String{heap_allocator()};

  /* Same length and same character multiset, so the candidate is a pure
     transposition of the typed name, the most likely typo. Used to break a tie
     in favor of git over gtf for the input gti. */
  let const do_check_anagram = [](StringView a, StringView b)
                                   wontthrow -> bool {
    if (a.length != b.length) return false;
    i32 counts[256] = {0};
    for (usize i = 0; i < a.length; i++) {
      counts[static_cast<u8>(a[i])]++;
      counts[static_cast<u8>(b[i])]--;
    }
    for (let const count : counts)
      if (count != 0) return false;
    return true;
  };

  let const do_consider = [&](StringView candidate) throws -> void {
    if (candidate.is_empty() || candidate == name) return;
    const usize distance = bounded_osa_distance(name, candidate, max_distance);
    if (distance > best_distance) return;
    const bool is_anagram = do_check_anagram(name, candidate);
    if (distance < best_distance || (is_anagram && !best_is_anagram)) {
      best_distance = distance;
      best_is_anagram = is_anagram;
      best = String{candidate};
    }
  };

  for (let const &local : local_names)
    do_consider(local.view());
  for (let const &builtin : builtin_names())
    do_consider(builtin.view());
  if (MAYBE_PATH.has_value()) {
    for (let const &dir_string : path_dirs()) {
      if (Maybe<ArrayList<String>> entries =
              Path::read_directory(Path{dir_string.view()}))
      {
        for (let const &entry : *entries)
          do_consider(entry.view());
      }
    }
  }

  if (best_distance > max_distance) return None;
  return best;
}

fn read_entire_standard_input() throws -> String
{
  let contents = String{heap_allocator()};
  char buffer[4096];
  loop
  {
    Maybe<usize> read_count = os::read_fd(SHIT_STDIN, buffer, sizeof(buffer));
    if (!read_count || *read_count == 0) break;
    contents.append(StringView{buffer, *read_count});
  }
  return contents;
}

fn read_line_from_fd(os::descriptor fd, bool &was_delimiter_terminated,
                     char delimiter, u64 deadline_nanos, bool *was_timed_out,
                     Allocator allocator) throws -> Maybe<String>
{
  let line = String{allocator};
  bool has_read_any_byte = false;
  loop
  {
    if (deadline_nanos != 0) {
      let const now_nanos = os::monotonic_nanos();
      if (now_nanos >= deadline_nanos) {
        if (was_timed_out != nullptr) *was_timed_out = true;
        break;
      }
      let const remaining_nanos_unsigned = deadline_nanos - now_nanos;
      let const remaining_nanos = static_cast<i64>(
          remaining_nanos_unsigned > static_cast<u64>(INT64_MAX)
              ? INT64_MAX
              : remaining_nanos_unsigned);
      let const readable = os::wait_for_fd_readable(fd, remaining_nanos);
      if (readable != 1) {
        if (readable == 0 && was_timed_out != nullptr) *was_timed_out = true;
        break;
      }
    }

    u8 one_byte = 0;
    Maybe<usize> read_count = os::read_fd(fd, &one_byte, 1);
    if (!read_count || *read_count == 0) break;
    has_read_any_byte = true;
    if (one_byte == static_cast<u8>(delimiter)) {
      was_delimiter_terminated = true;
      return line;
    }
    line.push(one_byte);
  }

  /* The loop fell out at end of input, so no delimiter ended the line. The read
     builtin maps an unterminated final line to a non-zero status while still
     assigning the bytes it read, the way dash does. */
  was_delimiter_terminated = false;

  if (!has_read_any_byte) return None;

  return line;
}

fn current_git_branch() throws -> String
{
  let dir = Path::current_directory();
  loop
  {
    let head = dir.clone();
    head.push_component(".git");
    /* A linked worktree or a submodule stores .git as a file holding a
       'gitdir: <path>' pointer rather than a directory, so the real git dir is
       followed before reading HEAD. */
    let git_dir = head.clone();
    if (let const dot_git = head.read_entire_file()) {
      let const pointer = dot_git->view();
      let const gitdir_prefix = StringView{"gitdir: "};
      if (pointer.starts_with(gitdir_prefix)) {
        let line = pointer.substring(gitdir_prefix.length);
        while (!line.is_empty() &&
               (line[line.length - 1] == '\n' || line[line.length - 1] == '\r'))
        {
          line = line.substring_of_length(0, line.length - 1);
        }
        let resolved_gitdir = Path{line};
        /* A relative gitdir pointer is relative to the directory holding the
           .git file, not the current directory. */
        if (!resolved_gitdir.is_absolute()) {
          resolved_gitdir = dir;
          resolved_gitdir.push_component(line);
        }
        git_dir = steal(resolved_gitdir);
      }
    }
    let git_head = git_dir.clone();
    git_head.push_component("HEAD");
    if (let const content = git_head.read_entire_file()) {
      let text = content->view();
      while (!text.is_empty() &&
             (text[text.length - 1] == '\n' || text[text.length - 1] == '\r'))
      {
        text = text.substring_of_length(0, text.length - 1);
      }
      let const ref_prefix = StringView{"ref: refs/heads/"};
      if (text.starts_with(ref_prefix))
        return String{text.substring(ref_prefix.length)};
      return String{
          text.substring_of_length(0, text.length < 7 ? text.length : 7)};
    }
    let parent = dir.clone();
    parent.push_component("..");
    let normalized = parent.to_absolute().normalized();
    if (normalized.text() == dir.text()) break;
    dir = steal(normalized);
  }
  return String{heap_allocator()};
}

} /* namespace utils */

} /* namespace shit */
