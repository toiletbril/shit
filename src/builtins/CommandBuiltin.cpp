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
    "The command builtin runs a command past a same-named function.");

FLAG(SHOW, Bool, 'v', "", "Print the resolution of the name in a terse form.");
FLAG(SHOW_VERBOSE, Bool, 'V', "",
     "Print the resolution of the name verbosely.");
FLAG(COMMAND_DEFAULT_PATH, Bool, 'p', "",
     "Resolve against a default PATH that finds the standard utilities.");
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
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  if (args.count() < 2) return 0;

  let const &name = args[1];

  LOG(Debug, "command resolving '%s' past shell functions", name.c_str());

  if (FLAG_SHOW.is_enabled() || FLAG_SHOW_VERBOSE.is_enabled()) {
    let const is_verbose = FLAG_SHOW_VERBOSE.is_enabled();
    /* A name that carries a slash is a pathname, so it resolves against the
       filesystem directly and never a keyword, alias, or builtin. */
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
    if (utils::is_posix_reserved_word(name.view())) {
      ec.print_to_stdout(is_verbose ? name + " is a shell keyword\n"
                                    : name + "\n");
      return 0;
    }
    if (let const alias = cxt.get_alias(name.view()); alias.has_value()) {
      if (is_verbose)
        ec.print_to_stdout(name + " is aliased to `" + *alias + "'\n");
      else
        ec.print_to_stdout("alias " + name + "='" + *alias + "'\n");
      return 0;
    }
    if (cxt.has_functions() && cxt.find_function(name.view()) != nullptr) {
      ec.print_to_stdout(is_verbose ? name + " is a function\n" : name + "\n");
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
      let resolved_text = String{cxt.scratch_allocator()};
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
    if (is_verbose) ec.print_to_stderr(name + ": not found\n");
    return 1;
  }

  let operand_args = ArrayList<String>{heap_allocator()};
  for (usize i = 1; i < args.count(); i++)
    operand_args.push_managed(args[i]);

  /* The default PATH is in force only while make_from resolves the program,
     then the resolver reverts to the environment PATH. */
  let const should_use_default_path = FLAG_COMMAND_DEFAULT_PATH.is_enabled();
  if (should_use_default_path)
    utils::set_path_for_resolution(String{"/usr/bin:/bin"});
  defer
  {
    if (should_use_default_path) utils::set_path_for_resolution(shit::None);
  };

  /* An unresolved operand returns 127 here rather than letting the not-found
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
