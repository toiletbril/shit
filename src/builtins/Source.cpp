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

pure fn Source::kind() const wontthrow -> Builtin::Kind { return Kind::Source; }

fn Source::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
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
  if (!contents.has_value())
    throw ErrorWithLocation{ec.source_location(),
                            "Unable to source the file '" + path +
                                "': " + os::last_system_error_message()};

  /* Operands after the file become the positional parameters the sourced file
     reads as $1 upward, restored to the caller's afterward. With no extra
     operand the caller's parameters carry through unchanged, the way bash
     leaves them. Passing operands to the dot command is a bash extension, so
     the sh mood ignores them the way dash does. */
  let const has_extra_args =
      !cxt.is_posix_mode() && ec.args().count() > path_index + 1;
  let saved_params = ArrayList<String>{};
  if (has_extra_args) {
    let params = ArrayList<String>{};
    for (usize i = path_index + 1; i < ec.args().count(); i++)
      params.push_managed(ec.args()[i]);

    saved_params = cxt.take_positional_params();
    cxt.set_positional_params(steal(params));
  }
  defer
  {
    if (has_extra_args) cxt.set_positional_params(steal(saved_params));
  };

  return cxt.run_source(*contents, "the file '" + path + "'", true,
                        ec.source_location(), StringView{path});
}

} // namespace shit
