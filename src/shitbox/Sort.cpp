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

pure fn Sort::kind() const wontthrow -> Utility::Kind { return Kind::Sort; }

fn Sort::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  /* contents keeps each file's bytes alive for the line views, and the reserve
     stops a grow from dangling a view into a String's inline buffer. */
  ArrayList<String> contents{cxt.scratch_allocator()};
  contents.reserve(sources.count());
  ArrayList<StringView> lines{cxt.scratch_allocator()};
  i32 status = 0;
  for (const StringView &source : sources) {
    let const content = read_named_or_stdin(ec, source);
    if (os::INTERRUPT_REQUESTED) return 130;
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "sort: cannot read '" +
                                    String{cxt.scratch_allocator(), source} +
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

  let output = String{cxt.scratch_allocator()};
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

} // namespace shitbox

} // namespace shit
