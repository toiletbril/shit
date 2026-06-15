#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");
HELP_DESCRIPTION_DECL(
    "The pwd builtin prints the absolute path of the current working directory "
    "to standard output.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Pwd);

namespace shit {

Pwd::Pwd() = default;

pure Builtin::Kind Pwd::kind() const wontthrow { return Kind::Pwd; }

i32 Pwd::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(verbosity::Debug, "pwd printing the current working directory");

  /* pwd reports the logical working directory by default, the way dash and bash
     do, so a directory entered through a symbolic link prints under the name it
     was reached by rather than the path getcwd resolves the link to. */
  let p = String{};
  p.append(utils::logical_working_directory(cxt).text());
  p += '\n';
  ec.print_to_stdout(p);
  return 0;
}

} /* namespace shit */
