#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* No flags. */

namespace shit {

Break::Break() = default;

Builtin::Kind
Break::kind() const
{
  return Kind::Break;
}

i32
Break::execute(ExecContext &ec, EvalContext &cxt) const
{
  /* The optional argument is how many enclosing loops to break, default one. */
  i64 level = 1;
  if (ec.args().size() > 1) {
    ErrorOr<i64> parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    level = parsed.value();
  }
  if (level < 1) level = 1;

  cxt.request_break(level, ec.source_location());
  return 0;
}

} /* namespace shit */
