#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-p] [-m mode] directory ...");

HELP_DESCRIPTION_DECL(
    "The mkdir utility creates each named directory. With -p it creates the "
    "missing parents and treats an existing directory as success. With -m it "
    "sets the file mode of the named directory from an octal operand.");

FLAG(MKDIR_PARENTS, Bool, 'p', "",
     "Create the missing parent directories and ignore one that already "
     "exists.");
FLAG(MKDIR_MODE, String, 'm', "",
     "Set the file mode of the named directory, an octal operand.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Mkdir);

namespace shit {

namespace shitbox {

/* Create one directory, with the -p case treating an already-present directory
   as success the way mkdir -p does. */
static fn make_one(StringView path, u32 mode, bool set_exact_mode,
                   bool ignore_existing) throws -> bool
{
  if (os::make_directory(path, mode)) {
    /* The create narrows the bits by the umask, so an explicit -m re-applies
       the exact mode the way POSIX mkdir sets it. A failed chmod leaves the
       directory at the wrong mode, so it reports rather than passing. */
    if (set_exact_mode && !os::set_file_mode(path, mode)) return false;
    return true;
  }

  if (ignore_existing && Path{path}.is_directory()) return true;
  return false;
}

Mkdir::Mkdir() = default;

pure Utility::Kind Mkdir::kind() const wontthrow { return Kind::Mkdir; }

fn Mkdir::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const should_make_parents = FLAG_MKDIR_PARENTS.is_enabled();

  /* The named directory takes the -m mode, the -p parents keep the default
     0777 masked by the umask the way GNU mkdir applies the mode. */
  u32 named_mode = 0777;
  if (FLAG_MKDIR_MODE.is_set()) {
    let const parsed = utils::parse_octal_integer(FLAG_MKDIR_MODE.value());
    /* parse_octal_integer accepts a sign and saturates on overflow without an
       error, so a sign-prefixed or oversized operand parses cleanly. The range
       check rejects it rather than truncating to an over-permissive mode. */
    if (parsed.is_error() || parsed.value() < 0 || parsed.value() > 07777)
      throw Error{"mkdir: invalid mode '" + String{FLAG_MKDIR_MODE.value()} +
                  "'"};

    named_mode = static_cast<u32>(parsed.value());
  }

  i32 status = 0;
  for (const String &operand : operands) {
    if (should_make_parents) {
      /* Each prefix of the path is created in turn, so a/b/c builds a, then
         a/b, then a/b/c, with an existing prefix passed over. The named
         directory at the full length takes the -m mode. */
      let const text = operand.view();
      for (usize i = 1; i <= text.length; i++) {
        if (i < text.length && text[i] != '/') continue;
        let const prefix = text.substring_of_length(0, i);
        if (prefix.is_empty()) continue;
        let const is_named_directory = (i == text.length);
        let const mode = is_named_directory ? named_mode : 0777;
        let const should_set_exact_mode =
            is_named_directory && FLAG_MKDIR_MODE.is_set();
        if (!make_one(prefix, mode, should_set_exact_mode, true)) {
          report_soft_shitbox_error(
              ec, cxt,
              "mkdir: cannot create directory '" + String{prefix} +
                  "': " + os::last_system_error_message());
          status = 1;
          break;
        }
      }
    } else if (!make_one(operand.view(), named_mode, FLAG_MKDIR_MODE.is_set(),
                         false))
    {
      report_soft_shitbox_error(ec, cxt,
                                "mkdir: cannot create directory '" + operand +
                                    "': " + os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} // namespace shitbox

} // namespace shit
