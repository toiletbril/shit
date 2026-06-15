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

  ArrayList<String> lines{};
  i32 status = 0;
  for (const StringView &source : sources) {
    Maybe<String> content = read_named_or_stdin(ec, source);
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "sort: cannot read '" + String{source} +
                                    "': " + os::last_system_error_message());
      status = 2;
      continue;
    }

    for (const StringView &line : split_keep_newlines(content->view())) {
      let const body = !line.is_empty() && line[line.length - 1] == '\n'
                           ? line.substring_of_length(0, line.length - 1)
                           : line;
      lines.push(String{body});
    }
  }

  sort_string_list(lines);

  let output = String{};
  if (FLAG_SORT_REVERSE.is_enabled())
    for (usize i = lines.count(); i > 0; i--) {
      output += lines[i - 1].view();
      output += '\n';
    }
  else
    for (const String &line : lines) {
      output += line.view();
      output += '\n';
    }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace shitbox */

} /* namespace shit */
