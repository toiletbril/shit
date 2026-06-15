#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-r] [file ...]");

HELP_DESCRIPTION_DECL(
    "The sort utility writes the lines of its input in byte order, reading "
    "standard input when no file is given. With -r the order is reversed.");

FLAG(SORT_REVERSE, Bool, 'r', "", "Reverse the order of the output.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Sort);

namespace shit {

namespace shitbox {

Sort::Sort() = default;

pure Utility::Kind Sort::kind() const wontthrow { return Kind::Sort; }

fn Sort::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  ArrayList<StringView> sources{};
  if (operands.is_empty())
    sources.push(StringView{"-"});
  else
    for (const String &operand : operands)
      sources.push(operand.view());

  /* Each file's bytes are kept alive in contents so the line views into them
     stay valid, and the reserve keeps the elements from moving, which would
     dangle a view into a small file held in a String's inline buffer. The lines
     then sort as views with no per-line copy. */
  ArrayList<String> contents{};
  contents.reserve(sources.count());
  ArrayList<StringView> lines{};
  i32 status = 0;
  for (const StringView &source : sources) {
    Maybe<String> content = read_named_or_stdin(ec, source);
    /* A Ctrl-C during the read returns 130 rather than freezing the utility. */
    if (os::INTERRUPT_REQUESTED) return 130;
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "sort: cannot read '" + String{source} +
                                    "': " + os::last_system_error_message());
      status = 2;
      continue;
    }

    contents.push(steal(*content));
    for (const StringView &line : split_keep_newlines(contents.back().view())) {
      let const body = !line.is_empty() && line[line.length - 1] == '\n'
                           ? line.substring_of_length(0, line.length - 1)
                           : line;
      lines.push(body);
    }
  }

  sort_stringview_list(lines);

  let output = String{};
  if (FLAG_SORT_REVERSE.is_enabled())
    for (usize i = lines.count(); i > 0; i--) {
      output += lines[i - 1];
      output += '\n';
    }
  else
    for (const StringView &line : lines) {
      output += line;
      output += '\n';
    }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace shitbox */

} /* namespace shit */
