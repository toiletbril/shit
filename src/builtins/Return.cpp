#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Return::Return() = default;

pure Builtin::Kind Return::kind() const wontthrow { return Kind::Return; }

i32 Return::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* return with no argument uses the status of the last command. */
  i64 status = cxt.last_exit_status();
  if (ec.args().count() > 1) {
    let const parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    status = parsed.value();
  }

  cxt.request_return(status, ec.source_location());
  return static_cast<i32>(status);
}

} /* namespace shit */
