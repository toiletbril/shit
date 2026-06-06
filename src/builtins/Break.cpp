#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

namespace shit {

Break::Break() = default;

pure fn Break::kind() const wontthrow -> Builtin::Kind { return Kind::Break; }

fn Break::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  /* The optional argument is how many enclosing loops to break, default one. */
  i64 level = 1;
  if (ec.args().count() > 1) {
    let const parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    level = parsed.value();
  }
  if (level < 1) level = 1;

  cxt.request_break(level, ec.source_location());
  return 0;
}

} /* namespace shit */
