#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("file");

HELP_DESCRIPTION_DECL(
    "The source builtin reads the named file and runs it in the current shell, "
    "so its variable assignments and function definitions persist in the "
    "caller. It is also spelled as the dot command.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Source);

namespace shit {

Source::Source() = default;

pure Builtin::Kind Source::kind() const wontthrow { return Kind::Source; }

i32 Source::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (ec.args().count() < 2) return report_usage_error(ec, cxt, ec.program());

  /* A leading -- ends option parsing, the form source -- file that ble.sh uses,
     so it is skipped before the filename is read rather than taken as one. */
  usize path_index = 1;
  if (ec.args()[1] == "--") path_index = 2;
  if (path_index >= ec.args().count())
    return report_usage_error(ec, cxt, ec.program());

  let const path = ec.args()[path_index].clone();
  LOG(Info, "source running file '%s' in the current shell", path.c_str());
  let const contents = Path{path.view()}.read_entire_file();
  if (!contents)
    throw Error{"Unable to source the file '" + path +
                "' because it cannot be opened"};

  return cxt.run_source(*contents, "the file '" + path + "'", true,
                        ec.source_location(), StringView{path});
}

} // namespace shit
