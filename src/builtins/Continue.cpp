#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

namespace shit {

Continue::Continue() = default;

Builtin::Kind Continue::kind() const { return Kind::Continue; }

i32 Continue::execute(ExecContext &ec, EvalContext &cxt) const
{
  /* The optional argument is how many enclosing loops to skip, default one. */
  i64 level = 1;
  if (ec.args().size() > 1) {
    const ErrorOr<i64> parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    level = parsed.value();
  }
  if (level < 1) level = 1;

  cxt.request_continue(level, ec.source_location());
  return 0;
}

} /* namespace shit */
