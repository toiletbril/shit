#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* command runs its operand as a command while ignoring a shell function of the
   same name, or with -v and -V reports how the operand resolves. The function
   table is consulted only when a simple command runs, so resolving and running
   the operand here already bypasses it. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("command [-v] [-V] name [argument ...]");

FLAG(SHOW, Bool, 'v', "", "Print the resolution of the name in a terse form.");
FLAG(SHOW_VERBOSE, Bool, 'V', "", "Print the resolution of the name verbosely.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

CommandBuiltin::CommandBuiltin() = default;

Builtin::Kind
CommandBuiltin::kind() const
{
  return Kind::CommandBuiltin;
}

i32
CommandBuiltin::execute(ExecContext &ec, EvalContext &cxt) const
{
  std::vector<std::string> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.size() < 2) return 0;

  const std::string &name = args[1];

  /* -v and -V resolve the name without running it, against a builtin and the
     PATH but not a function, the way command is meant to. */
  if (FLAG_SHOW.is_enabled() || FLAG_SHOW_VERBOSE.is_enabled()) {
    bool verbose = FLAG_SHOW_VERBOSE.is_enabled();
    if (search_builtin(name).has_value()) {
      ec.print_to_stdout(verbose ? name + " is a shell builtin\n" : name + "\n");
      return 0;
    }
    if (ArrayList<std::filesystem::path> paths =
            utils::search_program_path(name);
        paths.size() != 0)
    {
      ec.print_to_stdout(verbose ? name + " is " + paths[0].string() + "\n"
                                 : paths[0].string() + "\n");
      return 0;
    }
    if (verbose) ec.print_to_stdout(name + ": not found\n");
    return 1;
  }

  /* The bare form runs the operand and its arguments as a command, which
     resolves against a builtin or the PATH and never a function. */
  std::vector<std::string> operand_args{args.begin() + 1, args.end()};
  ExecContext sub = ExecContext::make_from(ec.source_location(), operand_args);
  return utils::execute_context(std::move(sub), cxt, false);
}

} /* namespace shit */
