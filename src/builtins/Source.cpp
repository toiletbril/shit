#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* The dot and source builtins read a file and run it in the current shell, so
   its assignments and function definitions persist in the caller. */

namespace shit {

Source::Source() = default;

pure Builtin::Kind Source::kind() const wontthrow { return Kind::Source; }

i32 Source::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() < 2) throw Error{"Filename argument is required"};

  const String path{ec.args()[1]};
  let const contents = utils::read_entire_file(path);
  if (!contents) throw Error{"Could not open '" + path + "'"};

  return cxt.run_source(*contents, "the file '" + path + "'", true,
                        ec.source_location(), StringView{path});
}

} /* namespace shit */
