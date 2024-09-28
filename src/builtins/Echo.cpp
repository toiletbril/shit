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
Echo::execute(ExecContext &ec) const
{
  std::vector<std::string> args = BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) {
    SHOW_BUILTIN_HELP("echo", ec);
    return 0;
  }

  std::string buf{};

  if (args.size() > 1) {
    buf += args[1];
    for (usize i = 2; i < args.size(); i++) {
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
