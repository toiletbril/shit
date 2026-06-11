#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* The dot and source builtins read a file and run it in the current shell, so
   its assignments and function definitions persist in the caller. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("file");

HELP_DESCRIPTION_DECL(
    "The source builtin reads the named file and runs it in the current shell, "
    "so its variable assignments and function definitions persist in the "
    "caller. It is also spelled as the dot command.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Source::Source() = default;

pure Builtin::Kind Source::kind() const wontthrow { return Kind::Source; }

i32 Source::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (ec.args().count() < 2)
    throw Error{"Unable to source because a filename argument is required"};

  /* A leading -- ends option parsing, the form source -- file that ble.sh uses,
     so it is skipped before the filename is read rather than taken as one. */
  usize path_index = 1;
  if (ec.args()[1] == "--") path_index = 2;
  if (path_index >= ec.args().count())
    throw Error{"Unable to source because a filename argument is required"};

  let const path = ec.args()[path_index].clone();
  LOG(verbosity::Info, "source running file '%s' in the current shell",
      path.c_str());
  let const contents = utils::read_entire_file(path);
  if (!contents)
    throw Error{"Unable to source the file '" + path +
                "' because it cannot be opened"};

  return cxt.run_source(*contents, "the file '" + path + "'", true,
                        ec.source_location(), StringView{path});
}

} /* namespace shit */
