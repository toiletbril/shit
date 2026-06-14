#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Colors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#include <cmath>
#include <cstdio>

/* bench is a poop-style comparator. It samples each command repeatedly as a
   measured child, collects wall time, peak resident set, and the Linux perf
   counters per sample, then prints a per-command summary of mean, standard
   deviation, minimum, maximum, and median. With more than one command it prints
   each later command against the first as a relative speedup with an
   uncertainty. The child output is suppressed during sampling so the report
   stays clean. */

FLAG_LIST_DECL();

FLAG(bench_runs, String, '\0', "runs",
     "the exact number of samples per command");
FLAG(bench_duration, String, '\0', "duration",
     "the sampling budget per command in milliseconds");
FLAG(HELP, Bool, '\0', "help", "Display help.");

HELP_SYNOPSIS_DECL("[--runs n] [--duration ms] command [command ...]");

HELP_DESCRIPTION_DECL(
    "The bench builtin runs each command repeatedly as a measured child and "
    "prints a summary of wall time, peak resident set, and the available perf "
    "counters as mean, standard deviation, and range. With more than one "
    "command it also prints each later command against the first as a relative "
    "speedup. Sampling stops at the run count given by --runs, otherwise at "
    "the "
    "--duration budget in milliseconds.");

REGISTER_BUILTIN_FLAGS(Bench);

namespace shit {

namespace {

/* The hard ceiling on samples a single command collects, so a fast command with
   a generous duration cannot grow the buffer without bound. */
constexpr usize MAX_SAMPLES = 100000;
constexpr usize MIN_SAMPLES = 3;
constexpr u64 DEFAULT_DURATION_MILLIS = 3000;

/* The unit a metric is formatted in, picked so the printed number stays a few
   significant figures wide. */
enum class metric_unit : u8
{
  Nanoseconds,
  Bytes,
  Count,
};

/* The samples for one metric across every run of one command, kept as doubles
   so the statistics avoid integer truncation. */
struct metric_stats
{
  double mean{0};
  double std_dev{0};
  double min{0};
  double max{0};
  double median{0};
};

/* Every per-sample number bench records for one run of a command. The perf
   fields stay zero on a platform without hardware counters. */
struct bench_sample
{
  double wall_nanos{0};
  double peak_rss_bytes{0};
  double cpu_cycles{0};
  double instructions{0};
  double cache_references{0};
  double cache_misses{0};
  double branch_misses{0};
};

/* The computed statistics for one command, one metric_stats per recorded
   metric, plus the sample count and whether perf counters are present. */
struct command_result
{
  String label{};
  usize sample_count{0};
  bool has_perf{false};
  metric_stats wall_time{};
  metric_stats peak_rss{};
  metric_stats cpu_cycles{};
  metric_stats instructions{};
  metric_stats cache_references{};
  metric_stats cache_misses{};
  metric_stats branch_misses{};
};

/* A SGR escape, or an empty view when color is off, so the format strings stay
   the same whether or not color is on. */
fn colored(StringView code, bool may_color) wontthrow -> StringView
{
  return may_color ? code : StringView{};
}

/* Compute the mean, standard deviation, minimum, maximum, and median of one
   metric, reading the field selected by the accessor out of every sample. The
   median sorts a copy of the values, so the sample order is left intact for the
   next metric. */
template <typename Accessor>
fn compute_stats(const ArrayList<bench_sample> &samples,
                 Accessor accessor) throws -> metric_stats
{
  let stats = metric_stats{};
  if (samples.is_empty()) return stats;

  double sum = 0;
  double minimum = accessor(samples[0]);
  double maximum = accessor(samples[0]);
  for (usize i = 0; i < samples.count(); i++) {
    const double value = accessor(samples[i]);
    sum += value;
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
  }

  const double count = static_cast<double>(samples.count());
  stats.mean = sum / count;
  stats.min = minimum;
  stats.max = maximum;

  double variance_sum = 0;
  for (usize i = 0; i < samples.count(); i++) {
    const double delta = accessor(samples[i]) - stats.mean;
    variance_sum += delta * delta;
  }
  /* The sample standard deviation divides by n-1, the unbiased estimator poop
     uses, and degrades to zero for a single sample. */
  stats.std_dev =
      (samples.count() > 1) ? std::sqrt(variance_sum / (count - 1)) : 0.0;

  let ordered = ArrayList<double>{};
  for (usize i = 0; i < samples.count(); i++)
    ordered.push(accessor(samples[i]));
  utils::sort_ascending(ordered);
  stats.median = ordered[ordered.count() / 2];

  return stats;
}

/* Format a metric value into a short number plus a unit suffix, scaling by the
   metric's natural magnitude the way poop does so the column stays narrow. */
cold fn format_metric(double value, metric_unit unit) throws -> String
{
  double scaled = value;
  const char *suffix = "";

  switch (unit) {
  case metric_unit::Nanoseconds:
    if (value >= 1e9) {
      scaled = value / 1e9;
      suffix = "s";
    } else if (value >= 1e6) {
      scaled = value / 1e6;
      suffix = "ms";
    } else if (value >= 1e3) {
      scaled = value / 1e3;
      suffix = "us";
    } else {
      suffix = "ns";
    }
    break;
  case metric_unit::Bytes:
    if (value >= 1024.0 * 1024 * 1024) {
      scaled = value / (1024.0 * 1024 * 1024);
      suffix = "GB";
    } else if (value >= 1024.0 * 1024) {
      scaled = value / (1024.0 * 1024);
      suffix = "MB";
    } else if (value >= 1024.0) {
      scaled = value / 1024.0;
      suffix = "KB";
    } else {
      suffix = "B";
    }
    break;
  case metric_unit::Count:
    if (value >= 1e9) {
      scaled = value / 1e9;
      suffix = "G";
    } else if (value >= 1e6) {
      scaled = value / 1e6;
      suffix = "M";
    } else if (value >= 1e3) {
      scaled = value / 1e3;
      suffix = "K";
    } else {
      suffix = "";
    }
    break;
  }

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.3g%s", scaled, suffix);
  return String{buffer};
}

/* One metric row's pre-formatted numbers, kept as plain strings so the summary
   pass can measure their visible width before it decides the column padding.
   The color escapes are added at render time, not here, since they carry no
   display width and would corrupt the measurement. */
struct metric_row
{
  StringView name{};
  metric_unit unit{metric_unit::Count};
  String mean{};
  String std_dev{};
  String min{};
  String max{};
};

/* Format the four numbers of one metric row out of its statistics, leaving the
   color and the padding for the render pass. */
fn make_metric_row(StringView name, const metric_stats &stats,
                   metric_unit unit) throws -> metric_row
{
  let row = metric_row{};
  row.name = name;
  row.unit = unit;
  row.mean = format_metric(stats.mean, unit);
  row.std_dev = format_metric(stats.std_dev, unit);
  row.min = format_metric(stats.min, unit);
  row.max = format_metric(stats.max, unit);
  return row;
}

/* Append n spaces, used to pad a value column out to its measured width. */
fn append_padding(String &out, usize count) throws -> void
{
  for (usize i = 0; i < count; i++)
    out.push(' ');
}

/* One line of the summary table, the metric name, its mean and standard
   deviation, then its range, colored when allowed. The mean and the stddev are
   padded to the widths the caller measured across every row of this command, so
   the "+/-" and the "(" line up. The padding counts the visible characters of
   the value, never the zero-width color escapes that wrap the number. */
fn append_metric_line(String &out, const metric_row &row, usize mean_width,
                      usize std_dev_width, bool may_color) throws -> void
{
  out += "  ";
  out.append(row.name);
  for (usize pad = row.name.length; pad < 18; pad++)
    out.push(' ');

  out.append(colored(colors::ansi::BOLD_GREEN, may_color));
  out.append(row.mean.view());
  out.append(colored(colors::ansi::RESET, may_color));
  append_padding(out, mean_width - row.mean.length());

  out += " +/- ";
  out.append(colored(colors::ansi::BOLD_GREEN, may_color));
  out.append(row.std_dev.view());
  out.append(colored(colors::ansi::RESET, may_color));
  append_padding(out, std_dev_width - row.std_dev.length());

  out += "  (";
  out.append(colored(colors::ansi::BOLD_CYAN, may_color));
  out.append(row.min.view());
  out.append(colored(colors::ansi::RESET, may_color));
  out += " ... ";
  out.append(colored(colors::ansi::BOLD_MAGENTA, may_color));
  out.append(row.max.view());
  out.append(colored(colors::ansi::RESET, may_color));
  out += ")\n";
}

/* The relative comparison of one metric against the first command, printed as a
   speedup or slowdown with the percentage uncertainty. The ratio compares the
   means, and the uncertainty propagates the two standard deviations through the
   ratio. A near-equal pair within the uncertainty reads as roughly equal. */
fn append_relative_line(String &out, StringView name, const metric_stats &first,
                        const metric_stats &other, bool may_color) throws
    -> void
{
  out += "  ";
  out.append(name);
  for (usize pad = name.length; pad < 18; pad++)
    out.push(' ');

  if (first.mean <= 0 || other.mean <= 0) {
    out += "n/a\n";
    return;
  }

  const double ratio = other.mean / first.mean;

  /* The relative uncertainty is the sum in quadrature of the two coefficients
     of variation, scaled onto the ratio, so a noisy pair reports a wider
     band. */
  const double first_cv = first.std_dev / first.mean;
  const double other_cv = other.std_dev / other.mean;
  const double ratio_uncertainty =
      ratio * std::sqrt(first_cv * first_cv + other_cv * other_cv);

  char buffer[128];
  if (ratio > 1.0) {
    std::snprintf(buffer, sizeof(buffer), "%.2fx slower +/- %.2f", ratio,
                  ratio_uncertainty);
    out.append(colored(colors::ansi::BOLD_RED, may_color));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.2fx faster +/- %.2f", 1.0 / ratio,
                  ratio_uncertainty / (ratio * ratio));
    out.append(colored(colors::ansi::BOLD_GREEN, may_color));
  }
  out += StringView{buffer};
  out.append(colored(colors::ansi::RESET, may_color));
  out += "\n";
}

/* Parse a non-negative integer flag value, throwing a builtin error when the
   text is not a number. */
fn parse_count_flag(StringView text, StringView flag_name) throws -> u64
{
  u64 value = 0;
  bool saw_digit = false;
  for (usize i = 0; i < text.length; i++) {
    const char c = text[i];
    if (c < '0' || c > '9')
      throw Error{StringView{"--"} + flag_name + " expects a number, got '" +
                  text + "'"};
    value = value * 10 + static_cast<u64>(c - '0');
    saw_digit = true;
  }
  if (!saw_digit)
    throw Error{StringView{"--"} + flag_name + " expects a number"};
  return value;
}

/* The progress redraw interval in nanoseconds, so a fast command does not
   repaint the line every sample and flicker the terminal. */
constexpr u64 PROGRESS_INTERVAL_NANOS = 16ULL * 1000000ULL;

/* Whether bench may draw an in-progress line. It draws only when stderr is a
   terminal, so a piped or redirected run stays silent and never pollutes the
   captured result. */
fn progress_is_enabled() throws -> bool { return colors::stderr_wants_color(); }

/* Repaint the in-progress line on stderr, carriage-returning over itself so the
   percent updates in place. The percent is the fraction of the run count or the
   duration budget reached so far, clamped to 100. */
fn draw_progress(StringView command, u64 percent) throws -> void
{
  let line = String{};
  line += "\rBenchmarking '";
  line.append(command);
  line += "' ";
  line += utils::uint_to_text(percent > 100 ? 100 : percent);
  line += "%..";
  print_error(line.view());
}

/* Erase the in-progress line so the final result starts on a clean row. The
   carriage return rewinds, the spaces blank the old text, and a second return
   parks the cursor at the start of the line. */
fn clear_progress() throws -> void
{
  print_error(StringView{"\r                                        \r"});
}

/* Sample one command until either the run count is reached or the duration
   budget elapses, never below the minimum and never above the ceiling. Each
   sample is a measured child with its output suppressed. The first failed spawn
   throws so the user learns the command is wrong. A Ctrl-C sets the shared
   interrupt flag, and the loop tests it every iteration, so the sampling stops
   promptly and the caller learns through was_interrupted that it must abort. */
fn sample_command(StringView command, Maybe<u64> run_limit, u64 duration_millis,
                  bool show_progress, bool &was_interrupted) throws
    -> command_result
{
  let result = command_result{};
  result.label = command;

  /* The command string is handed to the system shell so a pipeline, a
     redirection, or a shell builtin all run as one real child. */
  let child_argv = ArrayList<String>{};
#if SHIT_PLATFORM_IS WIN32
  child_argv.push(String{"cmd"});
  child_argv.push(String{"/c"});
#else
  child_argv.push(String{"/bin/sh"});
  child_argv.push(String{"-c"});
#endif
  child_argv.push(String{command});

  let samples = ArrayList<bench_sample>{};
  const u64 duration_nanos = duration_millis * 1000000ULL;
  const u64 start_nanos = os::monotonic_nanos();
  u64 last_progress_nanos = 0;
  bool perf_seen = false;

  for (usize i = 0;; i++) {
    /* A Ctrl-C delivered between samples stops the loop before the next child
       runs, mirroring the check Expression::evaluate makes per node. */
    if (os::INTERRUPT_REQUESTED) {
      was_interrupted = true;
      break;
    }

    const bool reached_min = i >= MIN_SAMPLES;
    const bool reached_run_limit = run_limit.has_value() && i >= *run_limit;
    const u64 elapsed_nanos = os::monotonic_nanos() - start_nanos;
    const bool reached_duration =
        !run_limit.has_value() && elapsed_nanos >= duration_nanos;
    if (reached_min && (reached_run_limit || reached_duration)) break;
    if (i >= MAX_SAMPLES) break;

    /* The progress repaints on its own interval rather than every sample, so a
       fast command does not flicker the line. The percent tracks the sample
       count toward an explicit run limit, otherwise the elapsed fraction of the
       duration budget. */
    if (show_progress &&
        (elapsed_nanos - last_progress_nanos) >= PROGRESS_INTERVAL_NANOS)
    {
      last_progress_nanos = elapsed_nanos;
      u64 percent = 0;
      if (run_limit.has_value() && *run_limit > 0)
        percent = static_cast<u64>(i) * 100 / *run_limit;
      else if (duration_nanos > 0)
        percent = elapsed_nanos * 100 / duration_nanos;
      draw_progress(command, percent);
    }

    let const measured = os::run_measured(child_argv, true);
    if (!measured.has_value())
      throw Error{StringView{"could not run '"} + command + "'"};

    /* A Ctrl-C delivered while the measured child held the foreground lands
       here, so the loop breaks before the slow sample is recorded. */
    if (os::INTERRUPT_REQUESTED) {
      was_interrupted = true;
      break;
    }

    let sample = bench_sample{};
    sample.wall_nanos = static_cast<double>(measured->wall_nanos);
    sample.peak_rss_bytes = static_cast<double>(measured->peak_rss_bytes);
    if (measured->has_perf) {
      perf_seen = true;
      sample.cpu_cycles = static_cast<double>(measured->perf.cpu_cycles);
      sample.instructions = static_cast<double>(measured->perf.instructions);
      sample.cache_references =
          static_cast<double>(measured->perf.cache_references);
      sample.cache_misses = static_cast<double>(measured->perf.cache_misses);
      sample.branch_misses = static_cast<double>(measured->perf.branch_misses);
    }
    samples.push(sample);
  }

  /* The progress line is erased once sampling ends, whether it ended at the
     budget or at a Ctrl-C, so the summary or the next prompt starts clean. */
  if (show_progress) clear_progress();

  result.sample_count = samples.count();
  result.has_perf = perf_seen;
  result.wall_time = compute_stats(
      samples, [](const bench_sample &s) { return s.wall_nanos; });
  result.peak_rss = compute_stats(
      samples, [](const bench_sample &s) { return s.peak_rss_bytes; });
  result.cpu_cycles = compute_stats(
      samples, [](const bench_sample &s) { return s.cpu_cycles; });
  result.instructions = compute_stats(
      samples, [](const bench_sample &s) { return s.instructions; });
  result.cache_references = compute_stats(
      samples, [](const bench_sample &s) { return s.cache_references; });
  result.cache_misses = compute_stats(
      samples, [](const bench_sample &s) { return s.cache_misses; });
  result.branch_misses = compute_stats(
      samples, [](const bench_sample &s) { return s.branch_misses; });

  return result;
}

/* The summary block for one command, every metric on its own line under a
   heading that names the command and the sample count. */
fn append_summary(String &out, const command_result &result,
                  bool may_color) throws -> void
{
  out.append(colored(colors::ansi::BOLD, may_color));
  out += "Benchmark: ";
  out += result.label;
  out.append(colored(colors::ansi::RESET, may_color));
  out += " (" + utils::uint_to_text(result.sample_count) + " runs)\n";

  /* The rows are formatted first so their mean and stddev widths are known
     before any line is rendered, which is what lets the value columns align. */
  let rows = ArrayList<metric_row>{};
  rows.push(
      make_metric_row("wall time", result.wall_time, metric_unit::Nanoseconds));
  rows.push(make_metric_row("peak rss", result.peak_rss, metric_unit::Bytes));
  if (result.has_perf) {
    rows.push(
        make_metric_row("cpu cycles", result.cpu_cycles, metric_unit::Count));
    rows.push(make_metric_row("instructions", result.instructions,
                              metric_unit::Count));
    rows.push(make_metric_row("cache refs", result.cache_references,
                              metric_unit::Count));
    rows.push(make_metric_row("cache misses", result.cache_misses,
                              metric_unit::Count));
    rows.push(make_metric_row("branch misses", result.branch_misses,
                              metric_unit::Count));
  }

  usize mean_width = 0;
  usize std_dev_width = 0;
  for (usize i = 0; i < rows.count(); i++) {
    if (rows[i].mean.length() > mean_width) mean_width = rows[i].mean.length();
    if (rows[i].std_dev.length() > std_dev_width)
      std_dev_width = rows[i].std_dev.length();
  }

  for (usize i = 0; i < rows.count(); i++)
    append_metric_line(out, rows[i], mean_width, std_dev_width, may_color);
}

/* The relative comparison block for one later command against the first, the
   same metrics expressed as a speedup or slowdown. */
fn append_comparison(String &out, const command_result &first,
                     const command_result &other, bool may_color) throws -> void
{
  out.append(colored(colors::ansi::BOLD, may_color));
  out += "Relative to: ";
  out += first.label;
  out.append(colored(colors::ansi::RESET, may_color));
  out += "\n  ";
  out.append(colored(colors::ansi::BOLD, may_color));
  out += other.label;
  out.append(colored(colors::ansi::RESET, may_color));
  out += "\n";

  append_relative_line(out, "wall time", first.wall_time, other.wall_time,
                       may_color);
  append_relative_line(out, "peak rss", first.peak_rss, other.peak_rss,
                       may_color);
  if (first.has_perf && other.has_perf) {
    append_relative_line(out, "cpu cycles", first.cpu_cycles, other.cpu_cycles,
                         may_color);
    append_relative_line(out, "instructions", first.instructions,
                         other.instructions, may_color);
  }
}

} /* namespace */

Bench::Bench() = default;

pure fn Bench::kind() const wontthrow -> Builtin::Kind { return Kind::Bench; }

cold fn Bench::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  /* The flag parser returns the builtin name as the first operand the way the
     shell parser keeps argv[0], so the commands start at the second operand. */
  let const arguments = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (arguments.count() < 2) {
    throw Error{StringView{"no command given"}};
  }

  Maybe<u64> run_limit = None;
  if (FLAG_bench_runs.is_set())
    run_limit = parse_count_flag(FLAG_bench_runs.value(), StringView{"runs"});

  u64 duration_millis = DEFAULT_DURATION_MILLIS;
  if (FLAG_bench_duration.is_set())
    duration_millis =
        parse_count_flag(FLAG_bench_duration.value(), StringView{"duration"});

  unused(cxt);

  /* The summary table is written to stdout, so its color follows the stdout
     terminal, the same gate the diagnostics and the prompt use. */
  const bool may_color = colors::stdout_wants_color();
  const bool show_progress = progress_is_enabled();

  LOG(Debug, "bench sampling %zu commands for %llu ms each",
      arguments.count() - 1, static_cast<unsigned long long>(duration_millis));

  let results = ArrayList<command_result>{};
  for (usize i = 1; i < arguments.count(); i++) {
    bool was_interrupted = false;
    results.push(sample_command(arguments[i].view(), run_limit, duration_millis,
                                show_progress, was_interrupted));

    /* A Ctrl-C aborts the whole benchmark, not just the current command. The
       interrupt flag is cleared here, the way the shell drops it after an
       interrupted command, so the next prompt is clean, the half-built table is
       discarded, and bench returns the 130 an interrupted command returns. */
    if (was_interrupted) {
      os::INTERRUPT_REQUESTED = 0;
      return 130;
    }
  }

  let out = String{};
  for (usize i = 0; i < results.count(); i++) {
    if (i > 0) out += "\n";
    append_summary(out, results[i], may_color);
  }

  /* The relative comparison is shown only when there is a second command to
     compare against the first. */
  if (results.count() > 1) {
    out += "\n";
    for (usize i = 1; i < results.count(); i++)
      append_comparison(out, results[0], results[i], may_color);
  }

  ec.print_to_stdout(out);
  return 0;
}

} /* namespace shit */
