#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL(
    "The shift builtin drops the leading positional parameters and renumbers "
    "the rest.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Shift);

namespace shit {

Shift::Shift() = default;

pure fn Shift::kind() const wontthrow -> Builtin::Kind { return Kind::Shift; }

fn Shift::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const shift_count = parse_optional_integer_arg(ec, 1);

  let const &params = cxt.positional_params();
  if (shift_count < 0 || static_cast<usize>(shift_count) > params.count()) {
    if (cxt.is_shopt_enabled("shift_verbose"))
      report_soft_builtin_error(ec, cxt, "shift count out of range");
    return 1;
  }

  LOG(All, "shift dropping %lld of %zu positional parameters",
      static_cast<long long>(shift_count), params.count());

  let shifted = ArrayList<String>{heap_allocator()};
  shifted.reserve(params.count() - static_cast<usize>(shift_count));
  for (usize i = static_cast<usize>(shift_count); i < params.count(); i++)
    shifted.push_managed(params[i]);
  cxt.set_positional_params(steal(shifted));
  return 0;
}

} // namespace shit
