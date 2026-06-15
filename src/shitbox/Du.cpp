#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-sh] [path ...]");

HELP_DESCRIPTION_DECL(
    "The du utility prints the total byte size of each path, descending into a "
    "directory and summing its files. The -s summary is the only mode and is "
    "the default. With -h the size is printed in a human-readable form.");

FLAG(DU_SUMMARY, Bool, 's', "",
     "Print only the total for each path, the default.");
FLAG(DU_HUMAN, Bool, 'h', "",
     "Print the size in a human-readable form such as 4.0K or 1.5M.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

/* The total byte size of a path, a file's own size or the sum of a directory's
   files reached recursively. A symlink is counted as its own small size and
   not followed, so a cycle cannot run forever. */
static fn total_size(const Path &path) throws -> u64
{
  if (path.is_directory() && !path.is_symbolic_link()) {
    u64 sum = 0;
    Maybe<ArrayList<String>> names = Path::read_directory(path);
    if (names.has_value())
      for (const String &name : *names) {
        let const child =
            PathBuilder{path.text().view()}.append(name.view()).build();
        sum += total_size(child);
      }
    return sum;
  }
  return path.file_size().value_or(0);
}

fn util_du(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  ArrayList<StringView> targets{};
  if (operands.is_empty())
    targets.push(StringView{"."});
  else
    for (const String &operand : operands)
      targets.push(operand.view());

  let output = String{};
  i32 status = 0;
  for (const StringView &target : targets) {
    let const path = Path{target};
    if (!path.exists()) {
      report_soft_shitbox_error(ec, cxt,
                                "du: cannot access '" + String{target} +
                                    "': no such file or directory");
      status = 1;
      continue;
    }
    let const total = total_size(path);
    output += FLAG_DU_HUMAN.is_enabled() ? format_human_size(total)
                                         : utils::uint_to_text(total);
    output += '\t';
    output += target;
    output += '\n';
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
