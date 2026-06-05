#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* Reads one line from standard input and splits it on IFS into the named
   variables, the last variable taking the remainder. The flag parser rejects
   an unknown option. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("read [-r] [name ...]");

FLAG(READ_RAW, Bool, 'r', "", "Do not treat a backslash as an escape.");

namespace shit {

Read::Read() = default;

Builtin::Kind Read::kind() const { return Kind::Read; }

i32 Read::execute(ExecContext &ec, EvalContext &cxt) const
{
  /* -r is accepted, and since backslash processing is not done here the read is
     raw either way. The first returned element is the command name, so the
     operand names begin at index 1. */
  ArrayList<String> names = parse_flags_vec(FLAG_LIST, ec.args());
  SHIT_DEFER { reset_flags(FLAG_LIST); };

  /* With no operand the line goes to REPLY, otherwise to the operands in
     order. The operand names are addressed by an offset into names. */
  bool has_operands = names.size() > 1;
  usize first_operand = 1;
  usize operand_count = has_operands ? names.size() - first_operand : 1;
  String reply_name = "REPLY";
  auto operand_name = [&](usize index) -> String {
    if (!has_operands) return reply_name;
    return names[first_operand + index];
  };

  /* The command's input descriptor honors a redirection or a heredoc on the
     read, falling back to the shell's standard input when none is present. The
     line is copied into a heap String for the splitting below. */
  Maybe<String> read_line =
      utils::read_line_from_fd(ec.in_fd.value_or(SHIT_STDIN));
  if (!read_line) {
    for (usize i = 0; i < operand_count; i++)
      cxt.set_shell_variable(operand_name(i), "");
    return 1;
  }
  String line = String{StringView{*read_line}};

  String field_separators =
      cxt.get_variable_value("IFS").value_or(String{heap_allocator(), " \t\n"});
  auto is_separator = [&field_separators](char c) {
    return field_separators.find_character(c).has_value();
  };

  usize cursor = 0;
  for (usize i = 0; i < operand_count; i++) {
    while (cursor < line.length() && is_separator(line[cursor]))
      cursor++;

    if (i + 1 == operand_count) {
      /* The last variable receives the rest of the line with trailing
         separators trimmed. */
      String rest = String{line.substring(cursor)};
      while (!rest.empty() && is_separator(rest.back()))
        rest.pop_back();
      cxt.set_shell_variable(operand_name(i), rest);
    } else {
      usize start = cursor;
      while (cursor < line.length() && !is_separator(line[cursor]))
        cursor++;
      cxt.set_shell_variable(operand_name(i),
                             line.substring_of_length(start, cursor - start));
    }
  }

  return 0;
}

} /* namespace shit */
