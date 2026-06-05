#include "../Eval.hpp"

#include "../Builtin.hpp"

/* No flags. The eval builtin joins its arguments with spaces and runs the
   result in the current shell. */

namespace shit {

Eval::Eval() = default;

Builtin::Kind Eval::kind() const { return Kind::Eval; }

i32 Eval::execute(ExecContext &ec, EvalContext &cxt) const
{
  std::string joined{};
  for (usize i = 1; i < ec.args().size(); i++) {
    if (i > 1) joined += ' ';
    joined.append(ec.args()[i].c_str(), ec.args()[i].size());
  }

  if (joined.empty()) return 0;

  /* eval leaves a return pending so it ends the enclosing function or the
     shell, the way dash propagates it, rather than ending the eval itself. */
  return cxt.run_source(joined, "eval", false);
}

} /* namespace shit */
