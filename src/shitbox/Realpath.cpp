#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("path ...");

HELP_DESCRIPTION_DECL(
    "The realpath utility prints the absolute, normalized form of each path, "
    "resolving the . and .. components against the current directory.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Realpath);

namespace shit {

namespace shitbox {

Realpath::Realpath() = default;

pure fn Realpath::kind() const wontthrow -> Utility::Kind
{
  return Kind::Realpath;
}

fn Realpath::execute(const ExecContext &ec, EvalContext &cxt,
                     const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let output = String{cxt.scratch_allocator()};
  for (const String &operand : operands) {
    let const resolved = Path{operand.view()}.to_absolute().normalized();
    output += resolved.text().view();
    output += '\n';
  }
  ec.print_to_stdout(output);
  return 0;
}

} // namespace shitbox

} // namespace shit
