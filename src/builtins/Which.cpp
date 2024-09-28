#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-OPTIONS] <program> [program, ...]");

FLAG(ALL, Bool, 'a', "all", "UNIMPLEMENTED: Show all matches.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Which::Which() = default;

Builtin::Kind
Which::kind() const
{
  return Kind::Which;
}

i32
Which::execute(ExecContext &ec) const
{
  std::vector<std::string> args = BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) {
    SHOW_BUILTIN_HELP("which", ec);
    return 0;
  }

  std::string buf{};
  i32         ret = 1;

  for (usize i = 1; i < args.size(); i++) {
    if (search_builtin(args[i]).has_value()) {
      buf += args[i];
      buf += ": Shell builtin";
    } else if (std::optional<std::filesystem::path> p =
            utils::search_program_path(args[i]);
        p.has_value())
    {
      buf += p->string();
    }
    buf += '\n';

    ret = 0;
  }

  ec.print_to_stdout(buf);

  return ret;
}

} /* namespace shit */
