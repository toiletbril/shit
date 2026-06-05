#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Pwd::Pwd() = default;

Builtin::Kind Pwd::kind() const { return Kind::Pwd; }

i32 Pwd::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);

  ArrayList<String> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  String p{};
  p.append(Path::current_directory().text());
  p += '\n';
  ec.print_to_stdout(p);
  return 0;
}

} /* namespace shit */
