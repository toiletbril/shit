#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

#include <cstdio>

/* umask prints the current file-creation mask as octal with no argument, and
   sets it from an octal argument. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[mask]");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Umask::Umask() = default;

pure Builtin::Kind Umask::kind() const wontthrow { return Kind::Umask; }

cold i32 Umask::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() == 1) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%04o", os::get_file_creation_mask());
    ec.print_to_stdout(String{buffer} + "\n");
    return 0;
  }

  let const &requested = args[1];
  let const parsed = utils::parse_octal_integer(requested);
  if (parsed.is_error()) {
    throw Error{"'" + requested + "' is not a valid octal mask"};
  }
  os::set_file_creation_mask(static_cast<u32>(parsed.value()));

  return 0;
}

} /* namespace shit */
