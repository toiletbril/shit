#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* shift drops the first n positional parameters, n defaulting to 1. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Shift::Shift() = default;

pure Builtin::Kind Shift::kind() const wontthrow { return Kind::Shift; }

i32 Shift::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  i64 count = 1;
  if (ec.args().count() > 1) {
    let const parsed = utils::parse_decimal_integer(ec.args()[1]);
    if (parsed.is_error()) throw parsed.error();
    count = parsed.value();
  }

  let const &params = cxt.positional_params();
  if (count < 0 || static_cast<usize>(count) > params.count()) return 1;

  /* ArrayList has no erase, so the kept tail is copied into a fresh list from
     index count onward. */
  ArrayList<String> shifted{heap_allocator()};
  for (usize i = static_cast<usize>(count); i < params.count(); i++)
    shifted.push(String{heap_allocator(), params[i]});
  cxt.set_positional_params(steal(shifted));
  return 0;
}

} /* namespace shit */
