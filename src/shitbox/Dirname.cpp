#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("path");

HELP_DESCRIPTION_DECL(
    "The dirname utility prints the directory part of a path.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Dirname);

namespace shit {

namespace shitbox {

Dirname::Dirname() = default;

pure fn Dirname::kind() const wontthrow -> Utility::Kind
{
  return Kind::Dirname;
}

fn Dirname::execute(const ExecContext &ec, EvalContext &cxt,
                    const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const parent = Path{operands[0].view()}.parent();
  let const text = parent.is_empty() ? StringView{"."} : parent.text().view();
  ec.print_to_stdout(String{cxt.scratch_allocator(), text} + "\n");

  return 0;
}

} // namespace shitbox

} // namespace shit
