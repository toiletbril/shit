#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

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
  const ArrayList<String> &args = ec.args();

  if (args.size() == 1) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%04o", os::get_file_creation_mask());
    ec.print_to_stdout(String{buffer} + "\n");
    return 0;
  }

  const String &requested = args[1];
  ErrorOr<i64> parsed = utils::parse_octal_integer(requested);
  if (parsed.is_error()) {
    throw Error{"umask: '" + std::string{requested.c_str(), requested.size()} +
                "' is not a valid octal mask"};
  }
  os::set_file_creation_mask(static_cast<u32>(parsed.value()));

  return 0;
}

} /* namespace shit */
