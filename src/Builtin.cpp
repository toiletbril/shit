#include "Builtin.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace shit {

fn show_builtin_help_impl(const ExecContext &ec,
                          const std::vector<std::string> &hs,
                          const ArrayList<Flag *> &fl) -> void
{
  String help_text{};
  help_text += StringView{make_synopsis(
      std::string_view{ec.args()[0].c_str(), ec.args()[0].size()}, hs)};
  help_text += '\n';
  help_text += StringView{make_flag_help(fl)};
  help_text += '\n';
  ec.print_to_stdout(help_text);
}

fn search_builtin(std::string_view builtin_name) -> Maybe<Builtin::Kind>
{
  return BUILTINS.find(StringView{builtin_name.data(), builtin_name.size()});
}

/* A one-line synopsis and a sentence of explanation for each builtin, shown by
   --help. */
/* The one-line synopsis and the sentence of explanation a builtin shows. */
struct BuiltinHelp
{
  String synopsis;
  String description;
};

static fn builtin_help(Builtin::Kind kind) -> BuiltinHelp
{
  switch (kind) {
  case Builtin::Kind::Echo:
    return {"echo [-n] [-e] [-E] [arg ...]",
            "Write the arguments to standard output, separated by spaces."};
  case Builtin::Kind::Cd:
    return {"cd [dir]", "Change the working directory to dir, or to HOME."};
  case Builtin::Kind::Exit:
    return {"exit [n]", "Exit the shell with status n, or the last status."};
  case Builtin::Kind::Pwd:
    return {"pwd", "Print the absolute path of the working directory."};
  case Builtin::Kind::Which:
    return {"which name ...", "Print the resolved path of each command name."};
  case Builtin::Kind::WhoAmI:
    return {"whoami", "Print the name of the current user."};
  case Builtin::Kind::Export:
    return {"export [name[=value] ...]",
            "Mark variables for the environment of later commands."};
  case Builtin::Kind::Break:
    return {"break [n]", "Exit the n innermost enclosing loops, n defaulting "
                         "to one."};
  case Builtin::Kind::Continue:
    return {"continue [n]",
            "Resume the next iteration of the nth enclosing loop."};
  case Builtin::Kind::Return:
    return {"return [n]",
            "Return from a function or a sourced file with status n."};
  case Builtin::Kind::True: return {"true", "Return a successful status."};
  case Builtin::Kind::False: return {"false", "Return a failing status."};
  case Builtin::Kind::Test:
    return {"test expression, or [ expression ]",
            "Evaluate a conditional expression and return its status."};
  case Builtin::Kind::Source:
    return {". file, or source file",
            "Read and run a file in the current shell."};
  case Builtin::Kind::Eval:
    return {"eval [arg ...]", "Join the arguments and run them as a command."};
  case Builtin::Kind::Set:
    return {"set [-/+eux] [--] [arg ...]",
            "Set shell options and the positional parameters, or list "
            "variables."};
  case Builtin::Kind::Shift:
    return {"shift [n]", "Drop the first n positional parameters."};
  case Builtin::Kind::Unset:
    return {"unset [-f] name ...", "Remove variables, or functions with -f."};
  case Builtin::Kind::Read:
    return {"read [name ...]",
            "Read one line from input and split it into the named variables."};
  case Builtin::Kind::Printf:
    return {"printf format [arg ...]",
            "Format the arguments under the format string and print them."};
  case Builtin::Kind::Umask:
    return {"umask [mask]",
            "Print the file-creation mask, or set it from an octal mask."};
  case Builtin::Kind::Getopts:
    return {"getopts optstring name [arg ...]",
            "Parse one positional option per call into name."};
  case Builtin::Kind::Trap:
    return {"trap [action condition ...]",
            "Run an action when a condition such as EXIT occurs, or list "
            "traps."};
  case Builtin::Kind::Exec:
    return {"exec [command [argument ...]]",
            "Replace the shell with the command, or apply redirections to the "
            "shell."};
  case Builtin::Kind::Type:
    return {"type name [name ...]",
            "Report how each name would be resolved as a command."};
  case Builtin::Kind::CommandBuiltin:
    return {"command [-v] name [argument ...]",
            "Run a command ignoring a function, or report its resolution."};
  case Builtin::Kind::Readonly:
    return {"readonly [name[=value] ...]",
            "Mark a variable unwritable, or list the read-only variables."};
  case Builtin::Kind::Local:
    return {"local name[=value] ...",
            "Declare a variable local to the current function."};
  case Builtin::Kind::Times:
    return {"times", "Print the user and system times of the shell."};
  case Builtin::Kind::Ulimit:
    return {"ulimit [-f|-n|-t|-u] [limit]",
            "Print or set a resource limit of the shell."};
  case Builtin::Kind::Hash:
    return {"hash [-r] [name ...]",
            "Accepted for compatibility, the shell resolves PATH lazily."};
  case Builtin::Kind::Alias:
    return {"alias [name[=value] ...]",
            "Define a command alias, or list the defined aliases."};
  case Builtin::Kind::Unalias:
    return {"unalias name [name ...]", "Remove a command alias."};
  case Builtin::Kind::Jobs:
    return {"jobs", "List the background jobs and their state."};
  case Builtin::Kind::Fg:
    return {"fg [%job]", "Bring a job to the foreground and wait for it."};
  case Builtin::Kind::Bg:
    return {"bg [%job]", "Resume a stopped job in the background."};
  case Builtin::Kind::Wait:
    return {"wait [%job|pid ...]", "Wait for jobs to finish."};
  case Builtin::Kind::Kill:
    return {"kill [-signal] %job|pid ...", "Send a signal to a job or pid."};
  }
  return {"", ""};
}

fn execute_builtin(ExecContext &&ec, EvalContext &cxt) -> i32
{
  /* Every builtin answers --help with its synopsis and a short explanation. */
  if (ec.args().size() > 1 && ec.args()[1] == "--help") {
    BuiltinHelp help = builtin_help(ec.builtin_kind());
    ec.print_to_stdout(StringView{"SYNOPSIS\n  "} + help.synopsis + "\n\n" +
                       help.description + "\n");
    ec.close_fds();
    return 0;
  }

  std::unique_ptr<Builtin> b{};

  switch (ec.builtin_kind()) {
    BUILTIN_SWITCH_CASES();
  default:
    SHIT_UNREACHABLE("Unhandled builtin of kind %d",
                     SHIT_ENUM(ec.builtin_kind()));
  }

  /* A builtin runs inside the shell process, so it keeps the shell's own signal
     handlers. Resetting them to the default here would let a Ctrl-C during a
     builtin terminate the whole shell, and it cost two extra syscalls on every
     builtin command. */
  SHIT_DEFER { ec.close_fds(); };

  try {
    return b->execute(ec, cxt);
  } catch (const Error &e) {
    throw ErrorWithLocation{ec.source_location(), StringView{"Builtin '"} +
                                                      ec.program() +
                                                      "': " + e.message()};
  }
}

Builtin::Builtin() = default;

} /* namespace shit */
