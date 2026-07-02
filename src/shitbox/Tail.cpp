#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n count] [-c count] [file ...]");

HELP_DESCRIPTION_DECL("The tail utility writes the last lines of each file.");

FLAG(TAIL_LINES, String, 'n', "", "Write the last count lines.");
FLAG(TAIL_BYTES, String, 'c', "", "Write the last count bytes.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Tail);

namespace shit {

namespace shitbox {

Tail::Tail() = default;

pure fn Tail::kind() const wontthrow -> Utility::Kind { return Kind::Tail; }

fn Tail::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  /* -c takes precedence over -n when both are given, matching GNU tail. */
  let const is_byte_mode = FLAG_TAIL_BYTES.is_set();
  i64 count = 10;
  if (is_byte_mode) {
    let const parsed = FLAG_TAIL_BYTES.value().to<i64>();
    if (parsed.is_error() || parsed.value() < 0)
      throw Error{
          "tail: invalid byte count '" +
          String{cxt.scratch_allocator(), FLAG_TAIL_BYTES.value()}
          + "'"
      };

    count = parsed.value();
  } else if (FLAG_TAIL_LINES.is_set()) {
    let const parsed = FLAG_TAIL_LINES.value().to<i64>();
    if (parsed.is_error() || parsed.value() < 0)
      throw Error{
          "tail: invalid line count '" +
          String{cxt.scratch_allocator(), FLAG_TAIL_LINES.value()}
          + "'"
      };

    count = parsed.value();
  }

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  let const should_print_headers = sources.count() > 1;
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;
  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    let const content = read_named_or_stdin(ec, sources[source_index]);
    if (os::INTERRUPT_REQUESTED) return 130;
    if (!content.has_value()) {
      report_soft_shitbox_error(
          ec, cxt,
          "tail: cannot open '" +
              String{cxt.scratch_allocator(), sources[source_index]} +
              "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    if (should_print_headers) {
      if (source_index > 0) output += '\n';
      output += "==> ";
      output += sources[source_index];
      output += " <==\n";
    }

    if (is_byte_mode) {
      let const wanted_count = static_cast<usize>(count);
      let const text = content->view();
      let const start = sub_sat(text.length, wanted_count);
      output += text.substring(start);
      continue;
    }

    let const lines = split_keep_newlines(content->view());
    let const wanted_count = static_cast<usize>(count);
    let const start = sub_sat(lines.count(), wanted_count);
    for (usize i = start; i < lines.count(); i++)
      output += lines[i];
  }

  ec.print_to_stdout(output);
  return status;
}

} // namespace shitbox

} // namespace shit
