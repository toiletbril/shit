#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"

/* Reads one line from standard input and splits it on IFS into the named
   variables, the last variable taking the remainder. The flag parser rejects
   an unknown option. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-r] [name ...]");

FLAG(READ_RAW, Bool, 'r', "", "Do not treat a backslash as an escape.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Read::Read() = default;

pure Builtin::Kind Read::kind() const wontthrow { return Kind::Read; }

i32 Read::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  /* -r is accepted, and since backslash processing is not done here the read is
     raw either way. The first returned element is the command name, so the
     operand names begin at index 1. */
  let const names = parse_flags_vec(FLAG_LIST, ec.args());
  defer { reset_flags(FLAG_LIST); };

  ASSERT(!names.is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* With no operand the line goes to REPLY, otherwise to the operands in
     order. The operand names are addressed by an offset into names. */
  let const has_operands = names.count() > 1;
  const usize first_operand = 1;
  let const operand_count = has_operands ? names.count() - first_operand : 1;
  const String reply_name = "REPLY";
  auto operand_name = [&](usize index) -> String {
    if (!has_operands) return reply_name;
    ASSERT(first_operand + index < names.count());
    return names[first_operand + index];
  };

  /* The command's input descriptor honors a redirection or a heredoc on the
     read, falling back to the shell's standard input when none is present. The
     line is copied into a heap String for the splitting below. */
  let was_newline_terminated = false;
  let const read_line = utils::read_line_from_fd(ec.in_fd.value_or(SHIT_STDIN),
                                                 was_newline_terminated);
  if (!read_line) {
    for (usize i = 0; i < operand_count; i++)
      cxt.set_shell_variable(operand_name(i), "");
    return 1;
  }
  let const line = String{StringView{*read_line}};

  let const field_separators =
      cxt.get_variable_value("IFS").value_or(String{heap_allocator(), " \t\n"});
  auto is_separator = [&field_separators](char c) {
    return field_separators.find_character(c).has_value();
  };
  /* POSIX folds an IFS whitespace run into a single delimiter, while each IFS
     non-whitespace character delimits one field on its own, so an empty field
     can sit between two non-whitespace delimiters. */
  auto is_ifs_whitespace = [&is_separator](char c) {
    return (c == ' ' || c == '\t' || c == '\n') && is_separator(c);
  };
  auto is_ifs_nonwhitespace = [&](char c) {
    return is_separator(c) && !is_ifs_whitespace(c);
  };

  usize cursor = 0;
  /* Leading IFS whitespace before the first field is skipped and produces no
     empty field. */
  while (cursor < line.length() && is_ifs_whitespace(line[cursor]))
    cursor++;

  for (usize i = 0; i < operand_count; i++) {
    if (i + 1 == operand_count) {
      /* The last variable receives the rest of the line with trailing IFS
         whitespace trimmed. A trailing non-whitespace IFS delimiter is kept,
         since dash leaves the empty field it introduces inside the remainder.
       */
      String rest = String{line.substring(cursor)};
      while (!rest.is_empty() && is_ifs_whitespace(rest.back()))
        rest.pop_back();
      cxt.set_shell_variable(operand_name(i), rest);
      break;
    }

    let const start = cursor;
    while (cursor < line.length() && !is_separator(line[cursor]))
      cursor++;
    cxt.set_shell_variable(operand_name(i),
                           line.substring_of_length(start, cursor - start));

    /* Consume one delimiter. A run of IFS whitespace folds to one, then an
       optional single non-whitespace IFS character, then trailing whitespace,
       so 'x : y' and 'x:y' both split into the same fields. */
    while (cursor < line.length() && is_ifs_whitespace(line[cursor]))
      cursor++;
    if (cursor < line.length() && is_ifs_nonwhitespace(line[cursor])) {
      cursor++;
      while (cursor < line.length() && is_ifs_whitespace(line[cursor]))
        cursor++;
    }
  }

  /* A final line that end of input ended rather than a newline yields a
     non-zero status, while the variables above are still assigned, the way dash
     reports a short read. */
  return was_newline_terminated ? 0 : 1;
}

} /* namespace shit */
