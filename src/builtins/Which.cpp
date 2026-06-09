#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] program [program ...]");

HELP_DESCRIPTION_DECL(
    "The which builtin prints how each named program resolves, naming it a "
    "shell builtin or printing its PATH location. With -a it prints every PATH "
    "match instead of the first. The status is non-zero when no name "
    "resolves.");

FLAG(ALL, Bool, 'a', "all", "Show all matches.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Which::Which() = default;

pure Builtin::Kind Which::kind() const wontthrow { return Kind::Which; }

i32 Which::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let buf = String{};

  for (usize i = 1; i < args.count(); i++) {
    let const &program_name = args[i];
    if (search_builtin(program_name.view()).has_value()) {
      buf += program_name;
      /* The descriptive suffix is for a human at a terminal. A pipe gets just
         the name, which stays machine readable. */
      if (os::is_stdout_a_tty()) buf += ": Shell builtin";
      buf += '\n';
    } else if (let const ps = utils::search_program_path(program_name,
                                                         FLAG_ALL.is_enabled());
               ps.count() != 0)
    {
      if (FLAG_ALL.is_enabled()) {
        for (let const &p : ps) {
          buf += p.text();
          buf += '\n';
        }
      } else {
        ASSERT(ps.count() > 0);
        buf += ps[0].text();
        buf += '\n';
      }
    }
  }

  ec.print_to_stdout(buf);

  return (buf.is_empty()) ? 1 : 0;
}

} /* namespace shit */
