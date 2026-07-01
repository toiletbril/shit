#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[command [argument ...]]");

HELP_DESCRIPTION_DECL(
    "The exec builtin replaces the shell with the named command instead of "
    "forking a child, so control does not return on success. The command names "
    "an executable file, not a builtin. The -l option prefixes the command's "
    "zeroth argument with a dash, so the program sees itself as a login shell "
    "the way login does. The -a option names that zeroth argument, and the -c "
    "option runs the command with an empty environment. With no command the "
    "builtin applies its redirections to the shell itself and returns. The "
    "options are read in the default and bash moods, while the sh mood passes "
    "them through as the dash exec does.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(EXEC_LOGIN, Bool, 'l', "",
     "Prefix the command's zeroth argument with a dash.");
FLAG(EXEC_ARGV0, String, 'a', "",
     "Pass the given name as the zeroth argument.");
FLAG(EXEC_CLEAR_ENV, Bool, 'c', "",
     "Run the command with an empty environment.");

REGISTER_BUILTIN_FLAGS(Exec);

namespace shit {

Exec::Exec() = default;

pure fn Exec::kind() const wontthrow -> Builtin::Kind { return Kind::Exec; }

fn Exec::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let should_be_login_shell = false;
  let should_use_empty_environment = false;
  let has_custom_argv0 = false;
  let custom_argv0 = String{heap_allocator()};
  usize command_index = 1;

  if (!cxt.is_posix_mode()) {
    while (command_index < args.count()) {
      const StringView arg = args[command_index].view();
      if (arg == "--") {
        command_index++;
        break;
      }

      if (arg.length < 2 || arg[0] != '-') break;

      let did_consume_value_word = false;
      for (usize k = 1; k < arg.length; k++) {
        let const option = arg[k];
        if (option == 'l') {
          should_be_login_shell = true;
        } else if (option == 'c') {
          should_use_empty_environment = true;
        } else if (option == 'a') {
          if (k + 1 < arg.length) {
            has_custom_argv0 = true;
            custom_argv0 = arg.substring(k + 1);
          } else if (command_index + 1 < args.count()) {
            has_custom_argv0 = true;
            custom_argv0 = args[command_index + 1];
            did_consume_value_word = true;
          } else {
            report_soft_builtin_error(ec, cxt,
                                      "Option requires an argument -- a");
            return 2;
          }

          break;
        } else {
          let option_text = String{cxt.scratch_allocator()};
          option_text.push(option);
          report_soft_builtin_error(
              ec, cxt, StringView{"Invalid option -- "} + option_text);
          return 2;
        }
      }

      command_index++;
      if (did_consume_value_word) command_index++;
    }
  }

  /* Inside an in-process subshell each touched descriptor is backed up first,
     so the change stays contained at the subshell's end. */
  if (command_index >= args.count()) {
    LOG(Debug, "exec applying redirections to the shell's own descriptors");
    if (ec.in_fd.has_value()) cxt.snapshot_subshell_descriptor(0);
    if (ec.out_fd.has_value()) cxt.snapshot_subshell_descriptor(1);
    if (ec.err_fd.has_value()) cxt.snapshot_subshell_descriptor(2);
    os::redirect_self(ec);
    return 0;
  }

  let const &command_name = args[command_index];

  LOG(Info, "exec replacing the shell with '%s'", command_name.c_str());

  /* Resolve to an executable file. A failure here ends the shell with 127, the
     status a command-not-found leaves. */
  let program_path = Path{};
  if (command_name.find_character('/').has_value()) {
    let resolved = Path::canonicalize(command_name);
    if (!resolved) {
      const CommandNotFound not_found{ec.source_location(),
                                      StringView{"Command '"} + command_name +
                                          "' was not found"};
      const String *source = cxt.current_source();
      show_message(not_found.to_string(source != nullptr ? source->view()
                                                         : StringView{}));
      utils::quit(127, true);
    }
    program_path = resolved.take();
  } else {
    let const found = utils::search_program_path(command_name);
    if (found.count() == 0) {
      const CommandNotFound not_found{ec.source_location(),
                                      StringView{"Command '"} + command_name +
                                          "' was not found"};
      const String *source = cxt.current_source();
      show_message(not_found.to_string(source != nullptr ? source->view()
                                                         : StringView{}));
      utils::quit(127, true);
    }
    ASSERT(found.count() > 0);
    program_path = found[0];
  }

  let command_args = ArrayList<String>{heap_allocator()};
  for (usize i = command_index; i < args.count(); i++)
    command_args.push_managed(args[i]);

  if (has_custom_argv0) command_args[0] = custom_argv0;

  if (should_be_login_shell) {
    let dashed_argv0 = StringView{"-"} + command_args[0];
    command_args[0] = steal(dashed_argv0);
  }

  let command = ExecContext::from_resolved(
      ec.source_location(), ResolvedCommand::from_program(program_path),
      steal(command_args));
  if (ec.in_fd) command.in_fd = ec.in_fd.take();
  if (ec.out_fd) command.out_fd = ec.out_fd.take();
  if (ec.err_fd) command.err_fd = ec.err_fd.take();
  command.dup_err_to_out = ec.dup_err_to_out;
  command.dup_out_to_err = ec.dup_out_to_err;
  command.dup_out_to_err_came_last = ec.dup_out_to_err_came_last;
  command.should_use_empty_environment = should_use_empty_environment;

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
       reach it. */
    show_message(error.message());
    utils::quit(126, true);
  }
}

} // namespace shit
