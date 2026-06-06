#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-OPTIONS] <program> [program, ...]");

FLAG(ALL, Bool, 'a', "all", "Show all matches.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Which::Which() = default;

Builtin::Kind Which::kind() const { return Kind::Which; }

i32 Which::execute(ExecContext &ec, EvalContext &cxt) const
{
  unused(cxt);

  const ArrayList<String> args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  String buf{};

  for (usize i = 1; i < args.size(); i++) {
    const String &program_name = args[i];
    if (search_builtin(
            std::string_view{program_name.c_str(), program_name.size()})
            .has_value())
    {
      buf += program_name;
      /* The descriptive suffix is for a human at a terminal. A pipe gets just
         the name, which stays machine readable. */
      if (os::is_stdout_a_tty()) buf += ": Shell builtin";
      buf += '\n';
    } else if (const ArrayList<Path> ps = utils::search_program_path(program_name);
               ps.size() != 0)
    {
      if (FLAG_ALL.is_enabled()) {
        for (const Path &p : ps) {
          buf += p.text();
          buf += '\n';
        }
      } else {
        buf += ps[0].text();
        buf += '\n';
      }
    }
  }

  ec.print_to_stdout(buf);

  return (buf.empty()) ? 1 : 0;
}

} /* namespace shit */
