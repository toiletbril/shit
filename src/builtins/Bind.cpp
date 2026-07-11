#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-lmrvpPsSq] [-f filename] [-q function] [-u function] "
                   "[keyseq: function ...]");

HELP_DESCRIPTION_DECL(
    "The bind builtin accepts readline key bindings without effect. The "
    "shit editor is the vendored toiletline, so key binding is not wired "
    "through this builtin.");

FLAG(LIST_NAMES, Bool, 'l', "", "Accepted without effect.");
FLAG(READLINE_META, Bool, 'm', "", "Accepted without effect.");
FLAG(REMOVE, Bool, 'r', "", "Accepted without effect.");
FLAG(PRINT, Bool, 'p', "", "Accepted without effect.");
FLAG(PRINT_ALL, Bool, 'P', "", "Accepted without effect.");
FLAG(QUERY, Bool, 'q', "", "Accepted without effect.");
FLAG(SHOW_HELP, Bool, 's', "", "Accepted without effect.");
FLAG(VARIABLE, Bool, 'v', "", "Accepted without effect.");
FLAG(FILENAME, String, 'f', "", "Accepted without effect.");
FLAG(UNBIND, String, 'u', "", "Accepted without effect.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Bind);

namespace shit {

Bind::Bind() = default;

pure fn Bind::kind() const wontthrow -> Builtin::Kind { return Kind::Bind; }

fn Bind::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(All, "bind accepted as a no-op with %zu operands", ec.args().count() - 1);
  return 0;
}

} // namespace shit
