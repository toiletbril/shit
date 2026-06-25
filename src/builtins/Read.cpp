#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-r] [name ...]");
HELP_DESCRIPTION_DECL(
    "The read builtin reads one line from standard input and splits it on IFS "
    "into the named variables, with the last variable taking the remainder. "
    "With no name the line goes to REPLY, and a line ended by end of input "
    "rather than a newline yields a non-zero status.");

FLAG(READ_RAW, Bool, 'r', "", "Do not treat a backslash as an escape.");
FLAG(READ_ARRAY, String, 'a', "",
     "Split the line into the named indexed array.");
FLAG(READ_PROMPT, String, 'p', "",
     "Print the prompt before reading, when reading from a terminal.");
FLAG(READ_TIMEOUT, String, 't', "", "Time out after the given seconds.");
FLAG(READ_NCHARS, String, 'n', "", "Read at most the given number of bytes.");
FLAG(READ_SILENT, Bool, 's', "", "Do not echo the input from a terminal.");
FLAG(READ_DELIM, String, 'd', "",
     "Read until the first byte of the given delimiter instead of a newline, "
     "or until a NUL byte when the delimiter is empty.");
FLAG(READ_FD, String, 'u', "", "Read from the given file descriptor.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Read);

namespace shit {

Read::Read() = default;

pure fn Read::kind() const wontthrow -> Builtin::Kind { return Kind::Read; }

fn Read::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  /* -r is accepted, and since backslash processing is not done here the read is
     raw either way. The first returned element is the command name, so the
     operand names begin at index 1. */
  let const names =
      parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position);
  defer { reset_flags(FLAG_LIST); };

  ASSERT(!names.is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The array, count, timeout, silent, delimiter, and descriptor options are
     bash extensions, so the sh mood rejects them the way dash rejects an
     illegal read option, while -r and -p stay portable. */
  if (cxt.is_posix_mode() &&
      (FLAG_READ_ARRAY.is_set() || FLAG_READ_TIMEOUT.is_set() ||
       FLAG_READ_NCHARS.is_set() || FLAG_READ_SILENT.is_enabled() ||
       FLAG_READ_DELIM.is_set() || FLAG_READ_FD.is_set()))
  {
    report_soft_builtin_error(ec, cxt, "Illegal option");
    return 2;
  }

  /* -u reads from the named descriptor rather than the read's own input, which
     is the command's redirected descriptor or the shell's standard input. */
  let read_fd = ec.in_fd.value_or(SHIT_STDIN);
  if (FLAG_READ_FD.is_set()) {
    if (let const parsed = utils::parse_decimal_integer(FLAG_READ_FD.value());
        !parsed.is_error())
      read_fd = os::descriptor_from_fd_number(parsed.value());
  }

  /* A -p prompt prints to standard error, and only when the read's own input is
     a terminal, the way bash stays quiet for a read whose descriptor is
     redirected from a file or a pipe even at an interactive prompt. */
  if (FLAG_READ_PROMPT.is_set() &&
      os::is_fd_a_tty(ec.in_fd.value_or(SHIT_STDIN)))
  {
    shit::print_error(FLAG_READ_PROMPT.value());
  }

  /* With no operand the line goes to REPLY, otherwise to the operands in
     order. The operand names are addressed by an offset into names. */
  let const has_operands = names.count() > 1;
  LOG(Debug, "read reading into '%s'",
      has_operands ? names[1].c_str() : "REPLY");
  const usize first_operand = 1;
  let const operand_count = has_operands ? names.count() - first_operand : 1;
  const String reply_name = "REPLY";
  let do_operand_name = [&](usize index) -> String {
    if (!has_operands) return reply_name;
    ASSERT(first_operand + index < names.count());
    return names[first_operand + index];
  };

  /* read -d reads until the first byte of its argument rather than a newline,
     and an empty argument reads until a NUL byte, which has no occurrence in
     ordinary text so the whole input is slurped. A bash-completion script reads
     a compgen run this way, with read -d '' and IFS set to a newline, to load
     every candidate into one array. */
  let const delimiter =
      FLAG_READ_DELIM.is_set()
          ? (FLAG_READ_DELIM.value().is_empty() ? '\0'
                                                : FLAG_READ_DELIM.value()[0])
          : '\n';

  /* read -n reads at most the given number of bytes, stopping early on the
     delimiter, so it never consumes more of a pipe than asked. Reaching the
     count is a success the way the delimiter is, while end of input before the
     count yields the short-read status below. With no -n the whole line is
     read.
   */
  i64 max_bytes = 0;
  if (FLAG_READ_NCHARS.is_set()) {
    if (let const parsed =
            utils::parse_decimal_integer(FLAG_READ_NCHARS.value());
        !parsed.is_error())
      max_bytes = parsed.value();
  }

  let was_newline_terminated = false;
  let read_line = Maybe<String>{};
  if (FLAG_READ_NCHARS.is_set()) {
    let buffer = String{};
    i64 bytes_read = 0;
    while (bytes_read < max_bytes) {
      char byte = 0;
      let const got = os::read_fd(read_fd, &byte, 1);
      if (!got.has_value() || *got == 0) break;

      if (byte == delimiter) {
        was_newline_terminated = true;
        break;
      }

      buffer.push(byte);
      bytes_read++;
    }

    if (bytes_read >= max_bytes) was_newline_terminated = true;
    if (bytes_read > 0 || was_newline_terminated) read_line = steal(buffer);
  } else {
    read_line =
        utils::read_line_from_fd(read_fd, was_newline_terminated, delimiter);
  }
  if (!read_line) {
    for (usize i = 0; i < operand_count; i++)
      cxt.set_shell_variable(do_operand_name(i), "");
    return 1;
  }
  let const line = String{StringView{*read_line}};

  let const field_separators = cxt.get_variable_value("IFS").value_or(
      String{cxt.scratch_allocator(), " \t\n"});
  let do_is_separator = [&field_separators](char c) {
    return field_separators.find_character(c).has_value();
  };
  /* POSIX folds an IFS whitespace run into a single delimiter, while each IFS
     non-whitespace character delimits one field on its own, so an empty field
     can sit between two non-whitespace delimiters. */
  let do_is_ifs_whitespace = [&do_is_separator](char c) {
    return (c == ' ' || c == '\t' || c == '\n') && do_is_separator(c);
  };
  let do_is_ifs_nonwhitespace = [&](char c) {
    return do_is_separator(c) && !do_is_ifs_whitespace(c);
  };

  /* read -a NAME splits the line into every field and stores them in the named
     indexed array rather than into separate scalar variables. */
  if (FLAG_READ_ARRAY.is_set()) {
    let words = ArrayList<String>{heap_allocator()};
    usize cursor = 0;
    while (cursor < line.length() && do_is_ifs_whitespace(line[cursor]))
      cursor++;
    while (cursor < line.length()) {
      const usize start = cursor;
      while (cursor < line.length() && !do_is_separator(line[cursor]))
        cursor++;
      words.push(String{line.substring_of_length(start, cursor - start)});
      while (cursor < line.length() && do_is_ifs_whitespace(line[cursor]))
        cursor++;
      if (cursor < line.length() && do_is_ifs_nonwhitespace(line[cursor])) {
        cursor++;
        while (cursor < line.length() && do_is_ifs_whitespace(line[cursor]))
          cursor++;
      }
    }
    cxt.set_indexed_array(FLAG_READ_ARRAY.value(), steal(words));
    return was_newline_terminated ? 0 : 1;
  }

  usize cursor = 0;
  /* Leading IFS whitespace before the first field is skipped and produces no
     empty field. */
  while (cursor < line.length() && do_is_ifs_whitespace(line[cursor]))
    cursor++;

  for (usize i = 0; i < operand_count; i++) {
    if (i + 1 == operand_count) {
      /* The last variable receives the rest of the line with trailing IFS
         whitespace trimmed. A trailing non-whitespace IFS delimiter is kept,
         since dash leaves the empty field it introduces inside the remainder.
       */
      let rest = String{line.substring(cursor)};
      while (!rest.is_empty() && do_is_ifs_whitespace(rest.back()))
        rest.pop_back();
      cxt.set_shell_variable(do_operand_name(i), rest);
      break;
    }

    let const start = cursor;
    while (cursor < line.length() && !do_is_separator(line[cursor]))
      cursor++;
    cxt.set_shell_variable(do_operand_name(i),
                           line.substring_of_length(start, cursor - start));

    /* Consume one delimiter. A run of IFS whitespace folds to one, then an
       optional single non-whitespace IFS character, then trailing whitespace,
       so 'x : y' and 'x:y' both split into the same fields. */
    while (cursor < line.length() && do_is_ifs_whitespace(line[cursor]))
      cursor++;
    if (cursor < line.length() && do_is_ifs_nonwhitespace(line[cursor])) {
      cursor++;
      while (cursor < line.length() && do_is_ifs_whitespace(line[cursor]))
        cursor++;
    }
  }

  /* A final line that end of input ended rather than a newline yields a
     non-zero status, while the variables above are still assigned, the way dash
     reports a short read. */
  return was_newline_terminated ? 0 : 1;
}

} // namespace shit
