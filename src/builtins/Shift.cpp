#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* No flags. shift drops the first n positional parameters, n defaulting to 1. */

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
    try {
      count = std::stoll(ec.args()[1]);
    } catch (...) {
      throw Error{"'" + ec.args()[1] + "' is not a number"};
    }
  }

  std::vector<std::string> params = cxt.positional_params();
  if (count < 0 || static_cast<usize>(count) > params.size()) return 1;

  params.erase(params.begin(), params.begin() + count);
  cxt.set_positional_params(std::move(params));
  return 0;
}

} /* namespace shit */
