#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL(
    "The return builtin stops the current function or sourced file and returns "
    "a status.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Return);

namespace shit {

Return::Return() = default;

pure fn Return::kind() const wontthrow -> Builtin::Kind { return Kind::Return; }

fn Return::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The default mood rejects a return outside a function or a sourced file,
     while the sh mood lets it end the script the way dash does. */
  if (!cxt.is_posix_mode() && !cxt.in_function_scope() && !cxt.is_sourcing()) {
    report_soft_builtin_error(
        ec, cxt, "can only `return' from a function or sourced script");
    return 2;
  }

  let const status = parse_optional_integer_arg(ec, cxt.last_exit_status());

  LOG(Debug, "return stopping the enclosing scope with status %lld",
      static_cast<long long>(status));
  cxt.request_return(status, ec.source_location());
  return static_cast<i32>(status);
}

} // namespace shit
