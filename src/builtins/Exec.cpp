#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* exec replaces the shell with the named program, so it does not fork. With no
   command it applies its redirections to the shell itself and returns. exec
   names an executable file, not a builtin, since it replaces the process. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[command [argument ...]]");

HELP_DESCRIPTION_DECL(
    "The exec builtin replaces the shell with the named command instead of "
    "forking a child, so control does not return on success. The command names "
    "an executable file, not a builtin. With no command the builtin applies "
    "its "
    "redirections to the shell itself and returns.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Exec);

namespace shit {

Exec::Exec() = default;

pure fn Exec::kind() const wontthrow -> Builtin::Kind { return Kind::Exec; }

fn Exec::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* exec with only redirections changes the shell's own descriptors and
     returns, so the rest of the session inherits them. Inside an in-process
     subshell each touched descriptor is backed up first, so the change stays
     contained at the subshell's end. */
  if (args.count() == 1) {
    LOG(Debug, "exec applying redirections to the shell's own descriptors");
    if (ec.in_fd.has_value()) cxt.snapshot_subshell_descriptor(0);
    if (ec.out_fd.has_value()) cxt.snapshot_subshell_descriptor(1);
    if (ec.err_fd.has_value()) cxt.snapshot_subshell_descriptor(2);
    os::redirect_self(ec);
    return 0;
  }

  let const &command_name = args[1];

  LOG(Info, "exec replacing the shell with '%s'", command_name.c_str());

  /* Resolve to an executable file. A failure here ends the shell with 127, the
     status a command-not-found leaves. */
  let program_path = Path{};
  if (command_name.find_character('/').has_value()) {
    let resolved = Path::canonicalize(command_name);
    if (!resolved) {
      show_message("exec: '" + command_name + "': not found");
      utils::quit(127, true);
    }
    program_path = resolved.take();
  } else {
    let const found = utils::search_program_path(command_name);
    if (found.count() == 0) {
      show_message("exec: '" + command_name + "': not found");
      utils::quit(127, true);
    }
    ASSERT(found.count() > 0);
    program_path = found[0];
  }

  let command_args = ArrayList<String>{};
  for (usize i = 1; i < args.count(); i++)
    command_args.push_managed(args[i]);
  let command = ExecContext::from_resolved(
      ec.source_location(), ResolvedCommand::from_program(program_path),
      steal(command_args));
  if (ec.in_fd) command.in_fd = ec.in_fd.take();
  if (ec.out_fd) command.out_fd = ec.out_fd.take();
  if (ec.err_fd) command.err_fd = ec.err_fd.take();
  command.dup_err_to_out = ec.dup_err_to_out;
  command.dup_out_to_err = ec.dup_out_to_err;
  command.dup_out_to_err_came_last = ec.dup_out_to_err_came_last;

  /* Inside an in-process subshell, a command substitution, or a pipeline
     stage, the process the exec would replace is that inner scope, not the
     shell, so $(exec cat) and true | exec cat must not kill the session. The
     program runs as a spawned child and its status ends the scope, the way
     bash's forked subshell or stage dies into its exec. */
  if (cxt.in_subshell() || cxt.is_in_pipeline_stage()) {
    LOG(Info, "exec runs '%s' as a child rather than replacing the shell",
        command_name.c_str());
    let const status = utils::execute_context(steal(command), cxt, false);
    if (cxt.in_subshell()) cxt.request_exit(status, ec.source_location());
    return status;
  }

  /* An external command replaces the shell. replace_process returns only by
     throwing, when the program was found but could not be executed, which ends
     the shell with 126, the status reserved for a command that is present but
     not executable. A name that resolves to None exited 127 above. */
  try {
    os::replace_process(steal(command));
  } catch (const ErrorBase &error) {
    /* replace_process throws ErrorWithLocation for a found-but-unexecutable
       file, a sibling of Error under ErrorBase, so the catch spans the base to
       reach it rather than letting it propagate as a generic exit-1 command
       error. */
    show_message(error.message());
    utils::quit(126, true);
  }
}

} /* namespace shit */
