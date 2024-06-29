#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-OPTIONS] <...>");

FLAG(NO_NEWLINE, Bool, 'n', "no-newlines",
     "Do not output the trailing newline.");
FLAG(ESCAPES, Bool, 'e', "escapes",
     "UNIMPLEMENTED: Enable interpretation of backslash escapes.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Echo::Echo() = default;

Builtin::Kind
Echo::kind() const
{
  return Kind::Echo;
}

i32
Echo::execute(utils::ExecContext &ec) const
{
  std::vector<std::string> args = parse_flags_vec(FLAG_LIST, ec.args());

  if (FLAG_HELP.is_enabled()) {
    std::string h{};
    h += make_synopsis("echo", HELP_SYNOPSIS);
    h += '\n';
    h += make_flag_help(FLAG_LIST);
    h += '\n';
    ec.print_to_stdout(h);
    return 0;
  }

  std::string buf{};

  if (args.size() > 0) {
    buf += args[0];
    for (usize i = 1; i < args.size(); i++) {
      buf += ' ';
      buf += args[i];
    }
  }
  if (!FLAG_NO_NEWLINE.is_enabled()) {
    buf += '\n';
  }

  ec.print_to_stdout(buf);

  return 0;
}

} /* namespace shit */
