#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

namespace shit {

Continue::Continue() = default;

fn Continue::kind() const -> Builtin::Kind { return Kind::Continue; }

fn Continue::execute(ExecContext &ec, EvalContext &cxt) const -> i32
{
  ASSERT(!ec.args().empty());

  /* The optional argument is how many enclosing loops to skip, default one. */
  i64 level = 1;
  if (ec.args().size() > 1) {
    let const parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    level = parsed.value();
  }
  if (level < 1) level = 1;

  cxt.request_continue(level, ec.source_location());
  return 0;
}

} /* namespace shit */
