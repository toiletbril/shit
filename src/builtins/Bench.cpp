#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Colors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#include <cmath>
#include <cstdio>

FLAG_LIST_DECL();

FLAG(bench_runs, String, '\0', "runs",
     "The exact number of samples per command.");
FLAG(bench_duration, String, '\0', "duration",
     "The sampling budget per command in milliseconds.");
FLAG(BENCH_IGNORE_EXIT, Bool, '\0', "ignore-exit-code",
     "Keep sampling even when the command returns a non-zero exit code.");
FLAG(BENCH_SHOW_OUTPUT, Bool, '\0', "show-output",
     "Show the command's output instead of suppressing it.");
FLAG(BENCH_NO_SHELL, Bool, '\0', "no-shell",
     "Fork the command directly, without wrapping it in a shell.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

HELP_SYNOPSIS_DECL("[--runs n] [--duration ms] [--ignore-exit-code] "
                   "[--show-output] [--no-shell] command [command ...]");

HELP_DESCRIPTION_DECL(
    "The bench builtin measures and compares the runtime of each command.");

REGISTER_BUILTIN_FLAGS(Bench);

namespace shit {

namespace {

constexpr usize MAX_SAMPLES = 100000;
constexpr usize MIN_SAMPLES = 3;
constexpr u64 DEFAULT_DURATION_MILLIS = 3000;

enum class metric_unit : u8
{
  Nanoseconds,
  Bytes,
  Count,
};

struct metric_stats
{
  double mean{0};
  double std_dev{0};
  double min{0};
  double max{0};
};

/* The perf fields stay zero on a platform without hardware counters. */
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

struct command_result
{
  explicit command_result(Allocator allocator) : label(allocator) {}
  String label;
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

fn colored(StringView code, bool should_color) wontthrow -> StringView
{
  return should_color ? code : StringView{};
}

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

  return stats;
}

cold fn format_metric(double value, metric_unit unit,
                      Allocator allocator) throws -> String
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
  return String{allocator, buffer};
}

/* The color escapes are added at render time, not here, since they carry no
   display width and would corrupt the width measurement. */
struct metric_row
{
  explicit metric_row(Allocator allocator)
      : mean(allocator), std_dev(allocator), min(allocator), max(allocator)
  {}
  StringView name{};
  metric_unit unit{metric_unit::Count};
  String mean;
  String std_dev;
  String min;
  String max;
};

fn make_metric_row(StringView name, const metric_stats &stats, metric_unit unit,
                   Allocator allocator) throws -> metric_row
{
  let row = metric_row{allocator};
  row.name = name;
  row.unit = unit;
  row.mean = format_metric(stats.mean, unit, allocator);
  row.std_dev = format_metric(stats.std_dev, unit, allocator);
  row.min = format_metric(stats.min, unit, allocator);
  row.max = format_metric(stats.max, unit, allocator);
  return row;
}

fn append_padding(String &out, usize count) throws -> void
{
  for (usize i = 0; i < count; i++)
    out.push(' ');
}

fn append_metric_line(String &out, const metric_row &row, usize mean_width,
                      usize std_dev_width, bool should_color) throws -> void
{
  out += "  ";
  out.append(row.name);
  for (usize pad = row.name.length; pad < 18; pad++)
    out.push(' ');

  out.append(colored(colors::ansi::BOLD_GREEN, should_color));
  out.append(row.mean.view());
  out.append(colored(colors::ansi::RESET, should_color));
  append_padding(out, mean_width - row.mean.length());

  out += " +/- ";
  out.append(colored(colors::ansi::BOLD_GREEN, should_color));
  out.append(row.std_dev.view());
  out.append(colored(colors::ansi::RESET, should_color));
  append_padding(out, std_dev_width - row.std_dev.length());

  out += "  (";
  out.append(colored(colors::ansi::BOLD_CYAN, should_color));
  out.append(row.min.view());
  out.append(colored(colors::ansi::RESET, should_color));
  out += " ... ";
  out.append(colored(colors::ansi::BOLD_MAGENTA, should_color));
  out.append(row.max.view());
  out.append(colored(colors::ansi::RESET, should_color));
  out += ")\n";
}

fn append_relative_line(String &out, StringView name, const metric_stats &first,
                        const metric_stats &other, bool should_color) throws
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
    out.append(colored(colors::ansi::BOLD_RED, should_color));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.2fx faster +/- %.2f", 1.0 / ratio,
                  ratio_uncertainty / (ratio * ratio));
    out.append(colored(colors::ansi::BOLD_GREEN, should_color));
  }
  out += StringView{buffer};
  out.append(colored(colors::ansi::RESET, should_color));
  out += "\n";
}

fn parse_count_flag(StringView text, StringView flag_name) throws -> u64
{
  u64 value = 0;
  bool has_seen_digit = false;
  for (usize i = 0; i < text.length; i++) {
    const char c = text[i];
    if (c < '0' || c > '9') {
      throw Error{StringView{"--"} + flag_name + " expects a number, got '" +
                  text + "'"};
    }
    value = value * 10 + static_cast<u64>(c - '0');
    has_seen_digit = true;
  }

  if (!has_seen_digit)
    throw Error{StringView{"--"} + flag_name + " expects a number"};

  return value;
}

constexpr u64 PROGRESS_INTERVAL_NANOS = 16ULL * 1000000ULL;

fn progress_is_enabled() throws -> bool { return colors::stderr_wants_color(); }

fn draw_progress(StringView command, u64 percent, Allocator allocator) throws
    -> void
{
  let line = String{allocator};
  line += "\rBenchmarking '";
  line.append(command);
  line += "' ";
  line += String::from(percent > 100 ? 100 : percent, allocator);
  line += "%..";
  print_error(line.view());
}

fn clear_progress() throws -> void
{
  print_error(StringView{"\r                                        \r"});
}

fn sample_command(StringView shell_binary, StringView command,
                  Maybe<u64> run_limit, u64 duration_millis,
                  bool should_show_progress, bool should_suppress_output,
                  bool should_ignore_exit_code, bool should_use_shell,
                  bool &was_interrupted, bool &did_command_fail,
                  i64 &failure_status, Allocator allocator) throws
    -> command_result
{
  let result = command_result{allocator};
  result.label = command;

  let child_argv = ArrayList<String>{allocator};
  if (should_use_shell) {
    child_argv.push(String{allocator, shell_binary});
    child_argv.push(String{allocator, "-c"});
    child_argv.push(String{allocator, command});
  } else {
    usize start = 0;
    for (usize i = 0; i <= command.length; i++) {
      let const at_end = i == command.length;
      if (at_end || command[i] == ' ' || command[i] == '\t') {
        if (i > start)
          child_argv.push(
              String{allocator, command.substring_of_length(start, i - start)});
        start = i + 1;
      }
    }

    if (child_argv.is_empty())
      throw Error{StringView{"bench --no-shell needs a command word in '"} +
                  command + "'"};
  }

  let samples = ArrayList<bench_sample>{allocator};
  const u64 duration_nanos = duration_millis * 1000000ULL;
  const u64 start_nanos = os::monotonic_nanos();
  u64 last_progress_nanos = 0;
  bool has_perf = false;

  for (usize i = 0;; i++) {
    if (os::INTERRUPT_REQUESTED) {
      was_interrupted = true;
      break;
    }

    const bool has_reached_min = i >= MIN_SAMPLES;
    const bool has_reached_run_limit = run_limit.has_value() && i >= *run_limit;
    const u64 elapsed_nanos = os::monotonic_nanos() - start_nanos;
    const bool has_reached_duration =
        !run_limit.has_value() && elapsed_nanos >= duration_nanos;

    if (has_reached_min && (has_reached_run_limit || has_reached_duration)) {
      break;
    }
    if (i >= MAX_SAMPLES) break;

    if (should_show_progress &&
        (elapsed_nanos - last_progress_nanos) >= PROGRESS_INTERVAL_NANOS)
    {
      last_progress_nanos = elapsed_nanos;
      u64 percent = 0;
      if (run_limit.has_value() && *run_limit > 0) {
        percent = static_cast<u64>(i) * 100 / *run_limit;
      } else if (duration_nanos > 0) {
        percent = elapsed_nanos * 100 / duration_nanos;
      }
      draw_progress(command, percent, allocator);
    }

    let const measured = os::run_measured(child_argv, should_suppress_output);
    if (!measured.has_value())
      throw Error{StringView{"Unable to run '"} + command +
                  "': " + os::last_system_error_message()};

    if (os::INTERRUPT_REQUESTED) {
      was_interrupted = true;
      break;
    }

    if (measured->exit_status != 0 && !should_ignore_exit_code) {
      did_command_fail = true;
      failure_status = measured->exit_status;
      break;
    }

    let sample = bench_sample{};
    sample.wall_nanos = static_cast<double>(measured->wall_nanos);
    sample.peak_rss_bytes = static_cast<double>(measured->peak_rss_bytes);
    if (measured->has_perf) {
      has_perf = true;
      sample.cpu_cycles = static_cast<double>(measured->perf.cpu_cycles);
      sample.instructions = static_cast<double>(measured->perf.instructions);
      sample.cache_references =
          static_cast<double>(measured->perf.cache_references);
      sample.cache_misses = static_cast<double>(measured->perf.cache_misses);
      sample.branch_misses = static_cast<double>(measured->perf.branch_misses);
    }
    samples.push(sample);
  }

  if (should_show_progress) clear_progress();

  result.sample_count = samples.count();
  result.has_perf = has_perf;
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

fn append_summary(String &out, const command_result &result, bool should_color,
                  Allocator allocator) throws -> void
{
  out.append(colored(colors::ansi::BOLD, should_color));
  out += "Benchmark: ";
  out += result.label;
  out.append(colored(colors::ansi::RESET, should_color));
  out += " (" + String::from(result.sample_count, allocator) + " runs)\n";

  let rows = ArrayList<metric_row>{allocator};
  rows.push(make_metric_row("wall time", result.wall_time,
                            metric_unit::Nanoseconds, allocator));
  rows.push(make_metric_row("peak rss", result.peak_rss, metric_unit::Bytes,
                            allocator));
  if (result.has_perf) {
    rows.push(make_metric_row("cpu cycles", result.cpu_cycles,
                              metric_unit::Count, allocator));
    rows.push(make_metric_row("instructions", result.instructions,
                              metric_unit::Count, allocator));
    rows.push(make_metric_row("cache refs", result.cache_references,
                              metric_unit::Count, allocator));
    rows.push(make_metric_row("cache misses", result.cache_misses,
                              metric_unit::Count, allocator));
    rows.push(make_metric_row("branch misses", result.branch_misses,
                              metric_unit::Count, allocator));
  }

  usize mean_width = 0;
  usize std_dev_width = 0;
  for (usize i = 0; i < rows.count(); i++) {
    if (rows[i].mean.length() > mean_width) mean_width = rows[i].mean.length();
    if (rows[i].std_dev.length() > std_dev_width)
      std_dev_width = rows[i].std_dev.length();
  }

  for (usize i = 0; i < rows.count(); i++)
    append_metric_line(out, rows[i], mean_width, std_dev_width, should_color);
}

fn append_comparison(String &out, const command_result &first,
                     const command_result &other, bool should_color) throws
    -> void
{
  out.append(colored(colors::ansi::BOLD, should_color));
  out += "Relative to: ";
  out += first.label;
  out.append(colored(colors::ansi::RESET, should_color));
  out += "\n  ";
  out.append(colored(colors::ansi::BOLD, should_color));
  out += other.label;
  out.append(colored(colors::ansi::RESET, should_color));
  out += "\n";

  append_relative_line(out, "wall time", first.wall_time, other.wall_time,
                       should_color);
  append_relative_line(out, "peak rss", first.peak_rss, other.peak_rss,
                       should_color);
  if (first.has_perf && other.has_perf) {
    append_relative_line(out, "cpu cycles", first.cpu_cycles, other.cpu_cycles,
                         should_color);
    append_relative_line(out, "instructions", first.instructions,
                         other.instructions, should_color);
  }
}

} // namespace

Bench::Bench() = default;

pure fn Bench::kind() const wontthrow -> Builtin::Kind { return Kind::Bench; }

cold fn Bench::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  /* The flag parser keeps argv[0], so the commands start at index 1. */
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const arguments = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (arguments.count() < 2) {
    throw Error{StringView{"No command given"}};
  }

  Maybe<u64> run_limit = None;
  if (FLAG_bench_runs.is_set())
    run_limit = parse_count_flag(FLAG_bench_runs.value(), StringView{"runs"});

  u64 duration_millis = DEFAULT_DURATION_MILLIS;
  if (FLAG_bench_duration.is_set())
    duration_millis =
        parse_count_flag(FLAG_bench_duration.value(), StringView{"duration"});

  let const should_color = colors::stdout_wants_color();
  let const should_show_progress = progress_is_enabled();
  let const should_ignore_exit_code = FLAG_BENCH_IGNORE_EXIT.is_enabled();
  let const should_suppress_output = !FLAG_BENCH_SHOW_OUTPUT.is_enabled();
  let const should_use_shell = !FLAG_BENCH_NO_SHELL.is_enabled();

  let shell_binary =
      String{cxt.scratch_allocator(), cxt.shell_executable_path()};
  if (shell_binary.is_empty()) {
    if (let const detected = os::current_executable_path())
      shell_binary = String{cxt.scratch_allocator(), detected->view()};
  }
  if (should_use_shell && shell_binary.is_empty())
    throw Error{
        StringView{"bench cannot find the shit binary to run a sample"}};

  LOG(Debug, "bench sampling %zu commands for %llu ms each",
      arguments.count() - 1, static_cast<unsigned long long>(duration_millis));

  let results = ArrayList<command_result>{cxt.scratch_allocator()};
  for (usize i = 1; i < arguments.count(); i++) {
    bool was_interrupted = false;
    bool did_command_fail = false;
    i64 failure_status = 0;
    results.push(sample_command(
        shell_binary.view(), arguments[i].view(), run_limit, duration_millis,
        should_show_progress, should_suppress_output, should_ignore_exit_code,
        should_use_shell, was_interrupted, did_command_fail, failure_status,
        cxt.scratch_allocator()));

    if (was_interrupted) {
      os::INTERRUPT_REQUESTED = 0;
      return 130;
    }

    if (did_command_fail) {
      if (should_show_progress) clear_progress();
      let const operand_location =
          i < operand_locations.count() ? operand_locations[i]
                                        : ec.source_location();
      report_soft_builtin_error(
          ec, cxt, operand_location,
          StringView{"the command '"} + arguments[i].view() +
              "' exited with status " +
              String::from(failure_status, cxt.scratch_allocator()),
          "Pass `--ignore-exit-code` to benchmark it regardless");
      return static_cast<i32>(failure_status);
    }
  }

  let out = String{cxt.scratch_allocator()};
  for (usize i = 0; i < results.count(); i++) {
    if (i > 0) out += "\n";
    append_summary(out, results[i], should_color, cxt.scratch_allocator());
  }

  if (results.count() > 1) {
    out += "\n";
    for (usize i = 1; i < results.count(); i++)
      append_comparison(out, results[0], results[i], should_color);
  }

  ec.print_to_stdout(out);
  return 0;
}

} // namespace shit
