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

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace shit {

namespace utils {

fn merge_tokens_to_string(const ArrayList<const Token *> &tokens) throws
    -> String
{
  let result = String{};
  result.reserve(64);
  for (let const token : tokens) {
    ASSERT(token != nullptr);
    result += token->raw_string();
    if (token != tokens.back()) {
      result += ' ';
    }
  }
  return result;
}

fn execute_context(ExecContext &&ec, EvalContext &cxt, bool is_async) throws
    -> i32
{
  if (!ec.is_builtin()) {
    /* Mimicry runs a shell script in-process in the matching mode instead of
       launching the shell. A background command keeps its fork, since an
       in-process subshell cannot run in the background. */
    if (cxt.mimicry() && !is_async) {
      if (Maybe<mimic_mood> mode = ec.program_path().detect_mimic_shell();
          mode.has_value())
      {
        LOG(Debug, "execute_context mimicking the shell for '%s'",
            ec.program().c_str());
#if SHIT_PLATFORM_IS POSIX
        if (cxt.shell_is_interactive() && os::shell_has_controlling_terminal())
        {
          let const command = String{ec.program().view()};

          /* A pipe synchronizes the terminal handoff. The child blocks on it
             until the parent has called give_controlling_terminal_to, so the
             child never touches the terminal before the handoff and stops under
             TOSTOP. A failed pipe falls back to the unsynchronized handoff,
             which is benign while TOSTOP stays off. */
          let const sync_pipe = os::make_pipe();

          shit::flush();
          let const child = os::fork_job_process();
          if (os::process_id_of(child) == 0) {
            if (sync_pipe.has_value()) {
              /* The child drops its write end first, so the read sees end of
                 file and unblocks if the parent dies before the handoff rather
                 than waiting forever. */
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
        /* The terminal command the shell exits with needs no isolation, the
           same condition the replace path below uses, so its run skips the
           snapshot. */
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
      LOG(Debug,
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
      } catch (const ExecFormatError &) {
        LOG(Debug, "swallowed an exec format error, running the "
                   "file as a shell script in place");
        /* The file has no shebang and is not a binary, so it runs as a shell
           script in place, the POSIX fallback. replace_process already placed
           the redirections, so the context's descriptors are cleared to avoid
           reapplying the now-closed ones. */
        ec.in_fd.reset();
        ec.out_fd.reset();
        ec.err_fd.reset();
        const mimic_mood mode = cxt.mood();
        const bool isolated = !(cxt.terminal_exec_allowed() &&
                                !cxt.in_subshell() && !cxt.has_exit_trap());
        quit(cxt.run_mimicked_script(ec, mode, isolated), false);
      } catch (const ErrorWithLocation &error) {
        /* The program resolved but could not be executed, so the caret points
           at the command and the shell exits 126, the way bash distinguishes a
           file it found but could not run from one it never found. */
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

    /* An interactive foreground command runs in its own process group and is
       handed the controlling terminal, so it dies on its own Ctrl-C and tmux
       names the window after it rather than after the shell. A background
       command, a non-interactive run, or a pipeline keeps the shell's group.
     */
    const bool is_foreground_job = !is_async && cxt.shell_is_interactive() &&
                                   os::shell_has_controlling_terminal();

    let const command = (is_async || is_foreground_job)
                            ? String{ec.program().view()}
                            : String{};

    os::process p = SHIT_INVALID_PROCESS;
    try {
      p = os::execute_program(steal(ec), !is_async,
                              /*new_process_group=*/is_foreground_job);
    } catch (const ExecFormatError &) {
      LOG(Debug, "swallowed an exec format error, running the "
                 "file as a shell script in this process");
      /* The file has no shebang and is not a binary, so a foreground command
         runs it as a shell script in this process instead of as a child, the
         POSIX fallback. */
      const mimic_mood mode = cxt.mood();
      const bool isolated = !(cxt.terminal_exec_allowed() &&
                              !cxt.in_subshell() && !cxt.has_exit_trap());
      return cxt.run_mimicked_script(ec, mode, isolated);
    }
    if (is_async) {
      cxt.set_last_background_pid(os::process_id_of(p));
      const i32 id = cxt.register_job(p, command);
      if (cxt.shell_is_interactive())
        shit::print_error("[" + int_to_text(id) + "] " +
                          uint_to_text(static_cast<u64>(os::process_id_of(p))) +
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

  LOG(Debug, "dispatching the builtin '%s'", ec.program().c_str());
  return execute_builtin(steal(ec), cxt);
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
  let children = ArrayList<os::process>{};
  os::process last_child = SHIT_INVALID_PROCESS;
  os::descriptor last_stdin = SHIT_INVALID_FD;

  /* Each stage's status is recorded against its position, so pipefail can
     report the rightmost stage that failed and the plain case can read the last
     stage. A builtin stage yields its status at once and an external one's
     status arrives from the wait below, tracked by the parallel child-to-stage
     list. */
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

    if (ec.is_unresolved()) {
      /* The stage's command did not resolve, already reported. It runs nothing
         and closes its descriptors, so the pipe it owns gives the next stage
         EOF, and its slot carries 127 for pipefail while the last stage still
         governs the plain pipeline status. */
      stage_status[stage_index] = 127;
      ec.close_fds();
    } else if (!ec.is_builtin()) {
      let const child = os::execute_program(steal(ec));
      children.push(child);
      child_stage.push(stage_index);
      last_child = child;
    } else if (!is_last) {
#if SHIT_PLATFORM_IS POSIX
      /* A non-last builtin stage is bash's own subshell, so it forks. An
         in-process run deadlocked when the builtin filled the pipe buffer
         before its consumer started, as in seq into head. The forked child runs
         concurrently and ends through SIGPIPE on a write to a closed pipe. */
      const os::process child =
          os::fork_compound_stage(ec.in_fd, ec.out_fd, ec.err_fd);
      if (child == 0) {
        /* fork_compound_stage already placed the pipe ends on the standard
           descriptors, so the context's own descriptors are cleared and the
           builtin writes through the inherited standard output. */
        ec.in_fd = shit::None;
        ec.out_fd = shit::None;
        ec.err_fd = shit::None;
        /* make_pipe marks both ends close-on-exec, which an external stage
           drops at exec. A forked builtin never execs, so the child closes that
           read end by hand, otherwise a write never sees the reader leave. */
        if (last_stdin != SHIT_INVALID_FD) os::close_fd(last_stdin);
        cxt.set_in_pipeline_stage(true);
        cxt.enter_subshell();
        i32 child_status = 0;
        try {
          child_status = execute_builtin(steal(ec), cxt);
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
      /* The parent keeps no copy of the stage's pipe ends, otherwise the reader
         never sees the writer close. */
      ec.close_fds();
      children.push(child);
      child_stage.push(stage_index);
      last_child = child;
#else
      /* Windows has no fork, so a non-last builtin stage runs in process as
         before. A producer larger than the pipe buffer can block there. */
      cxt.set_in_pipeline_stage(true);
      defer { cxt.set_in_pipeline_stage(false); };
      ret = execute_builtin(steal(ec), cxt);
      stage_status[stage_index] = ret;
#endif
    } else {
      /* The last builtin stage runs in this process, so a read or a cd in it
         affects the shell and its status stands in for the stage. The
         pipeline-stage flag makes exec spawn a child rather than replace the
         shell, and the defer clears it even on a throw. */
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

  /* PIPESTATUS exposes each stage's status by position, so a script reads a
     non-last stage's status the way bash publishes it after a pipeline. */
  let pipe_status = ArrayList<String>{heap_allocator()};
  pipe_status.reserve(stage_count);
  for (usize i = 0; i < stage_count; i++)
    pipe_status.push(int_to_text(stage_status[i], heap_allocator()));
  cxt.set_indexed_array("PIPESTATUS", steal(pipe_status));

  /* pipefail reports the rightmost stage that failed, or zero when every stage
     succeeded. Otherwise the pipeline reports the last stage alone. */
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
  let lines = ArrayList<StringView>{};
  /* Counting the line breaks first reserves the exact size. */
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
  if (local == nullptr) return String{};

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
  /* The digits are written into a stack buffer and the result is built from the
     view in one allocation. */
  char buffer[21];
  return String{allocator, int_to_text_into(value, buffer, sizeof(buffer))};
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

fn format_time_report_custom(StringView format, double real_seconds,
                             double user_seconds, double system_seconds) throws
    -> String
{
  let report = String{};

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

    /* A precision digit and the long-format flag may precede a time conversion,
       so %3lR is three fractional digits in the minutes form. The precision is
       clamped to six, the way bash caps it. */
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
      /* The cpu busy percent prints with two decimals, the bash default, and
         takes no precision. */
      const double cpu_percent =
          real_seconds > 0.0
              ? (user_seconds + system_seconds) / real_seconds * 100.0
              : 0.0;
      std::snprintf(buffer, sizeof(buffer), "%.2f", cpu_percent);
      report += buffer;
      continue;
    }

    default:
      /* An unrecognized conversion emits the percent and the letter. */
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

static fn skip_ascii_whitespace(StringView text, usize &offset) wontthrow
    -> void
{
  while (offset < text.length && is_ascii_whitespace(text.data[offset]))
    offset++;
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
                   StringView wanted) wontthrow -> Maybe<usize>
{
  for (usize i = 0; i < suffixes.count(); i++) {
    if (suffixes[i] == wanted) return i;
  }
  return None;
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
   mask offset that slice begins at. */
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

constexpr StaticStringMap<posix_class_test>::entry POSIX_CLASS_ENTRIES[] = {
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

constexpr StaticStringMap<posix_class_test> POSIX_CLASSES{
    POSIX_CLASS_ENTRIES,
    sizeof(POSIX_CLASS_ENTRIES) / sizeof(POSIX_CLASS_ENTRIES[0])};

/* Whether the byte belongs to the named class. An unknown name matches
   nothing, the way bash treats a class it does not know. */
bool byte_is_in_posix_class(StringView class_name, u8 byte) throws
{
  if (const Maybe<posix_class_test> test = POSIX_CLASSES.find(class_name);
      test.has_value())
    return (*test)(byte);
  return false;
}

} // namespace

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
        close_scan++;
      if (close_scan < glob.count() && is_close_at(close_scan)) close_scan++;
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
static StringMap<ArrayList<Path>> PATH_CACHE{heap_allocator()};

/* The filenames of each PATH directory, keyed by the directory text, so a
   directory shared by several PATH entries or kept across a PATH change is read
   from disk only once. A PATH update rebuilds PATH_CACHE from these listings,
   reading from disk only the directories not already here. */
static StringMap<ArrayList<String>> DIR_LISTING_CACHE{heap_allocator()};

/* A cd and hash -r set this so the next lookup drops the cache and re-resolves,
   the way dash rehashes lazily. While it is false a hit returns the stored path
   with no stat. */
static bool PATH_CACHE_IS_STALE = false;

/* True once the interactive setup seeded the whole map, so a PATH change
   rebuilds it eagerly. A non-interactive run never seeds it, so a PATH change
   there only marks the cache stale and the resolver stays lazy. */
static bool PATH_MAP_IS_EAGER = false;

static ArrayList<String> PATH_COMMAND_NAMES{};
static bool PATH_COMMAND_NAMES_IS_VALID = false;

static ArrayList<String> BUILT_PATH_DIRS{};
static bool BUILT_PATH_DIRS_IS_VALID = false;

static Maybe<String> MAYBE_PATH = os::get_environment_variable("PATH");

/* Defined below, declared here so the cache rebuild can split the current PATH
   into its deduplicated directories. */
static fn split_path_dirs(StringView path_var) throws -> ArrayList<String>;

/* Append one resolved absolute path under a program name, creating the list on
   the first hit. */
static fn cache_resolved_path(StringView name, const Path &full_path) throws
    -> void
{
  PATH_CACHE.get_or_create(name, ArrayList<Path>{}).push(full_path);
}

/* The filenames in a directory, read from disk on the first request and kept
   under the directory text for later. An unreadable directory caches an empty
   listing so it is not stat-walked again until the cache is dropped. */
static fn directory_listing(const Path &directory) throws
    -> const ArrayList<String> &
{
  let const key = directory.text().view();
  if (const ArrayList<String> *cached = DIR_LISTING_CACHE.find(key))
    return *cached;
  let entries = Path::read_directory(directory);
  DIR_LISTING_CACHE.set(key, entries.has_value() ? steal(*entries)
                                                 : ArrayList<String>{});
  return *DIR_LISTING_CACHE.find(key);
}

/* Rebuild the program cache from the current PATH, in PATH order so the first
   directory's copy of a name wins. A directory whose listing is already cached
   is not read again, so a PATH change that only adds a directory reads just
   that one from disk. */
static fn path_dir_lists_equal(const ArrayList<String> &a,
                               const ArrayList<String> &b) wontthrow -> bool
{
  if (a.count() != b.count()) return false;
  for (usize i = 0; i < a.count(); i++)
    if (a[i].view() != b[i].view()) return false;
  return true;
}

static fn rebuild_path_cache() throws -> void
{
  PATH_CACHE.clear();
  PATH_CACHE_IS_STALE = false;
  PATH_COMMAND_NAMES_IS_VALID = false;
  if (!MAYBE_PATH) return;

  let const path_dirs = split_path_dirs(*MAYBE_PATH);

  BUILT_PATH_DIRS.clear();
  BUILT_PATH_DIRS.reserve(path_dirs.count());
  for (let const &dir_string : path_dirs)
    BUILT_PATH_DIRS.push(String{dir_string.view()});
  BUILT_PATH_DIRS_IS_VALID = true;

  /* A counting pass sizes the cache once so the bulk insert does not climb a
     rehash chain from sixteen slots. The directory listings cache, so the
     insert pass below reads them back without a second readdir. */
  usize total_entries = 0;
  for (let const &dir_string : path_dirs)
    total_entries += directory_listing(Path{dir_string.view()}).count();
  PATH_CACHE.reserve(total_entries);

  for (let const &dir_string : path_dirs) {
    let const directory = Path{dir_string.view()};
    for (let const &entry_name : directory_listing(directory)) {
      let name = entry_name.clone();
      os::erase_extension_and_get_its_index(name);
      let full_path = directory.clone();
      full_path.push_component(entry_name.view());
      cache_resolved_path(name.view(), full_path);
    }
  }
}

fn clear_path_map() throws -> void
{
  LOG(Info, "clear_path_map dropping %zu cached program resolutions",
      PATH_CACHE.count());
  MAYBE_PATH = os::get_environment_variable("PATH");
  PATH_CACHE.clear();
  DIR_LISTING_CACHE.clear();
  PATH_COMMAND_NAMES.clear();
  PATH_COMMAND_NAMES_IS_VALID = false;
  BUILT_PATH_DIRS.clear();
  BUILT_PATH_DIRS_IS_VALID = false;
  PATH_CACHE_IS_STALE = false;
  PATH_MAP_IS_EAGER = false;
}

fn invalidate_path_cache() throws -> void
{
  /* A cd or hash -r can change what a relative PATH entry resolves to or add a
     program to a directory, so the directory listings are dropped to force a
     fresh disk read, and PATH_CACHE is left to the stale flag, which defers its
     clear to the next lookup so a run that resolves no further command pays
     nothing. */
  LOG(Info, "invalidate_path_cache marking the program cache stale");
  DIR_LISTING_CACHE.clear();
  PATH_CACHE_IS_STALE = true;
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
  if (!PATH_MAP_IS_EAGER) {
    /* A non-interactive run never seeded the map, so it stays lazy. */
    PATH_CACHE_IS_STALE = true;
    return;
  }

  if (MAYBE_PATH.has_value() && BUILT_PATH_DIRS_IS_VALID &&
      !PATH_CACHE_IS_STALE)
  {
    let const new_dirs = split_path_dirs(*MAYBE_PATH);
    if (path_dir_lists_equal(new_dirs, BUILT_PATH_DIRS)) return;
  }

  /* The listing cache survives a PATH change, so the rebuild reads from disk
     only the directories the new PATH adds and reuses the rest. */
  LOG(Info, "set_path_for_resolution rebuilding for a changed PATH");
  rebuild_path_cache();
}

/* Split PATH into its directory components. The last component carries no
   trailing delimiter, so a plain delimiter scan drops it and the directory is
   never searched. POSIX treats an empty component as the current directory. */
static fn split_path_dirs(StringView path_var) throws -> ArrayList<String>
{
  let dirs = ArrayList<String>{};
  let current = String{};

  /* A directory that appears more than once in PATH is kept only on its first
     occurrence, so the eager scan and the per-command resolve do not read it
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
static String CACHED_SPLIT_PATH_VALUE{};
static ArrayList<String> CACHED_PATH_DIRS{};
static bool CACHED_PATH_DIRS_VALID = false;

static fn path_dirs() throws -> const ArrayList<String> &
{
  if (!MAYBE_PATH) {
    CACHED_PATH_DIRS = ArrayList<String>{};
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
  /* The interactive setup seeds the whole map once, so a later PATH change
     rebuilds it eagerly while reading only the directories the change adds. */
  PATH_MAP_IS_EAGER = true;
  rebuild_path_cache();
}

fn path_command_names() throws -> const ArrayList<String> &
{
  if (!PATH_MAP_IS_EAGER)
    initialize_path_map();
  else if (PATH_CACHE_IS_STALE)
    rebuild_path_cache();

  if (!PATH_COMMAND_NAMES_IS_VALID) {
    PATH_COMMAND_NAMES.clear();
    PATH_COMMAND_NAMES.reserve(PATH_CACHE.count());
    PATH_CACHE.for_each([](StringView name, const ArrayList<Path> &paths) {
      unused(paths);
      PATH_COMMAND_NAMES.push(String{name});
    });
    PATH_COMMAND_NAMES_IS_VALID = true;
  }

  return PATH_COMMAND_NAMES;
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
  if (!MAYBE_PATH) return ArrayList<Path>{};

  LOG(Debug, "statting candidates for '%.*s' along PATH%s",
      static_cast<int>(program_name.length), program_name.data,
      find_all ? ", collecting every match" : "");

  let result = ArrayList<Path>{};

  /* The cache key is the program name without an omitted extension, the same
     key the lookup uses. */
  let key = String{program_name};
  os::erase_extension_and_get_its_index(key);

  for (let const &dir_string : path_dirs()) {
    let const directory = Path{dir_string.view()};

    let full_path = directory.clone();
    full_path.push_component(program_name);
    let full_path_str = full_path.text().clone();

    /* A name with an explicit extension is tried as is, while a bare name tries
       each omitted suffix in turn. */
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
    LOG(Info,
        "search_program_path clearing stale cache before resolving '%.*s'",
        (int) program_name.length, program_name.data);
    PATH_CACHE.clear();
    PATH_CACHE_IS_STALE = false;
  }

  let program_name_without_extension = String{program_name};

  const os::ext_index typed_extension =
      os::erase_extension_and_get_its_index(program_name_without_extension);

  /* which -a wants every match, so it skips the cache and scans PATH in full.
   */
  if (find_all) return resolve_along_path(program_name, true);

  /* A name typed with an explicit extension is matched exactly by the search,
     so the extension-stripped cache key would resolve the wrong file. The cache
     is consulted only when no extension was typed, which on POSIX is always. A
     hit returns the stored absolute path with no stat, the way dash returns a
     hashed location. */
  if (typed_extension == 0) {
    if (const ArrayList<Path> *const cached =
            PATH_CACHE.find(program_name_without_extension.view());
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
  let best = String{};

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
  if (MAYBE_PATH) {
    for (let const &dir_string : split_path_dirs(*MAYBE_PATH)) {
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
  let contents = String{};
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
                     char delimiter) throws -> Maybe<String>
{
  let line = String{};
  bool has_read_any_byte = false;
  loop
  {
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
  return String{};
}

} // namespace utils

} // namespace shit
