#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");
HELP_DESCRIPTION_DECL(
    "The pwd builtin prints the absolute path of the current working directory "
    "to standard output.");

FLAG(PWD_LOGICAL, Bool, 'L', "",
     "Print the logical directory, keeping the symlinks the path was reached "
     "through. This is the default.");
FLAG(PWD_PHYSICAL, Bool, 'P', "",
     "Print the physical directory, with every symlink resolved.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Pwd);

namespace shit {

Pwd::Pwd() = default;

pure fn Pwd::kind() const wontthrow -> Builtin::Kind { return Kind::Pwd; }

fn Pwd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let output = String{};
  /* The physical form resolves every symlink through getcwd. The logical form,
     the POSIX default, prints PWD so the path keeps the symlinks it was reached
     through, the way bash and dash do, so cd /tmp on a system where /tmp is a
     symlink still reports /tmp. PWD is used only when it names an absolute
     path, otherwise an unset or relative value falls back to the physical
     directory so a path is always printed. */
  let const want_physical = FLAG_PWD_PHYSICAL.is_enabled();
  let const logical_pwd = cxt.get_variable_value("PWD");
  if (!want_physical && logical_pwd.has_value() && !logical_pwd->is_empty() &&
      logical_pwd->view()[0] == '/')
  {
    LOG(Debug, "pwd printing the logical directory from PWD");
    output.append(logical_pwd->view());
  } else {
    LOG(Debug, "pwd printing the physical directory from getcwd");
    output.append(Path::current_directory().text());
  }
  output += '\n';
  ec.print_to_stdout(output);
  return 0;
}

} // namespace shit
