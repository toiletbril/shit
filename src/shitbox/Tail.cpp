#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n count] [-c count] [file ...]");

HELP_DESCRIPTION_DECL(
    "The tail utility writes the last count lines of each file, ten by "
    "default, reading standard input when no file is given. With -c it writes "
    "the last count bytes instead.");

FLAG(TAIL_LINES, String, 'n', "", "Write the last count lines.");
FLAG(TAIL_BYTES, String, 'c', "", "Write the last count bytes.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Tail);

namespace shit {

namespace shitbox {

Tail::Tail() = default;

pure Utility::Kind Tail::kind() const wontthrow { return Kind::Tail; }

fn Tail::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  /* The -c byte count takes precedence over -n when both are given, the way GNU
     tail reads the last of the two. */
  let const is_byte_mode = FLAG_TAIL_BYTES.is_set();
  i64 count = 10;
  if (is_byte_mode) {
    let const parsed = utils::parse_decimal_integer(FLAG_TAIL_BYTES.value());
    if (parsed.is_error() || parsed.value() < 0)
      throw Error{"tail: invalid byte count '" +
                  String{FLAG_TAIL_BYTES.value()} + "'"};

    count = parsed.value();
  } else if (FLAG_TAIL_LINES.is_set()) {
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

  let const should_print_headers = sources.count() > 1;
  let output = String{};
  i32 status = 0;
  for (usize s = 0; s < sources.count(); s++) {
    Maybe<String> content = read_named_or_stdin(ec, sources[s]);
    /* A Ctrl-C during the read returns 130 rather than freezing the utility. */
    if (os::INTERRUPT_REQUESTED) return 130;
    if (!content.has_value()) {
      report_soft_shitbox_error(ec, cxt,
                                "tail: cannot open '" + String{sources[s]} +
                                    "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    if (should_print_headers) {
      if (s > 0) output += '\n';
      output += "==> ";
      output += sources[s];
      output += " <==\n";
    }

    if (is_byte_mode) {
      let const wanted_count = static_cast<usize>(count);
      let const text = content->view();
      let const start =
          text.length > wanted_count ? text.length - wanted_count : 0;
      output += text.substring(start);
      continue;
    }

    let const lines = split_keep_newlines(content->view());
    let const wanted_count = static_cast<usize>(count);
    let const start =
        lines.count() > wanted_count ? lines.count() - wanted_count : 0;
    for (usize i = start; i < lines.count(); i++)
      output += lines[i];
  }

  ec.print_to_stdout(output);
  return status;
}

} // namespace shitbox

} // namespace shit
