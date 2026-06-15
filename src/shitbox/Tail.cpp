#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n count] [file ...]");

HELP_DESCRIPTION_DECL(
    "The tail utility writes the last count lines of each file, ten by "
    "default, reading standard input when no file is given.");

FLAG(TAIL_LINES, String, 'n', "", "Write the last count lines.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Tail);

namespace shit {

namespace shitbox {

fn util_tail(const ExecContext &ec, EvalContext &cxt,
             const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  i64 count = 10;
  if (FLAG_TAIL_LINES.is_set()) {
    let const parsed = utils::parse_decimal_integer(FLAG_TAIL_LINES.value());
    if (parsed.is_error() || parsed.value() < 0)
      throw Error{"tail: invalid line count '" +
                  String{FLAG_TAIL_LINES.value()} + "'"};
    count = parsed.value();
  }

  ArrayList<StringView> sources{};
  if (operands.is_empty())
    sources.push(StringView{"-"});
  else
    for (const String &operand : operands)
      sources.push(operand.view());

  let const print_headers = sources.count() > 1;
  let output = String{};
  i32 status = 0;
  for (usize s = 0; s < sources.count(); s++) {
    Maybe<String> content = read_named_or_stdin(ec, sources[s]);
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "tail: cannot open '" + String{sources[s]} +
                                    "': " + os::last_system_error_message());
      status = 1;
      continue;
    }
    if (print_headers) {
      if (s > 0) output += '\n';
      output += "==> ";
      output += sources[s];
      output += " <==\n";
    }
    let const lines = split_keep_newlines(content->view());
    let const want = static_cast<usize>(count);
    let const start = lines.count() > want ? lines.count() - want : 0;
    for (usize i = start; i < lines.count(); i++)
      output += lines[i];
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
