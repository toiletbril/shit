#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-c] [file]");

HELP_DESCRIPTION_DECL(
    "The uniq utility collapses each run of adjacent equal lines into one, "
    "reading standard input when no file is given. With -c it prefixes the "
    "count of each run.");

FLAG(UNIQ_COUNT, Bool, 'c', "", "Prefix each line with the count of its run.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

static fn count_prefix(u64 run_length) throws -> String
{
  let const digits = utils::uint_to_text(run_length);
  String prefix{};
  for (usize i = digits.count(); i < 7; i++)
    prefix += ' ';
  prefix += digits.view();
  prefix += ' ';
  return prefix;
}

fn util_uniq(const ExecContext &ec, EvalContext &cxt,
             const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const source = operands.is_empty() ? StringView{"-"} : operands[0].view();
  Maybe<String> content = read_named_or_stdin(ec, source);
  if (!content.has_value())
    throw Error{"uniq: cannot read '" + String{source} +
                "': " + os::last_system_error_message()};

  let const show_count = FLAG_UNIQ_COUNT.is_enabled();
  let output = String{};
  bool have_previous = false;
  StringView previous{};
  u64 run_length = 0;

  let const flush = [&]() throws -> void {
    if (!have_previous) return;
    if (show_count) output += count_prefix(run_length);
    output += previous;
    output += '\n';
  };

  for (const StringView &line : split_keep_newlines(content->view())) {
    let const body = !line.is_empty() && line[line.length - 1] == '\n'
                         ? line.substring_of_length(0, line.length - 1)
                         : line;
    if (have_previous && body == previous) {
      run_length++;
      continue;
    }
    flush();
    previous = body;
    run_length = 1;
    have_previous = true;
  }
  flush();

  ec.print_to_stdout(output);
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
