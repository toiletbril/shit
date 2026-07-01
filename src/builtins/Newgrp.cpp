#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[group]");
HELP_DESCRIPTION_DECL(
    "The newgrp builtin changes the group of the shell.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Newgrp);

namespace shit {

Newgrp::Newgrp() = default;

pure fn Newgrp::kind() const wontthrow -> Builtin::Kind { return Kind::Newgrp; }

fn Newgrp::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The group change must outlive this command, so it re-execs system newgrp.
   */
  let const found = utils::search_program_path("newgrp");
  if (found.count() == 0) {
    show_message(
        "Unable to run newgrp because the newgrp program was not found");
    return 127;
  }

  let command_args = ArrayList<String>{heap_allocator()};
  command_args.push_managed("newgrp");
  for (usize i = 1; i < args.count(); i++)
    command_args.push_managed(args[i]);

  let command = ExecContext::from_resolved(
      ec.source_location(), ResolvedCommand::from_program(found[0]),
      steal(command_args));

  LOG(Info, "newgrp handing the shell off to '%s'", found[0].text().c_str());

  try {
    os::replace_process(steal(command));
  } catch (const Error &error) {
    show_message(error.to_string());
    return 126;
  }
}

} // namespace shit
