#include "../Eval.hpp"

#include "../Builtin.hpp"

/* The eval builtin joins its arguments with spaces and runs the result in the
   current shell. */

namespace shit {

Eval::Eval() = default;

pure fn Eval::kind() const wontthrow -> Builtin::Kind { return Kind::Eval; }

fn Eval::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  let joined = std::string{};
  for (usize i = 1; i < ec.args().count(); i++) {
    if (i > 1) joined += ' ';
    joined.append(ec.args()[i].c_str(), ec.args()[i].count());
  }

  if (joined.empty()) return 0;

  /* eval leaves a return pending so it ends the enclosing function or the
     shell, the way dash propagates it, rather than ending the eval itself. */
  return cxt.run_source(joined, "eval", false, ec.source_location(),
                        StringView{"eval"});
}

} /* namespace shit */
