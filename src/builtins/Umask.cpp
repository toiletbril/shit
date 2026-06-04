#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"

#include <cstdio>

/* No flags. umask prints the current file-creation mask as octal with no
   argument, and sets it from an octal argument. */

namespace shit {

Umask::Umask() = default;

Builtin::Kind
Umask::kind() const
{
  return Kind::Umask;
}

i32
Umask::execute(ExecContext &ec, EvalContext &cxt) const
{
  SHIT_UNUSED(cxt);
  const std::vector<std::string> &args = ec.args();

  if (args.size() == 1) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%04o", os::get_file_creation_mask());
    ec.print_to_stdout(std::string{buffer} + "\n");
    return 0;
  }

  try {
    u32 mask = static_cast<u32>(std::stoul(args[1], nullptr, 8));
    os::set_file_creation_mask(mask);
  } catch (...) {
    throw Error{"umask: '" + args[1] + "' is not a valid octal mask"};
  }

  return 0;
}

} /* namespace shit */
