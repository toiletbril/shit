#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-t] [-n count] [array]");

HELP_DESCRIPTION_DECL(
    "The mapfile builtin, also named readarray, reads lines from the standard "
    "input into an indexed array, one line per element. The default array "
    "name is MAPFILE when no name operand is given.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* The letters are hand-parsed in execute, so these FLAG rows only feed the
   help text. */
FLAG(MAPFILE_TRIM, Bool, 't', "", "Strip the trailing newline from each line.");
FLAG(MAPFILE_COUNT, String, 'n', "",
     "Read at most count lines, zero for all of them.");

REGISTER_BUILTIN_FLAGS(Mapfile);

namespace shit {

Mapfile::Mapfile() = default;

pure fn Mapfile::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Mapfile;
}

fn Mapfile::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  bool should_strip_newline = false;
  StringView array_name = "MAPFILE";
  /* Zero is bash's unlimited. */
  i64 max_lines = 0;
  i64 skip_count = 0;
  i64 origin = 0;
  let has_origin = false;
  char delimiter = '\n';
  let read_fd = ec.in_fd.value_or(SHIT_STDIN);

  for (usize i = 1; i < args.count(); i++) {
    const StringView arg = args[i].view();
    if (arg == "-t") {
      should_strip_newline = true;
      continue;
    }
    if (arg.is_empty() || arg[0] != '-' || arg == "-") {
      array_name = arg;
      continue;
    }

    let const letter = arg[1];
    let value = StringView{};
    let has_value = false;
    if (arg.length > 2) {
      value = arg.substring(2);
      has_value = true;
    } else if (i + 1 < args.count() &&
               (letter == 'n' || letter == 's' || letter == 'O' ||
                letter == 'd' || letter == 'u'))
    {
      value = args[++i].view();
      has_value = true;
    }

    if (letter == 'd') {
      delimiter = value.is_empty() ? '\0' : value[0];
      continue;
    }

    if (!has_value) continue;

    if (letter == 'u') {
      if (let const fd = value.to<i64>(); !fd.is_error())
        read_fd = os::descriptor_from_fd_number(fd.value());
      continue;
    }

    let const number = value.to<i64>();
    if (number.is_error() || number.value() < 0) continue;
    if (letter == 'n') {
      max_lines = number.value();
    } else if (letter == 's') {
      skip_count = number.value();
    } else if (letter == 'O') {
      origin = number.value();
      has_origin = true;
    }
  }

  LOG(Debug, "mapfile reading lines into array '%.*s'",
      static_cast<int>(array_name.length), array_name.data);

  /* The skipped lines are read and dropped before any line is stored, so -s
     does not count against -n. */
  for (i64 skipped = 0; skipped < skip_count; skipped++) {
    bool was_terminated = false;
    if (!utils::read_line_from_fd(read_fd, was_terminated, delimiter)) break;
  }

  let lines = ArrayList<String>{heap_allocator()};
  loop
  {
    if (max_lines > 0 && static_cast<i64>(lines.count()) >= max_lines) break;

    bool was_newline_terminated = false;
    let const read =
        utils::read_line_from_fd(read_fd, was_newline_terminated, delimiter);
    if (!read) break;

    let element = String{read->view()};
    if (!should_strip_newline && was_newline_terminated)
      element.push(delimiter);
    lines.push(steal(element));
  }

  LOG(Debug, "mapfile stored %zu lines", lines.count());

  if (has_origin) {
    for (usize element_index = 0; element_index < lines.count();
         element_index++)
    {
      char index_text[24];
      const StringView subscript =
          utils::int_to_text_into(origin + static_cast<i64>(element_index),
                                  index_text, sizeof(index_text));
      cxt.assign_array_element(array_name, subscript,
                               lines[element_index].view(), false);
    }
  } else {
    cxt.set_indexed_array(array_name, steal(lines));
  }
  return 0;
}

} // namespace shit
