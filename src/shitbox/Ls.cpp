#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a1l] [path ...]");

HELP_DESCRIPTION_DECL(
    "The ls utility lists the names in each directory operand, or the current "
    "directory when none is given, one per line in byte order.");

FLAG(LS_ALL, Bool, 'a', "", "List entries whose name starts with a dot.");
FLAG(LS_ONE, Bool, '1', "", "List one entry per line, the default.");
FLAG(LS_LONG, Bool, 'l', "",
     "Print a type letter and the byte size before each name.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

/* One entry's line, the bare name or, under -l, a type letter and the size in
   front of it. The type is d for a directory, l for a symlink, and - for a
   regular file. */
static fn format_entry(const Path &entry, StringView name) throws -> String
{
  if (!FLAG_LS_LONG.is_enabled()) return String{name} + "\n";

  char type = '-';
  if (entry.is_symbolic_link())
    type = 'l';
  else if (entry.is_directory())
    type = 'd';

  let const size = entry.file_size().value_or(0);
  String line{};
  line.push(type);
  line += ' ';
  line += utils::uint_to_text(size);
  line += ' ';
  line += name;
  line += '\n';
  return line;
}

fn util_ls(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  ArrayList<StringView> targets{};
  if (operands.is_empty())
    targets.push(StringView{"."});
  else
    for (const String &operand : operands)
      targets.push(operand.view());

  let output = String{};
  let const print_headers = targets.count() > 1;
  i32 status = 0;
  for (usize t = 0; t < targets.count(); t++) {
    let const target = targets[t];
    let const path = Path{target};
    if (!path.exists()) {
      report_soft_shitbox_error(ec, cxt,
                                "ls: cannot access '" + String{target} +
                                    "': no such file or directory");
      status = 2;
      continue;
    }

    if (!path.is_directory()) {
      output += format_entry(path, target);
      continue;
    }

    Maybe<ArrayList<String>> names = Path::read_directory(path);
    if (!names.has_value()) {
      report_soft_shitbox_error(
          ec, cxt, "ls: cannot open directory '" + String{target} + "'");
      status = 2;
      continue;
    }
    sort_string_list(*names);

    if (print_headers) {
      if (t > 0) output += '\n';
      output += target;
      output += ":\n";
    }
    for (const String &name : *names) {
      if (!FLAG_LS_ALL.is_enabled() && !name.is_empty() &&
          name.view()[0] == '.')
        continue;
      let const child = PathBuilder{target}.append(name.view()).build();
      output += format_entry(child, name.view());
    }
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
