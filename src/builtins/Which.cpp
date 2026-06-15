#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
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

REGISTER_BUILTIN_FLAGS(Which);

namespace shit {

Which::Which() = default;

pure Builtin::Kind Which::kind() const wontthrow { return Kind::Which; }

i32 Which::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let output = String{};

  for (usize i = 1; i < args.count(); i++) {
    let const &program_name = args[i];
    LOG(Debug, "which resolving '%s' against builtins and PATH",
        program_name.c_str());
    if (search_builtin(program_name.view()).has_value()) {
      output += program_name;
      /* The descriptive suffix is for a human at a terminal. A pipe gets just
         the name, which stays machine readable. */
      if (os::is_stdout_a_tty()) output += ": Shell builtin";
      output += '\n';
    } else if (let const paths = utils::search_program_path(
                   program_name, FLAG_ALL.is_enabled());
               paths.count() != 0)
    {
      if (FLAG_ALL.is_enabled()) {
        for (let const &path : paths) {
          output += path.text();
          output += '\n';
        }
      } else {
        ASSERT(paths.count() > 0);
        output += paths[0].text();
        output += '\n';
      }
    }
  }

  ec.print_to_stdout(output);

  return (output.is_empty()) ? 1 : 0;
}

} /* namespace shit */
