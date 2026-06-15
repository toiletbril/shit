#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n count] [file ...]");

HELP_DESCRIPTION_DECL(
    "The head utility writes the first count lines of each file, ten by "
    "default, reading standard input when no file is given. It stops reading "
    "once it has the lines it needs, so it never drains an endless input.");

FLAG(HEAD_LINES, String, 'n', "", "Write the first count lines.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Head);

namespace shit {

namespace shitbox {

/* Read from the descriptor until max_lines newlines have arrived or the input
   ends, copying no further. Stopping early matters for an endless producer such
   as yes, since head reading to the end would never return and would keep the
   producer alive. */
static fn read_up_to_lines(os::descriptor fd, i64 max_lines) throws -> String
{
  String out{};
  i64 lines = 0;
  char buffer[4096];
  while (lines < max_lines) {
    let const read_count = os::read_fd(fd, buffer, sizeof(buffer));
    if (!read_count.has_value() || *read_count == 0) break;
    for (usize i = 0; i < *read_count && lines < max_lines; i++) {
      out.push(buffer[i]);
      if (buffer[i] == '\n') lines++;
    }
  }
  return out;
}

fn util_head(const ExecContext &ec, EvalContext &cxt,
             const ArrayList<String> &args) throws -> i32
{
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  i64 count = 10;
  if (FLAG_HEAD_LINES.is_set()) {
    let const parsed = utils::parse_decimal_integer(FLAG_HEAD_LINES.value());
    if (parsed.is_error() || parsed.value() < 0)
      throw Error{"head: invalid line count '" +
                  String{FLAG_HEAD_LINES.value()} + "'"};
    count = parsed.value();
  }

  ArrayList<StringView> sources{};
  if (operands.is_empty())
    sources.push(StringView{"-"});
  else
    for (const String &operand : operands)
      sources.push(operand.view());

  let const print_headers = sources.count() > 1;
  i32 status = 0;
  for (usize s = 0; s < sources.count(); s++) {
    /* The input descriptor is read directly so a regular file or a pipe is read
       only as far as the requested lines, rather than slurped whole. */
    os::descriptor fd;
    bool opened = false;
    if (sources[s] == "-") {
      fd = ec.in_fd.value_or(SHIT_STDIN);
    } else {
      let const opened_fd =
          os::open_file_descriptor(sources[s], os::file_open_mode::Read);
      if (!opened_fd.has_value()) {
        report_soft_shitbox_error(ec, cxt,
                                  "head: cannot open '" + String{sources[s]} +
                                      "': " + os::last_system_error_message());
        status = 1;
        continue;
      }
      fd = *opened_fd;
      opened = true;
    }

    let const text = read_up_to_lines(fd, count);
    if (opened) os::close_fd(fd);

    let output = String{};
    if (print_headers) {
      if (s > 0) output += '\n';
      output += "==> ";
      output += sources[s];
      output += " <==\n";
    }
    output += text.view();
    ec.print_to_stdout(output);
  }

  return status;
}

} /* namespace shitbox */

} /* namespace shit */
