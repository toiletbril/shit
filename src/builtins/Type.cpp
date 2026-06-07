#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Utils.hpp"

/* type reports how each name resolves, checking the same order the shell uses
   to run a command, a function first, then a builtin, then the PATH. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("type name [name ...]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Type::Type() = default;

pure Builtin::Kind Type::kind() const wontthrow { return Kind::Type; }

i32 Type::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  String out{};
  bool all_found = true;

  for (usize i = 1; i < args.count(); i++) {
    let const &name = args[i];

    if (utils::is_posix_reserved_word(name.view())) {
      out += name;
      out += " is a shell keyword\n";
    } else if (let const alias = cxt.get_alias(name.view()); alias.has_value())
    {
      out += name;
      out += " is an alias for ";
      out += *alias;
      out += "\n";
    } else if (cxt.has_functions() && cxt.find_function(name) != nullptr) {
      out += name;
      out += " is a shell function\n";
    } else if (search_builtin(name.view()).has_value()) {
      out += name;
      out += " is a shell builtin\n";
    } else if (let const paths = utils::search_program_path(name);
               paths.count() != 0)
    {
      ASSERT(paths.count() > 0);
      out += name;
      out += " is ";
      out += paths[0].text();
      out += "\n";
    } else {
      out += name;
      out += ": not found\n";
      all_found = false;
    }
  }

  ec.print_to_stdout(out);
  return all_found ? 0 : 1;
}

} /* namespace shit */
