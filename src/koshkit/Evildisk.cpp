/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements evildisk. It reports filesystem capacity, disk failure
 * counters, and available SMART data for mounted filesystems or named paths.
 */

#include "../Cli.hpp"
#include "../CliColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] [--color when] [file ...]");

HELP_DESCRIPTION_DECL(
    "The evildisk utility reports filesystem capacity and disk health data.");

FLAG(EVILDISK_ALL, Bool, 'a', "all", "Show available SMART data.");
FLAG(EVILDISK_COLOR, String, '\0', "color",
     "Set color output to always, auto, or never.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Evildisk);

namespace koshka::koshkit {

namespace {

struct disk_row
{
  String source{heap_allocator()};
  String type{heap_allocator()};
  String size{heap_allocator()};
  String used{heap_allocator()};
  String available{heap_allocator()};
  String use{heap_allocator()};
  String target{heap_allocator()};
  u64 use_percent{0};
};

struct smart_row
{
  String device{heap_allocator()};
  String status{heap_allocator()};
  String model{heap_allocator()};
  String protocol{heap_allocator()};
};

fn usage_style(u64 percent) wontthrow -> StringView
{
  if (percent >= 90) return colors::ansi::BOLD_RED;
  if (percent >= 75) return colors::ansi::BOLD_YELLOW;
  return colors::ansi::BOLD_GREEN;
}

fn report_value(StringView report, const StringView *names, usize name_count,
                Allocator allocator) throws -> String
{
  usize position = 0;
  while (position < report.length) {
    let const line = report.next_line(position).trim_blanks();
    for (usize index = 0; index < name_count; index++) {
      if (!line.starts_with(names[index])) continue;

      let value = line.substring(names[index].length).trim_blanks();
      if (!value.is_empty() && value[0] == ':')
        value = value.substring(1).trim_blanks();
      if (!value.is_empty()) return String{allocator, value};
    }
  }

  return String{allocator};
}

fn read_smart_rows(EvalContext &cxt,
                   const ArrayList<os::mounted_filesystem> &filesystems,
                   Allocator allocator) throws -> ArrayList<smart_row>
{
  let rows = ArrayList<smart_row>{allocator};
  let const smartctl = resolve_util_program(cxt, "smartctl");
#if defined __APPLE__
  let const diskutil = resolve_util_program(cxt, "diskutil");
#endif
  static constexpr StringView DEVICE_NAMES[] = {"Device Identifier"};
  static constexpr StringView STATUS_NAMES[] = {
      "SMART Status", "SMART overall-health self-assessment test result",
      "SMART Health Status", "Critical Warning"};
  static constexpr StringView MODEL_NAMES[] = {"Device / Media Name",
                                               "Device Model", "Model Number"};
  static constexpr StringView PROTOCOL_NAMES[] = {"Protocol",
                                                  "Transport protocol"};

  for (let const &filesystem : filesystems) {
    let report = Maybe<String>{};
    if (smartctl.has_value() && filesystem.source.starts_with("/dev/")) {
      let arguments = ArrayList<String>{heap_allocator()};
      arguments.push(String{"-H"});
      arguments.push(filesystem.source.clone());
      report = capture_util_program_output(*smartctl, steal(arguments),
                                           10'000'000'000);
    }
#if defined __APPLE__
    else if (diskutil.has_value())
    {
      let arguments = ArrayList<String>{heap_allocator()};
      arguments.push(String{"info"});
      arguments.push(filesystem.target.clone());
      report = capture_util_program_output(*diskutil, steal(arguments),
                                           10'000'000'000);
    }
#endif
    if (!report.has_value()) continue;

    let row = smart_row{};
    row.device = report_value(report->view(), DEVICE_NAMES,
                              countof(DEVICE_NAMES), allocator);
    if (row.device.is_empty()) row.device = filesystem.source.clone();
    row.status = report_value(report->view(), STATUS_NAMES,
                              countof(STATUS_NAMES), allocator);
    if (row.status.is_empty()) continue;
    row.model = report_value(report->view(), MODEL_NAMES, countof(MODEL_NAMES),
                             allocator);
    if (row.model.is_empty()) row.model = String{allocator, "-"};
    row.protocol = report_value(report->view(), PROTOCOL_NAMES,
                                countof(PROTOCOL_NAMES), allocator);
    if (row.protocol.is_empty()) row.protocol = String{allocator, "-"};

    bool is_duplicate = false;
    for (let const &existing : rows) {
      if (existing.device != row.device) continue;
      is_duplicate = true;
      break;
    }
    if (!is_duplicate) rows.push(steal(row));
  }

  return rows;
}

}

Evildisk::Evildisk() = default;

pure fn Evildisk::kind() const wontthrow -> Utility::Kind
{
  return Kind::Evildisk;
}

fn Evildisk::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args, &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  cli_color_mode color_mode = cli_color_mode::Auto;
  if (FLAG_EVILDISK_COLOR.is_set()) {
    let const parsed = parse_cli_color_mode(FLAG_EVILDISK_COLOR.value());
    if (!parsed.has_value()) {
      report_soft_koshkit_error(ec, cxt, "evildisk: invalid color mode",
                                "use always, auto, or never");
      return 1;
    }
    color_mode = *parsed;
  }
  let const should_color = stdout_wants_color(color_mode);
  let const allocator = cxt.scratch_allocator();

  let filesystems = ArrayList<os::mounted_filesystem>{allocator};
  if (operands.is_empty()) {
    filesystems = os::mounted_filesystems();
    filesystems.sort([](const os::mounted_filesystem &left,
                        const os::mounted_filesystem &right) {
      if (left.target != right.target) return left.target < right.target;
      return left.source < right.source;
    });
  } else {
    filesystems.reserve(operands.count());
    for (let const &operand : operands) {
      filesystems.push(os::mounted_filesystem{operand.clone(), operand.clone(),
                                              String{allocator},
                                              String{allocator}});
    }
  }

  let rows = ArrayList<disk_row>{allocator};
  rows.reserve(filesystems.count());
  i32 status = 0;
  for (let const &mounted : filesystems) {
    os::filesystem_status filesystem{};
    if (!os::stat_filesystem(mounted.target.view(), filesystem)) {
      report_soft_koshkit_error(ec, cxt,
                                "evildisk: cannot read '" + mounted.target +
                                    "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    let const total = scaled_filesystem_blocks(
        filesystem.total_blocks, filesystem.fundamental_block_size, 1);
    let const free = scaled_filesystem_blocks(
        filesystem.free_blocks, filesystem.fundamental_block_size, 1);
    let const available = scaled_filesystem_blocks(
        filesystem.available_blocks, filesystem.fundamental_block_size, 1);
    let const used = total > free ? total - free : 0;
    let const use_percent = filesystem_usage_percent(used, available);
    let row = disk_row{};
    row.source = String{allocator, mounted.source.view()};
    row.type = mounted.type.is_empty() ? String{allocator, "-"}
                                       : String{allocator, mounted.type.view()};
    row.size = format_human_size(total, allocator);
    row.used = format_human_size(used, allocator);
    row.available = format_human_size(available, allocator);
    row.use = String::from(use_percent, allocator) + "%";
    row.target = String{allocator, mounted.target.view()};
    row.use_percent = use_percent;
    rows.push(steal(row));
  }

  usize source_width = 10;
  usize type_width = 4;
  usize size_width = 4;
  usize used_width = 4;
  usize available_width = 9;
  usize use_width = 3;
  for (let const &row : rows) {
    if (row.source.length() > source_width) source_width = row.source.length();
    if (row.type.length() > type_width) type_width = row.type.length();
    if (row.size.length() > size_width) size_width = row.size.length();
    if (row.used.length() > used_width) used_width = row.used.length();
    if (row.available.length() > available_width) {
      available_width = row.available.length();
    }
    if (row.use.length() > use_width) use_width = row.use.length();
  }

  let output = String{allocator};
  append_report_text(output, "DISKS", colors::ansi::BOLD_BLUE, should_color);
  output += "\n  ";
  append_report_column(output, "FILESYSTEM", source_width, false,
                       colors::ansi::BOLD_CYAN, should_color);
  output += "  ";
  append_report_column(output, "TYPE", type_width, false,
                       colors::ansi::BOLD_CYAN, should_color);
  output += "  ";
  append_report_column(output, "SIZE", size_width, true,
                       colors::ansi::BOLD_CYAN, should_color);
  output += "  ";
  append_report_column(output, "USED", used_width, true,
                       colors::ansi::BOLD_CYAN, should_color);
  output += "  ";
  append_report_column(output, "AVAILABLE", available_width, true,
                       colors::ansi::BOLD_CYAN, should_color);
  output += "  ";
  append_report_column(output, "USE", use_width, true, colors::ansi::BOLD_CYAN,
                       should_color);
  output += "  ";
  append_report_text(output, "MOUNT", colors::ansi::BOLD_CYAN, should_color);
  output += "\n";

  for (let const &row : rows) {
    output += "  ";
    append_report_column(output, row.source.view(), source_width, false,
                         colors::ansi::GREEN, should_color);
    output += "  ";
    append_report_column(output, row.type.view(), type_width, false,
                         colors::ansi::BOLD_MAGENTA, should_color);
    output += "  ";
    append_report_column(output, row.size.view(), size_width, true,
                         colors::ansi::CYAN, should_color);
    output += "  ";
    append_report_column(output, row.used.view(), used_width, true,
                         colors::ansi::CYAN, should_color);
    output += "  ";
    append_report_column(output, row.available.view(), available_width, true,
                         colors::ansi::CYAN, should_color);
    output += "  ";
    append_report_column(output, row.use.view(), use_width, true,
                         usage_style(row.use_percent), should_color);
    output += "  ";
    append_report_text(output, row.target.view(), colors::ansi::BOLD_GREEN,
                       should_color);
    output += "\n";
  }

  let disk_snapshot = os::read_disk_io_snapshot(allocator);
  disk_snapshot.disks.sort(
      [](const os::disk_io_status &left, const os::disk_io_status &right) {
        return left.name < right.name;
      });
  bool has_failure_counters = false;
  for (let const &disk : disk_snapshot.disks) {
    if (disk.has_field(os::disk_io_field::ReadErrors) ||
        disk.has_field(os::disk_io_field::WriteErrors) ||
        disk.has_field(os::disk_io_field::ReadRetries) ||
        disk.has_field(os::disk_io_field::WriteRetries))
    {
      has_failure_counters = true;
      break;
    }
  }

  output += "\n";
  append_report_text(output, "FAILURES", colors::ansi::BOLD_BLUE, should_color);
  output += "\n";
  if (!has_failure_counters) {
    let body = String{allocator};
    append_report_field(body, "Status", "unavailable", colors::ansi::BOLD_CYAN,
                        should_color);
    append_report_body(output, body.view());
  } else {
    output += "  ";
    append_report_column(output, "DEVICE", 16, false, colors::ansi::BOLD_CYAN,
                         should_color);
    constexpr StringView HEADERS[] = {
        "READ ERRORS",
        "WRITE ERRORS",
        "READ RETRIES",
        "WRITE RETRIES",
    };
    constexpr usize WIDTHS[] = {11, 12, 12, 13};
    for (usize index = 0; index < countof(HEADERS); index++) {
      output += "  ";
      append_report_column(output, HEADERS[index], WIDTHS[index], true,
                           colors::ansi::BOLD_CYAN, should_color);
    }
    output += "\n";
    for (let const &disk : disk_snapshot.disks) {
      const u64 counters[] = {
          disk.read_error_count,
          disk.write_error_count,
          disk.read_retry_count,
          disk.write_retry_count,
      };
      constexpr os::disk_io_field FIELDS[] = {
          os::disk_io_field::ReadErrors,
          os::disk_io_field::WriteErrors,
          os::disk_io_field::ReadRetries,
          os::disk_io_field::WriteRetries,
      };
      output += "  ";
      append_report_column(output, disk.name.view(), 16, false,
                           colors::ansi::BOLD_GREEN, should_color);
      for (usize index = 0; index < countof(counters); index++) {
        output += "  ";
        append_report_column(
            output,
            disk.has_field(FIELDS[index])
                ? String::from(counters[index], allocator).view()
                : StringView{"-"},
            WIDTHS[index], true,
            counters[index] == 0 ? colors::ansi::GREEN : colors::ansi::BOLD_RED,
            should_color);
      }
      output += "\n";
    }
  }

  if (FLAG_EVILDISK_ALL.is_enabled()) {
    let const smart_rows = read_smart_rows(cxt, filesystems, allocator);
    output += "\n";
    append_report_text(output, "SMART", colors::ansi::BOLD_BLUE, should_color);
    output += "\n";
    if (smart_rows.is_empty()) {
      let body = String{allocator};
      append_report_field(body, "Status", "unavailable",
                          colors::ansi::BOLD_CYAN, should_color);
      append_report_body(output, body.view());
    } else {
      usize device_width = 6;
      usize status_width = 6;
      usize model_width = 5;
      for (let const &row : smart_rows) {
        if (row.device.length() > device_width)
          device_width = row.device.length();
        if (row.status.length() > status_width)
          status_width = row.status.length();
        if (row.model.length() > model_width) model_width = row.model.length();
      }

      output += "  ";
      append_report_column(output, "DEVICE", device_width, false,
                           colors::ansi::BOLD_CYAN, should_color);
      output += "  ";
      append_report_column(output, "STATUS", status_width, false,
                           colors::ansi::BOLD_CYAN, should_color);
      output += "  ";
      append_report_column(output, "MODEL", model_width, false,
                           colors::ansi::BOLD_CYAN, should_color);
      output += "  ";
      append_report_text(output, "PROTOCOL", colors::ansi::BOLD_CYAN,
                         should_color);
      output += "\n";
      for (let const &row : smart_rows) {
        output += "  ";
        append_report_column(output, row.device.view(), device_width, false,
                             colors::ansi::BOLD_GREEN, should_color);
        output += "  ";
        let const status_style =
            row.status == "Verified" || row.status == "PASSED" ||
                    row.status == "0x00"
                ? colors::ansi::BOLD_GREEN
                : colors::ansi::BOLD_YELLOW;
        append_report_column(output, row.status.view(), status_width, false,
                             status_style, should_color);
        output += "  ";
        append_report_column(output, row.model.view(), model_width, false, {},
                             should_color);
        output += "  ";
        append_report_text(output, row.protocol.view(),
                           colors::ansi::BOLD_MAGENTA, should_color);
        output += "\n";
      }
    }
  }

  ec.print_to_stdout(output);
  return status;
}

}
