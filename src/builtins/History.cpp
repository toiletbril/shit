#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Toiletline.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-c] [-r|-n|-a|-w] [-p arg ...] [count]");
HELP_DESCRIPTION_DECL(
    "The history builtin lists and maintains the interactive command history "
    "kept in the file named by SHIT_HISTORY, or ~/.shit_history by default. A "
    "bare history prints the numbered list, and a trailing count prints only "
    "that many of the most recent entries.");

FLAG(HISTORY_CLEAR, Bool, 'c', "", "Clear the history list.");
FLAG(HISTORY_DELETE, Bool, 'd', "",
     "Accept a delete offset operand, leaving the list unchanged.");
FLAG(HISTORY_APPEND, Bool, 'a', "", "Write the history list to the file.");
FLAG(HISTORY_READ_NEW, Bool, 'n', "", "Read the history file into the list.");
FLAG(HISTORY_READ, Bool, 'r', "", "Read the history file into the list.");
FLAG(HISTORY_WRITE, Bool, 'w', "", "Write the history list to the file.");
FLAG(HISTORY_PRINT, Bool, 'p', "", "Print the operands, storing nothing.");
FLAG(HISTORY_STORE, Bool, 's', "",
     "Accept operands to store, leaving the list unchanged.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(History);

namespace shit {

History::History() = default;

pure fn History::kind() const wontthrow -> Builtin::Kind
{
  return Kind::History;
}

static fn print_history_list(const ExecContext &ec, EvalContext &cxt,
                             usize wanted_count) throws -> void
{
  let const path = toiletline::history_path();
  if (!path.has_value()) return;

  let const contents = path->read_entire_file();
  if (!contents.has_value()) return;

  let const text = contents->view();
  let lines = ArrayList<StringView>{cxt.scratch_allocator()};
  usize line_start = 0;
  for (usize i = 0; i < text.length; i++)
    if (text[i] == '\n') {
      lines.push(text.substring_of_length(line_start, i - line_start));
      line_start = i + 1;
    }

  if (line_start < text.length) lines.push(text.substring(line_start));

  usize first_index = 0;
  if (wanted_count != 0 && wanted_count < lines.count())
    first_index = lines.count() - wanted_count;

  let out = String{cxt.scratch_allocator()};
  for (usize i = first_index; i < lines.count(); i++) {
    let const number = String::from(i + 1, cxt.scratch_allocator());
    for (usize pad = number.count(); pad < 5; pad++)
      out += ' ';
    out.append(number.view());
    out += "  ";
    out.append(lines[i]);
    out += '\n';
  }
  ec.print_to_stdout(out);
}

fn History::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let did_maintain_list = false;

  if (FLAG_HISTORY_CLEAR.is_enabled()) {
    LOG(Debug, "history clearing the list");
    toiletline::history_clear();
    did_maintain_list = true;
  }

  if (FLAG_HISTORY_READ.is_enabled() || FLAG_HISTORY_READ_NEW.is_enabled()) {
    LOG(Debug, "history reading the file into the list");
    toiletline::history_read();
    did_maintain_list = true;
  }

  if (FLAG_HISTORY_APPEND.is_enabled() || FLAG_HISTORY_WRITE.is_enabled()) {
    LOG(Debug, "history writing the list to the file");
    toiletline::history_write();
    did_maintain_list = true;
  }

  if (FLAG_HISTORY_PRINT.is_enabled()) {
    let out = String{cxt.scratch_allocator()};
    for (usize i = 1; i < args.count(); i++) {
      out.append(args[i].view());
      out += '\n';
    }
    ec.print_to_stdout(out);
    did_maintain_list = true;
  }

  if (FLAG_HISTORY_DELETE.is_enabled() || FLAG_HISTORY_STORE.is_enabled())
    did_maintain_list = true;

  if (did_maintain_list) return 0;

  usize wanted_count = 0;
  if (args.count() > 1 && args[1].view().is_all_decimal_digits()) {
    let const digits = args[1].view();
    for (usize i = 0; i < digits.length; i++)
      wanted_count = (wanted_count * 10) + static_cast<usize>(digits[i] - '0');
  }

  print_history_list(ec, cxt, wanted_count);
  return 0;
}

} // namespace shit
