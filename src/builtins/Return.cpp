#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* No flags. */

namespace shit {

Return::Return() = default;

Builtin::Kind Return::kind() const { return Kind::Return; }

i32 Return::execute(ExecContext &ec, EvalContext &cxt) const
{
  /* return with no argument uses the status of the last command. */
  i64 status = cxt.last_exit_status();
  if (ec.args().size() > 1) {
    ErrorOr<i64> parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    status = parsed.value();
  }

  cxt.request_return(status, ec.source_location());
  return static_cast<i32>(status);
}

} /* namespace shit */
