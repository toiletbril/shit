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

namespace shit {

Pwd::Pwd() = default;

pure Builtin::Kind Pwd::kind() const wontthrow { return Kind::Pwd; }

i32 Pwd::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(verbosity::Debug, "pwd printing the current working directory");

  let p = String{};
  p.append(Path::current_directory().text());
  p += '\n';
  ec.print_to_stdout(p);
  return 0;
}

} /* namespace shit */
