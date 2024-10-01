#include "../Builtin.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

WhoAmI::WhoAmI() = default;

Builtin::Kind
WhoAmI::kind() const
{
  return Kind::WhoAmI;
}

i32
WhoAmI::execute(ExecContext &ec) const
{
  std::vector<std::string> args = BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) {
    SHOW_BUILTIN_HELP("whoami", ec);
    return 0;
  }

  std::string p{};

  if (std::optional<std::string> u = os::get_current_user(); u.has_value()) {
    p += *u;
    p += '\n';
    ec.print_to_stdout(p);
    return 0;
  }

  return 1;
}

} /* namespace shit */
