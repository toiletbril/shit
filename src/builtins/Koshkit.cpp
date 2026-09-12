/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements bundled utility selection, help and sorted listings,
 * direct bare-name dispatch, and multicall symlink installation for the
 * koshkit builtin.
 */

#include "../Koshkit.hpp"

#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../CliColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[utility] [arg ...]");

HELP_DESCRIPTION_DECL("The koshkit builtin runs a bundled utility.");

FLAG(HELP, Bool, '\0', "help", "Display help and list the utilities.");
FLAG(KOSHKIT_LIST, Bool, '\0', "list", "List the utility names, one per line.");
FLAG(KOSHKIT_ASSIMILATE, String, '\0', "assimilate",
     "Install a symlink to this binary for each utility into the given "
     "directory.");

REGISTER_BUILTIN_FLAGS(Koshkit);

namespace koshka {

Koshkit::Koshkit() = default;

pure fn Koshkit::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Koshkit;
}

enum class utility_section : u8
{
  Posix,
  Koshka,
};

static pure fn get_utility_section(koshkit::Utility::Kind kind) wontthrow
    -> utility_section
{
  switch (kind) {
  case koshkit::Utility::Kind::Evil:
  case koshkit::Utility::Kind::Evilfiles:
  case koshkit::Utility::Kind::Evilfs:
  case koshkit::Utility::Kind::Evilnet:
  case koshkit::Utility::Kind::Goodnode:
  case koshkit::Utility::Kind::Evilps:
  case koshkit::Utility::Kind::Goodstat:
  case koshkit::Utility::Kind::Evildisk:
  case koshkit::Utility::Kind::Evilio:
  case koshkit::Utility::Kind::Evillogs:
  case koshkit::Utility::Kind::Goodcore:
  case koshkit::Utility::Kind::Evilss:
  case koshkit::Utility::Kind::Goodfsw: return utility_section::Koshka;
  default: return utility_section::Posix;
  }
}

fn Koshkit::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (let const chosen = koshkit::find_util(ec.args()[0].view());
      chosen.has_value())
    return koshkit::dispatch(ec, cxt, 0, chosen);

  if (ec.args().count() >= 2) {
    if (let const chosen = koshkit::find_util(ec.args()[1].view());
        chosen.has_value())
      return koshkit::dispatch(ec, cxt, 1, chosen);
  }

  let sorted_names = ArrayList<String>{cxt.scratch_allocator()};
  for (const String &name : koshkit::util_names())
    sorted_names.push(name.clone());
  sorted_names.sort();

  if (ec.args().count() >= 2 && ec.args()[1] == "--list") {
    let names_output = String{cxt.scratch_allocator()};
    for (let const &name : sorted_names) {
      names_output += name.view();
      names_output += '\n';
    }
    ec.print_to_stdout(names_output);
    return 0;
  }

  if (ec.args().count() >= 2 && ec.args()[1] == "--assimilate") {
    if (ec.args().count() < 3) return report_usage_error(ec, cxt, ec.program());

    let const target = os::current_executable_path();
    if (!target.has_value()) {
      report_soft_builtin_error(
          ec, cxt, "Cannot resolve this binary's path to assimilate");
      return 1;
    }

    if (!Path{ec.args()[2].view()}.is_directory()) {
      report_soft_builtin_error(
          ec, cxt,
          "Cannot assimilate into '" +
              String{cxt.scratch_allocator(), ec.args()[2].view()} +
              "': not a directory");
      return 1;
    }

    i32 status = 0;
    for (let const &name : sorted_names) {
      let link = Path{ec.args()[2].view()};
      link.push_component(name.view());
      if (link.is_symbolic_link()) os::remove_file(link.text().view());
      if (!os::create_symlink(target->view(), link.text().view())) {
        report_soft_builtin_error(ec, cxt,
                                  "Cannot link '" + link.text() +
                                      "': " + os::last_system_error_message());
        status = 1;
      }
    }
    return status;
  }

  if (ec.args().count() < 2 || ec.args()[1] == "--help") {
    let listing = String{cxt.scratch_allocator()};
    listing += "DESCRIPTION\n";
    listing += wrap_text(HELP_DESCRIPTION, HELP_INDENT, HELP_WRAP_WIDTH);
    listing += "\n\nSYNOPSIS\n";
    listing += "  koshkit [utility] [arg ...]\n";
    listing += "  koshkit --list\n";
    listing += "  koshkit --assimilate DIR\n";
    listing += "\nUTILITIES\n\n";

    let posix_names = ArrayList<StringView>{cxt.scratch_allocator()};
    let koshka_names = ArrayList<StringView>{cxt.scratch_allocator()};
    for (let const &name : sorted_names) {
      let const kind = koshkit::find_util(name.view());
      ASSERT(kind.has_value());
      switch (get_utility_section(*kind)) {
      case utility_section::Posix: posix_names.push(name.view()); break;
      case utility_section::Koshka: koshka_names.push(name.view()); break;
      }
    }

    let const should_color = colors::stdout_wants_color();
    append_report_name_section(listing, "POSIX", posix_names, should_color,
                               "  ");
    append_report_name_section(listing, "Koshka", koshka_names, should_color,
                               "  ");

    ec.print_to_stdout(format_cli_help(listing.view()));
    return 0;
  }

  return koshkit::dispatch(ec, cxt, 1);
}

} // namespace koshka
