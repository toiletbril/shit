#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

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
    const String &count_argument = ec.args()[1];
    std::string count_text{count_argument.c_str(), count_argument.size()};
    try {
      count = std::stoll(count_text);
    } catch (...) {
      throw Error{"'" + count_text + "' is not a number"};
    }
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
