#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* No flags. shift drops the first n positional parameters, n defaulting to 1.
 */

namespace shit {

Shift::Shift() = default;

Builtin::Kind
Shift::kind() const
{
  return Kind::Shift;
}

i32
Shift::execute(ExecContext &ec, EvalContext &cxt) const
{
  i64 count = 1;
  if (ec.args().size() > 1) {
    ErrorOr<i64> parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    count = parsed.value();
  }

  const ArrayList<String> &params = cxt.positional_params();
  if (count < 0 || static_cast<usize>(count) > params.size()) return 1;

  /* ArrayList has no erase, so the kept tail is copied into a fresh list from
     index count onward. */
  ArrayList<String> shifted{heap_allocator()};
  for (usize i = static_cast<usize>(count); i < params.size(); i++)
    shifted.push(String{heap_allocator(), params[i]});
  cxt.set_positional_params(std::move(shifted));
  return 0;
}

} /* namespace shit */
