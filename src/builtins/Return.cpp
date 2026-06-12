#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL(
    "The return builtin stops the current function or sourced file and returns "
    "the status n to its caller. With no argument it returns the status of the "
    "last command run.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Return);

namespace shit {

Return::Return() = default;

pure Builtin::Kind Return::kind() const wontthrow { return Kind::Return; }

i32 Return::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* return with no argument uses the status of the last command. */
  let const status = parse_optional_integer_arg(ec, cxt.last_exit_status());

  LOG(verbosity::Debug, "return stopping the enclosing scope with status %lld",
      static_cast<long long>(status));
  cxt.request_return(status, ec.source_location());
  return static_cast<i32>(status);
}

} /* namespace shit */
