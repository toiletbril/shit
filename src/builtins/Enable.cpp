#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] [-n] [-f filename] [-s] [name ...]");
HELP_DESCRIPTION_DECL(
    "The enable builtin lists and toggles shell builtins. In shit every "
    "builtin is always enabled, so toggling is accepted without effect and "
    "the listing is the only observable output.");

FLAG(ALL, Bool, 'a', "", "List every builtin.");
FLAG(DISABLE, Bool, 'n', "", "Accepted without effect.");
FLAG(LOAD_FILE, String, 'f', "", "Accepted without effect.");
FLAG(SILENT, Bool, 's', "", "Accepted without effect.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Enable);

namespace shit {

Enable::Enable() = default;

pure fn Enable::kind() const wontthrow -> Builtin::Kind { return Kind::Enable; }

fn Enable::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  if (FLAG_ALL.is_enabled()) {
    let out = String{cxt.scratch_allocator()};
    for (let const &name : builtin_names()) {
      out += "enable ";
      out += name;
      out += '\n';
    }
    ec.print_to_stdout(out);
    return 0;
  }

  if (args.count() == 1) return 0;

  i32 status = 0;
  for (usize i = 1; i < args.count(); i++) {
    let const &name = args[i];
    let const resolved = search_builtin(name.view());
    if (!resolved.has_value()) {
      report_soft_builtin_error(
          ec, cxt, StringView{"'"} + name + "' is not a shell builtin");
      status = 1;
    }
  }

  return status;
}

} // namespace shit
