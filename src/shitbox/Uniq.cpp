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

REGISTER_SHITBOX_UTIL_FLAGS(Uniq);

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

Uniq::Uniq() = default;

pure Utility::Kind Uniq::kind() const wontthrow { return Kind::Uniq; }

fn Uniq::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  let const source = operands.is_empty() ? StringView{"-"} : operands[0].view();
  Maybe<String> content = read_named_or_stdin(ec, source);
  /* A Ctrl-C during the read returns 130 rather than freezing the utility. */
  if (os::INTERRUPT_REQUESTED) return 130;
  if (!content.has_value())
    throw Error{"uniq: cannot read '" + String{source} +
                "': " + os::last_system_error_message()};

  let const should_show_count = FLAG_UNIQ_COUNT.is_enabled();
  let output = String{};
  bool has_previous = false;
  StringView previous{};
  u64 run_length = 0;

  let const do_flush = [&]() throws -> void {
    if (!has_previous) return;
    if (should_show_count) output += count_prefix(run_length);
    output += previous;
    output += '\n';
  };

  for (const StringView &line : split_keep_newlines(content->view())) {
    let const body = !line.is_empty() && line[line.length - 1] == '\n'
                         ? line.substring_of_length(0, line.length - 1)
                         : line;
    if (has_previous && body == previous) {
      run_length++;
      continue;
    }

    do_flush();
    previous = body;
    run_length = 1;
    has_previous = true;
  }

  do_flush();

  ec.print_to_stdout(output);
  return 0;
}

} // namespace shitbox

} // namespace shit
