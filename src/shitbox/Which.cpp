#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] program [program ...]");

HELP_DESCRIPTION_DECL(
    "The which utility prints how each named program resolves, naming it a "
    "shell builtin or printing its PATH location. With -a it prints every PATH "
    "match instead of the first. The status is non-zero when no name "
    "resolves.");

FLAG(ALL, Bool, 'a', "all", "Show all matches.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Which);

namespace shit {

namespace shitbox {

Which::Which() = default;

pure Utility::Kind Which::kind() const wontthrow { return Kind::Which; }

fn Which::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);

  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let output = String{};

  for (let const &program_name : operands) {
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
        output += paths[0].text();
        output += '\n';
      }
    }
  }

  ec.print_to_stdout(output);

  return output.is_empty() ? 1 : 0;
}

} /* namespace shitbox */

} /* namespace shit */
