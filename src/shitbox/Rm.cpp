#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-rRf] path ...");

HELP_DESCRIPTION_DECL(
    "The rm utility removes each path. With -r it removes a directory and its "
    "contents. With -f a missing path is not an error.");

FLAG(RM_RECURSIVE_R, Bool, 'r', "", "Remove directories and their contents.");
FLAG(RM_RECURSIVE_UPPER, Bool, 'R', "",
     "Remove directories and their contents.");
FLAG(RM_FORCE, Bool, 'f', "", "Ignore a missing path and never prompt.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Rm);

namespace shit {

namespace shitbox {

fn remove_path(StringView path, bool is_recursive) throws -> bool
{
  let const target = Path{path};
  if (is_recursive && target.is_directory() && !target.is_symbolic_link()) {
    Maybe<ArrayList<String>> names = Path::read_directory(target);
    if (names.has_value())
      for (const String &name : *names) {
        let const child = PathBuilder{path}.append(name.view()).build();
        if (!remove_path(child.text().view(), is_recursive)) return false;
      }
    return os::remove_directory(path);
  }
  return os::remove_file(path);
}

/* Whether the operand names the . or .. directory entry, its basename after any
   trailing slashes. POSIX requires rm to refuse such an operand so a recursive
   remove cannot delete the working or the parent directory entry, and the -f
   flag does not waive the refusal. */
static fn names_dot_or_dotdot(StringView operand) wontthrow -> bool
{
  usize end = operand.length;
  while (end > 1 && operand[end - 1] == '/')
    end--;

  usize start = end;
  while (start > 0 && operand[start - 1] != '/')
    start--;

  let const base = operand.substring_of_length(start, end - start);
  return base == StringView{"."} || base == StringView{".."};
}

/* Whether the operand names the root directory, every byte a slash. A recursive
   remove of / would walk the whole filesystem, so rm refuses it the way GNU rm
   does under its default preserve-root, and the -f flag does not waive it. The
   dot guard already covers /. and /.. through their basename. */
static fn names_root_directory(StringView operand) wontthrow -> bool
{
  if (operand.length == 0) return false;

  for (usize i = 0; i < operand.length; i++)
    if (operand[i] != '/') return false;

  return true;
}

Rm::Rm() = default;

pure Utility::Kind Rm::kind() const wontthrow { return Kind::Rm; }

fn Rm::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const should_force = FLAG_RM_FORCE.is_enabled();
  let const is_recursive =
      FLAG_RM_RECURSIVE_R.is_enabled() || FLAG_RM_RECURSIVE_UPPER.is_enabled();

  if (operands.is_empty() && !should_force) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  i32 status = 0;
  for (const String &operand : operands) {
    if (names_dot_or_dotdot(operand.view())) {
      report_soft_shitbox_error(ec, cxt,
                                "rm: refusing to remove '.' or '..' directory: "
                                "skipping '" +
                                    operand + "'");
      status = 1;
      continue;
    }

    if (names_root_directory(operand.view())) {
      report_soft_shitbox_error(
          ec, cxt,
          "rm: refusing to remove the root directory: skipping '" + operand +
              "'");
      status = 1;
      continue;
    }

    if (!Path{operand.view()}.exists()) {
      if (should_force) continue;
      report_soft_shitbox_error(ec, cxt,
                                "rm: cannot remove '" + operand +
                                    "': no such file or directory");
      status = 1;
      continue;
    }
    if (!remove_path(operand.view(), is_recursive)) {
      if (should_force) continue;
      report_soft_shitbox_error(ec, cxt,
                                "rm: cannot remove '" + operand +
                                    "': " + os::last_system_error_message());
      status = 1;
    }
  }
  return status;
}

} // namespace shitbox

} // namespace shit
