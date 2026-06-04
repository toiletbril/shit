#include "Builtin.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace shit {

void
show_builtin_help_impl(const ExecContext &ec,
                       const std::vector<std::string> &hs,
                       const std::vector<Flag *> &fl)
{
  std::string h{};
  h += make_synopsis(ec.args()[0], hs);
  h += '\n';
  h += make_flag_help(fl);
  h += '\n';
  ec.print_to_stdout(h);
}

Maybe<Builtin::Kind>
search_builtin(std::string_view builtin_name)
{
  if (auto b = BUILTINS.find(std::string{builtin_name}); b != BUILTINS.end())
    return b->second;

  return shit::nothing;
}

/* A one-line synopsis and a sentence of explanation for each builtin, shown by
   --help. */
static std::pair<std::string, std::string>
builtin_help(Builtin::Kind kind)
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
  case Builtin::Kind::Colon:
    return {":", "Do nothing and return a successful status."};
  case Builtin::Kind::True:
    return {"true", "Return a successful status."};
  case Builtin::Kind::False:
    return {"false", "Return a failing status."};
  case Builtin::Kind::Test:
    return {"test expression, or [ expression ]",
            "Evaluate a conditional expression and return its status."};
  case Builtin::Kind::Source:
    return {". file, or source file",
            "Read and run a file in the current shell."};
  case Builtin::Kind::Eval:
    return {"eval [arg ...]",
            "Join the arguments and run them as a command."};
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
  }
  return {"", ""};
}

i32
execute_builtin(ExecContext &&ec, EvalContext &cxt)
{
  /* Every builtin answers --help with its synopsis and a short explanation. */
  if (ec.args().size() > 1 && ec.args()[1] == "--help") {
    auto [synopsis, description] = builtin_help(ec.builtin_kind());
    ec.print_to_stdout("SYNOPSIS\n  " + synopsis + "\n\n" + description + "\n");
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
    throw ErrorWithLocation{ec.source_location(),
                            "Builtin '" + ec.program() + "': " + e.message()};
  }
}

Builtin::Builtin() = default;

} /* namespace shit */
