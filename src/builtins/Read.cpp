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
  /* The first returned element is the command name, so the operand names begin
     at index 1. */
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
  /* Without -r a backslash escapes the next byte, so a backslash before the
     line delimiter joins the next line and a backslash before any other byte
     makes it a literal that no longer splits on IFS. The -r and -n forms keep
     every byte. */
  let accumulated = String{StringView{*read_line}};
  let const should_process_escapes =
      !FLAG_READ_RAW.is_enabled() && !FLAG_READ_NCHARS.is_set();
  if (should_process_escapes) {
    let do_trailing_backslash_count = [&accumulated]() -> usize {
      usize backslash_count = 0;
      while (backslash_count < accumulated.length() &&
             accumulated[accumulated.length() - 1 - backslash_count] == '\\')
        backslash_count++;
      return backslash_count;
    };
    /* An odd run of trailing backslashes leaves one escaping the delimiter, so
       the delimiter is dropped and the next line is read and appended. */
    while (was_newline_terminated && do_trailing_backslash_count() % 2 == 1) {
      accumulated.pop_back();
      let const continued =
          utils::read_line_from_fd(read_fd, was_newline_terminated, delimiter);
      if (!continued.has_value()) break;
      accumulated.append(continued->view());
    }
  }

  /* The de-escaped bytes pair with a mask marking which one came from a
     backslash escape, so the splitter below keeps an escaped IFS byte inside
     its field. The mask is all false in the raw forms. */
  let line = String{};
  let is_literal_byte = ArrayList<bool>{};
  line.reserve(accumulated.length());
  is_literal_byte.reserve(accumulated.length());
  for (usize i = 0; i < accumulated.length(); i++) {
    if (should_process_escapes && accumulated[i] == '\\') {
      if (i + 1 >= accumulated.length()) break;
      line.push(accumulated[i + 1]);
      is_literal_byte.push(true);
      i++;
    } else {
      line.push(accumulated[i]);
      is_literal_byte.push(false);
    }
  }

  let const field_separators = cxt.get_variable_value("IFS").value_or(
      String{cxt.scratch_allocator(), " \t\n"});
  let do_is_separator = [&](usize i) {
    return !is_literal_byte[i] &&
           field_separators.find_character(line[i]).has_value();
  };
  /* POSIX folds an IFS whitespace run into a single delimiter, while each IFS
     non-whitespace character delimits one field on its own, so an empty field
     can sit between two non-whitespace delimiters. */
  let do_is_ifs_whitespace = [&](usize i) {
    return !is_literal_byte[i] &&
           (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') &&
           field_separators.find_character(line[i]).has_value();
  };
  let do_is_ifs_nonwhitespace = [&](usize i) {
    return do_is_separator(i) && !do_is_ifs_whitespace(i);
  };

  /* read -a NAME splits the line into every field and stores them in the named
     indexed array rather than into separate scalar variables. */
  if (FLAG_READ_ARRAY.is_set()) {
    let words = ArrayList<String>{heap_allocator()};
    usize cursor = 0;
    while (cursor < line.length() && do_is_ifs_whitespace(cursor))
      cursor++;
    while (cursor < line.length()) {
      const usize start = cursor;
      while (cursor < line.length() && !do_is_separator(cursor))
        cursor++;
      words.push(String{line.substring_of_length(start, cursor - start)});
      while (cursor < line.length() && do_is_ifs_whitespace(cursor))
        cursor++;
      if (cursor < line.length() && do_is_ifs_nonwhitespace(cursor)) {
        cursor++;
        while (cursor < line.length() && do_is_ifs_whitespace(cursor))
          cursor++;
      }
    }
    cxt.set_indexed_array(FLAG_READ_ARRAY.value(), steal(words));
    return was_newline_terminated ? 0 : 1;
  }

  usize cursor = 0;
  /* Leading IFS whitespace before the first field is skipped and produces no
     empty field. */
  while (cursor < line.length() && do_is_ifs_whitespace(cursor))
    cursor++;

  for (usize i = 0; i < operand_count; i++) {
    if (i + 1 == operand_count) {
      /* The last variable receives the rest of the line with trailing IFS
         whitespace trimmed. A trailing non-whitespace IFS delimiter is kept,
         since dash leaves the empty field it introduces inside the remainder.
       */
      usize end_position = line.length();
      while (end_position > cursor && do_is_ifs_whitespace(end_position - 1))
        end_position--;
      cxt.set_shell_variable(
          do_operand_name(i),
          line.substring_of_length(cursor, end_position - cursor));
      break;
    }

    let const start = cursor;
    while (cursor < line.length() && !do_is_separator(cursor))
      cursor++;
    cxt.set_shell_variable(do_operand_name(i),
                           line.substring_of_length(start, cursor - start));

    /* Consume one delimiter. A run of IFS whitespace folds to one, then an
       optional single non-whitespace IFS character, then trailing whitespace,
       so 'x : y' and 'x:y' both split into the same fields. */
    while (cursor < line.length() && do_is_ifs_whitespace(cursor))
      cursor++;
    if (cursor < line.length() && do_is_ifs_nonwhitespace(cursor)) {
      cursor++;
      while (cursor < line.length() && do_is_ifs_whitespace(cursor))
        cursor++;
    }
  }

  /* A final line that end of input ended rather than a newline yields a
     non-zero status, while the variables above are still assigned, the way dash
     reports a short read. */
  return was_newline_terminated ? 0 : 1;
}

} // namespace shit
