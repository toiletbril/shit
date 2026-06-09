#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

/* mapfile, also named readarray, reads every line of standard input into an
   indexed array, one line per element. Without -t each element keeps its
   trailing newline, with -t the newline is stripped. The default array name is
   MAPFILE when no name operand is given. */

namespace shit {

Mapfile::Mapfile() = default;

pure Builtin::Kind Mapfile::kind() const wontthrow { return Kind::Mapfile; }

i32 Mapfile::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  bool strip_newline = false;
  StringView array_name = "MAPFILE";
  for (usize i = 1; i < args.count(); i++) {
    const StringView arg = args[i].view();
    if (arg == "-t")
      strip_newline = true;
    else if (!arg.is_empty() && arg[0] == '-')
      continue;
    else
      array_name = arg;
  }

  ArrayList<String> lines{heap_allocator()};
  for (;;) {
    bool was_newline_terminated = false;
    let const read = utils::read_line_from_fd(ec.in_fd.value_or(SHIT_STDIN),
                                              was_newline_terminated);
    if (!read) break;
    String element{StringView{*read}};
    if (!strip_newline && was_newline_terminated) element += "\n";
    lines.push(steal(element));
  }

  cxt.set_indexed_array(array_name, steal(lines));
  return 0;
}

} /* namespace shit */
