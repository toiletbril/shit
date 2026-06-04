#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

#include <fstream>
#include <sstream>

/* No flags. The dot and source builtins read a file and run it in the current
   shell, so its assignments and function definitions persist in the caller. */

namespace shit {

Source::Source() = default;

Builtin::Kind
Source::kind() const
{
  return Kind::Source;
}

i32
Source::execute(ExecContext &ec, EvalContext &cxt) const
{
  if (ec.args().size() < 2)
    throw Error{"filename argument is required"};

  const std::string &path = ec.args()[1];
  std::ifstream file{path, std::ios::binary};
  if (!file)
    throw Error{"could not open '" + path + "'"};

  std::ostringstream contents{};
  contents << file.rdbuf();

  return cxt.run_source(contents.str(), "the file '" + path + "'");
}

} /* namespace shit */
