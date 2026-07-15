#include "../Shitbox.hpp"

#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[utility] [arg ...]");

HELP_DESCRIPTION_DECL("The shitbox builtin runs a bundled coreutility.");

FLAG(HELP, Bool, '\0', "help", "Display help and list the utilities.");
FLAG(SHITBOX_LIST, Bool, '\0', "list", "List the utility names, one per line.");
FLAG(SHITBOX_ASSIMILATE, String, '\0', "assimilate",
     "Install a symlink to this binary for each utility into the given "
     "directory.");

REGISTER_BUILTIN_FLAGS(Shitbox);

namespace shit {

Shitbox::Shitbox() = default;

pure fn Shitbox::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Shitbox;
}

fn Shitbox::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  /* A bare-name invocation arrives with the utility name already at args[0],
     so it dispatches with the name in place. The resolver sets this up when the
     shitbox option is on. */
  if (shitbox::find_util(ec.args()[0].view()).has_value())
    return shitbox::dispatch(ec, cxt, 0);

  let sorted_names = ArrayList<String>{cxt.scratch_allocator()};
  for (const String &name : shitbox::util_names())
    sorted_names.push(name.clone());
  shitbox::sort_string_list(sorted_names);

  if (ec.args().count() >= 2 && ec.args()[1] == "--list") {
    let names_output = String{cxt.scratch_allocator()};
    for (let const &name : sorted_names) {
      names_output += name.view();
      names_output += '\n';
    }
    ec.print_to_stdout(names_output);
    return 0;
  }

  if (ec.args().count() >= 2 && ec.args()[1] == "--binary-identity") {
    let const executable = os::current_executable_path();
    if (!executable.has_value()) return 1;
    let const identity = utils::file_content_identity(Path{*executable},
                                                      cxt.scratch_allocator());
    if (!identity.has_value()) return 1;
    ec.print_to_stdout(identity->view() + "\n");
    return 0;
  }

  if (ec.args().count() >= 2 && ec.args()[1] == "--transaction-lock-held") {
    if (ec.args().count() < 4) return report_usage_error(ec, cxt, ec.program());
    let const lock = os::acquire_process_lock(ec.args()[2].view());
    if (!lock.has_value()) {
      report_soft_builtin_error(ec, cxt, "Cannot lock the remote transaction");
      return 1;
    }
    defer { os::release_process_lock(*lock); };

    let command = ArrayList<String>{cxt.scratch_allocator()};
    command.reserve(ec.args().count() - 3);
    for (usize position = 3; position < ec.args().count(); position++)
      command.push(ec.args()[position].clone());
    let const result = os::run_measured(command, false, *lock);
    return result.has_value() ? static_cast<i32>(result->exit_status) : 126;
  }

  if (ec.args().count() >= 2 && ec.args()[1] == "--transaction-lock") {
    if (ec.args().count() < 4) return report_usage_error(ec, cxt, ec.program());
    let const executable = os::current_executable_path();
    if (!executable.has_value()) return 126;

    let keeper = ArrayList<String>{cxt.scratch_allocator()};
    keeper.reserve(ec.args().count() + 5);
    keeper.push(String{executable->view()});
    keeper.push(String{"-p"});
    keeper.push(String{"--mood"});
    keeper.push(String{"sh"});
    keeper.push(String{"-c"});
    keeper.push(String{"shitbox --transaction-lock-held \"$@\""});
    keeper.push(String{"shit"});
    for (usize position = 2; position < ec.args().count(); position++)
      keeper.push(ec.args()[position].clone());
    let const result = os::run_measured(keeper, false);
    return result.has_value() ? static_cast<i32>(result->exit_status) : 126;
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
      /* A real file at the path is left alone so assimilate never clobbers a
         user's binary. */
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
    listing += "  shitbox [utility] [arg ...]\n";
    listing += "  shitbox --list\n";
    listing += "  shitbox --assimilate DIR\n";
    listing += "\nUTILITIES\n";

    let joined_names = String{cxt.scratch_allocator()};
    for (let const &name : sorted_names) {
      if (!joined_names.is_empty()) joined_names += ", ";
      joined_names += name.view();
    }
    listing += wrap_text(joined_names.view(), HELP_INDENT, HELP_WRAP_WIDTH);
    listing += '\n';

    ec.print_to_stdout(listing);
    return 0;
  }

  return shitbox::dispatch(ec, cxt, 1);
}

} // namespace shit
