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
    "With no name the line goes to REPLY, and a line that ends at end of input "
    "without a newline yields a non-zero status.");

FLAG(READ_RAW, Bool, 'r', "", "Do not treat a backslash as an escape.");
FLAG(READ_ARRAY, String, 'a', "",
     "Split the line into the named indexed array.");
FLAG(READ_PROMPT, String, 'p', "",
     "Print the prompt before reading, when reading from a terminal.");
FLAG(READ_TIMEOUT, String, 't', "", "Time out after the given seconds.");
FLAG(READ_NCHARS, String, 'n', "", "Read at most the given number of bytes.");
FLAG(READ_SILENT, Bool, 's', "", "Do not echo the input from a terminal.");
FLAG(READ_DELIM, String, 'd', "",
     "Read until the first byte of the given delimiter, or until a NUL byte "
     "when the delimiter is empty.");
FLAG(READ_FD, String, 'u', "", "Read from the given file descriptor.");
FLAG(READ_EDIT, Bool, 'e', "",
     "Accepted for compatibility. The line editor is always used at a "
     "terminal.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Read);

namespace shit {

Read::Read() = default;

pure fn Read::kind() const wontthrow -> Builtin::Kind { return Kind::Read; }

fn Read::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const names = PARSE_BUILTIN_ARGS(ec);

  ASSERT(!names.is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* The array, count, timeout, silent, delimiter, descriptor, and editor
     options are bash extensions the sh mood rejects. */
  if (cxt.is_posix_mode() &&
      (FLAG_READ_ARRAY.is_set() || FLAG_READ_TIMEOUT.is_set() ||
       FLAG_READ_NCHARS.is_set() || FLAG_READ_SILENT.is_enabled() ||
       FLAG_READ_DELIM.is_set() || FLAG_READ_FD.is_set() ||
       FLAG_READ_EDIT.is_enabled()))
  {
    report_soft_builtin_error(ec, cxt, "Illegal option");
    return 2;
  }

  let read_fd = ec.in_fd.value_or(SHIT_STDIN);
  if (FLAG_READ_FD.is_set()) {
    if (let const parsed = FLAG_READ_FD.value().to<i64>(); !parsed.is_error())
      read_fd = os::descriptor_from_fd_number(parsed.value());
  }

  i64 timeout_nanos = -1;
  if (FLAG_READ_TIMEOUT.is_set()) {
    let const parsed =
        utils::parse_timeout_seconds_to_nanos(FLAG_READ_TIMEOUT.value());
    if (parsed.is_error()) {
      report_soft_builtin_error(ec, cxt, "Invalid timeout specification");
      return 1;
    }
    timeout_nanos = parsed.value();
  }

  if (timeout_nanos == 0)
    return os::wait_for_fd_readable(read_fd, 0) == 1 ? 0 : 1;

  /* A -p prompt prints only when the read's own input is a terminal, matching
     bash for a redirected descriptor. */
  if (FLAG_READ_PROMPT.is_set() &&
      os::is_fd_a_tty(ec.in_fd.value_or(SHIT_STDIN)))
  {
    shit::print_error(FLAG_READ_PROMPT.value());
  }

  let const has_operands = names.count() > 1;
  LOG(Debug, "read reading into '%s'",
      has_operands ? names[1].c_str() : "REPLY");
  const usize first_operand = 1;
  let const operand_count = has_operands ? names.count() - first_operand : 1;
  const String reply_name = "REPLY";
  let do_operand_name = [&](usize index) -> const String & {
    if (!has_operands) return reply_name;
    ASSERT(first_operand + index < names.count());
    return names[first_operand + index];
  };

  /* An empty -d argument reads until a NUL byte, so the whole input is slurped.
   */
  let const delimiter =
      FLAG_READ_DELIM.is_set()
          ? (FLAG_READ_DELIM.value().is_empty() ? '\0'
                                                : FLAG_READ_DELIM.value()[0])
          : '\n';

  /* Reaching the -n count is a success the way the delimiter is, while end of
     input before the count yields the short-read status below. */
  i64 max_bytes = 0;
  if (FLAG_READ_NCHARS.is_set()) {
    if (let const parsed = FLAG_READ_NCHARS.value().to<i64>();
        !parsed.is_error())
      max_bytes = parsed.value();
  }

  let was_newline_terminated = false;
  let was_timed_out = false;
  let read_line = Maybe<String>{};
  if (FLAG_READ_NCHARS.is_set()) {
    let buffer = String{cxt.scratch_allocator()};
    const u64 deadline_nanos =
        timeout_nanos > 0
            ? os::monotonic_nanos() + static_cast<u64>(timeout_nanos)
            : 0;
    i64 bytes_read = 0;
    while (bytes_read < max_bytes) {
      if (timeout_nanos > 0) {
        i64 remaining_nanos =
            static_cast<i64>(deadline_nanos - os::monotonic_nanos());
        if (remaining_nanos < 0) remaining_nanos = 0;
        let const readable = os::wait_for_fd_readable(read_fd, remaining_nanos);
        if (readable != 1) {
          if (readable == 0) was_timed_out = true;
          break;
        }
      }

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
        utils::read_line_from_fd(read_fd, was_newline_terminated, delimiter,
                                 timeout_nanos, &was_timed_out);
  }
  if (!read_line) {
    for (usize i = 0; i < operand_count; i++)
      cxt.set_shell_variable(do_operand_name(i), "");
    return was_timed_out ? 142 : 1;
  }
  /* Without -r a backslash escapes the next byte, joining the next line at the
     delimiter and making any other byte a literal that no longer splits on IFS.
   */
  let accumulated = String{cxt.scratch_allocator(), read_line->view()};
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
  let line = String{cxt.scratch_allocator()};
  let is_literal_byte = ArrayList<bool>{cxt.scratch_allocator()};
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

    while (cursor < line.length() && do_is_ifs_whitespace(cursor))
      cursor++;
    if (cursor < line.length() && do_is_ifs_nonwhitespace(cursor)) {
      cursor++;
      while (cursor < line.length() && do_is_ifs_whitespace(cursor))
        cursor++;
    }
  }

  return was_newline_terminated ? 0 : 1;
}

} // namespace shit
