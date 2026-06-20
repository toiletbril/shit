#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-v] [-V] name [argument ...]");

HELP_DESCRIPTION_DECL(
    "The command builtin runs name with its arguments as a command, resolving "
    "it against a builtin or the PATH while ignoring a shell function of the "
    "same name. With -v it prints a terse description of how name resolves, "
    "and "
    "with -V it prints a verbose description, in either case without running "
    "the command.");

FLAG(SHOW, Bool, 'v', "", "Print the resolution of the name in a terse form.");
FLAG(SHOW_VERBOSE, Bool, 'V', "",
     "Print the resolution of the name verbosely.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(CommandBuiltin);

namespace shit {

CommandBuiltin::CommandBuiltin() = default;

pure fn CommandBuiltin::kind() const wontthrow -> Builtin::Kind
{
  return Kind::CommandBuiltin;
}

fn CommandBuiltin::execute(ExecContext &ec, EvalContext &cxt) const throws
    -> i32
{
  let const args =
      parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  if (args.count() < 2) return 0;

  let const &name = args[1];

  LOG(Debug, "command resolving '%s' past shell functions", name.c_str());

  /* -v and -V resolve the name without running it, against a builtin and the
     PATH but not a function, the way command is meant to. */
  if (FLAG_SHOW.is_enabled() || FLAG_SHOW_VERBOSE.is_enabled()) {
    let const is_verbose = FLAG_SHOW_VERBOSE.is_enabled();
    /* A name that carries a slash is a pathname, not a command name, so it
       resolves against the filesystem directly and never a keyword, alias, or
       builtin, the way bash treats it. It resolves when it is an executable
       regular file, and the path is printed as typed. */
    if (name.find_character('/').has_value()) {
      let const candidate = Path{name.view()};
      if (candidate.exists() && !candidate.is_directory() &&
          candidate.is_executable())
      {
        ec.print_to_stdout(is_verbose ? name + " is " + name + "\n"
                                      : name + "\n");
        return 0;
      }
      if (is_verbose) ec.print_to_stdout(name + ": not found\n");
      return 1;
    }
    /* A reserved word resolves first, terse to the bare word and verbose to a
       keyword note, matching dash. */
    if (utils::is_posix_reserved_word(name.view())) {
      ec.print_to_stdout(is_verbose ? name + " is a shell keyword\n"
                                    : name + "\n");
      return 0;
    }
    /* An alias resolves next, terse to its definition and verbose to a note. */
    if (let const alias = cxt.get_alias(name.view()); alias.has_value()) {
      if (is_verbose)
        ec.print_to_stdout(name + " is an alias for " + *alias + "\n");
      else
        ec.print_to_stdout("alias " + name + "='" + *alias + "'\n");
      return 0;
    }
    if (search_builtin(name.view()).has_value()) {
      ec.print_to_stdout(is_verbose ? name + " is a shell builtin\n"
                                    : name + "\n");
      return 0;
    }
    if (const ArrayList<Path> paths = utils::search_program_path(name);
        paths.count() != 0)
    {
      let resolved_text = String{};
      if (is_verbose) {
        resolved_text += name;
        resolved_text += " is ";
        resolved_text += paths[0].text();
        resolved_text += "\n";
      } else {
        resolved_text += paths[0].text();
        resolved_text += "\n";
      }
      ec.print_to_stdout(resolved_text);
      return 0;
    }
    if (is_verbose) ec.print_to_stdout(name + ": not found\n");
    return 1;
  }

  /* The bare form runs the operand and its arguments as a command, which
     resolves against a builtin or the PATH and never a function. */
  let operand_args = ArrayList<String>{};
  for (usize i = 1; i < args.count(); i++)
    operand_args.push_managed(args[i]);

  /* An operand that does not resolve is non-fatal, the same as a bare command
     word. Report it to stderr and return 127 rather than letting the not-found
     error unwind and abort the shell. */
  Maybe<ExecContext> sub;
  try {
    sub = ExecContext::make_from(ec.source_location(), steal(operand_args),
                                 cxt.mood(), cxt.shitbox());
  } catch (const CommandNotFound &not_found) {
    LOG(Debug, "command swallowed a not-found error: %s",
        not_found.message().c_str());
    const String *source = cxt.current_source();
    show_message(
        not_found.to_string(source != nullptr ? source->view() : StringView{}));
    return 127;
  }
  return utils::execute_context(steal(*sub), cxt, false);
}

} // namespace shit
