#include "../Builtin.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Pwd::Pwd() = default;

Builtin::Kind
Pwd::kind() const
{
  return Kind::Pwd;
}

i32
Pwd::execute(ExecContext &ec) const
{
  std::vector<std::string> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  std::string p{};
  p = utils::get_current_directory().string();
  p += '\n';
  ec.print_to_stdout(p);
  return 0;
}

} /* namespace shit */
