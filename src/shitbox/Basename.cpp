#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("path [suffix]");

HELP_DESCRIPTION_DECL(
    "The basename utility prints the final component of the path, with a "
    "trailing suffix removed when one is given.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Basename);

namespace shit {

namespace shitbox {

Basename::Basename() = default;

pure Utility::Kind Basename::kind() const wontthrow { return Kind::Basename; }

fn Basename::execute(const ExecContext &ec, EvalContext &cxt,
                     const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  /* The path is held in a named local so its filename view does not dangle
     past a temporary. */
  let const path = Path{operands[0].view()};
  StringView name = path.filename();
  if (operands.count() > 1) {
    let const suffix = operands[1].view();
    if (suffix.length < name.length &&
        name.substring_of_length(name.length - suffix.length, suffix.length) ==
            suffix)
    {
      name = name.substring_of_length(0, name.length - suffix.length);
    }
  }

  ec.print_to_stdout(String{name} + "\n");
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
