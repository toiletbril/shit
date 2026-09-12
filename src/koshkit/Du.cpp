/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the du utility. It recursively totals file sizes without
 * following symbolic links and formats either byte counts or human-readable
 * totals.
 */

#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Path.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-sh] [path ...]");

HELP_DESCRIPTION_DECL(
    "The du utility prints the total byte size of each path.");

FLAG(DU_SUMMARY, Bool, 's', "",
     "Print only the total for each path, the default.");
FLAG(DU_HUMAN, Bool, 'h', "",
     "Print the size in a human-readable form such as 4.0K or 1.5M.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Du);

namespace koshka {

namespace koshkit {

static fn total_size(const Path &path, Path &failed_path,
                     String &failure_message,
                     const os::file_status *known_status = nullptr) throws
    -> Maybe<u64>
{
  os::file_status queried_status{};
  if (known_status == nullptr) {
    if (!os::stat_path(path.text().view(), queried_status)) {
      failure_message = os::last_system_error_message();
      failed_path = path.clone();
      return None;
    }

    known_status = &queried_status;
  }

  let const type_letter = os::file_type_letter(known_status->mode);
  if (type_letter == 'd') {
    u64 total_bytes = 0;
    let const children =
        os::list_directory_status(path.text().view(), heap_allocator());
    if (!children.has_value()) {
      failure_message = os::last_system_error_message();
      failed_path = path.clone();
      return None;
    }

    for (let const &child_entry : *children) {
      if (os::INTERRUPT_REQUESTED) return None;

      let const child = PathBuilder{path.text().view()}
                            .append(child_entry.child.name.view())
                            .build();
      let const child_status =
          child_entry.has_status ? &child_entry.status : nullptr;
      let const child_size =
          total_size(child, failed_path, failure_message, child_status);
      if (!child_size.has_value()) return None;
      if (*child_size > UINT64_MAX - total_bytes) {
        failure_message = "the total size is too large";
        failed_path = child.clone();
        return None;
      }
      total_bytes += *child_size;
    }

    return total_bytes;
  }

  return known_status->size;
}

fn append_size_line(String &output, u64 size, StringView path,
                    Allocator allocator) throws -> void
{
  output += FLAG_DU_HUMAN.is_enabled() ? format_human_size(size, allocator)
                                       : String::from(size, allocator);
  output += '\t';
  output += path;
  output += '\n';
}

Du::Du() = default;

pure fn Du::kind() const wontthrow -> Utility::Kind { return Kind::Du; }

fn Du::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args, &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  let const allocator = cxt.scratch_allocator();
  ArrayList<String> targets{allocator};
  if (operands.is_empty()) {
    let names = Path::read_directory(Path{"."});
    if (!names.has_value()) {
      report_soft_koshkit_error(
          ec, cxt, "du: cannot read '.': " + os::last_system_error_message());
      return 1;
    }
    names->sort();
    targets.reserve(names->count());
    for (let const &name : *names)
      targets.push(String{allocator, name.view()});
  } else {
    targets.reserve(operands.count());
    for (let const &operand : operands)
      targets.push(String{allocator, operand.view()});
  }

  let output = String{allocator};
  i32 status = 0;
  u64 current_directory_total = 0;
  bool is_current_directory_total_valid = true;
  for (let const &target : targets) {
    let const path = Path{target.view()};
    if (!path.exists()) {
      report_soft_koshkit_error(ec, cxt,
                                "du: cannot access '" +
                                    String{allocator, target.view()} +
                                    "': no such file or directory");
      status = 1;
      is_current_directory_total_valid = false;
      continue;
    }
    let failed_path = Path{};
    let failure_message = String{cxt.scratch_allocator()};
    let const total = total_size(path, failed_path, failure_message);
    if (os::INTERRUPT_REQUESTED) return 130;
    if (!total.has_value()) {
      report_soft_koshkit_error(ec, cxt,
                                "du: cannot read '" + failed_path.text() +
                                    "': " + failure_message);
      status = 1;
      is_current_directory_total_valid = false;
      continue;
    }
    append_size_line(output, *total, target.view(), allocator);

    if (operands.is_empty()) {
      if (*total > UINT64_MAX - current_directory_total) {
        is_current_directory_total_valid = false;
        status = 1;
      } else {
        current_directory_total += *total;
      }
    }
  }

  if (operands.is_empty() && is_current_directory_total_valid) {
    append_size_line(output, current_directory_total, ".", allocator);
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
