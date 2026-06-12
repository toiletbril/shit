#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

/* mapfile, also named readarray, reads every line of standard input into an
   indexed array, one line per element. Without -t each element keeps its
   trailing newline, with -t the newline is stripped. The default array name is
   MAPFILE when no name operand is given. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-t] [-n count] [array]");

HELP_DESCRIPTION_DECL(
    "The mapfile builtin, also named readarray, reads lines from the standard "
    "input into an indexed array, one line per element. The default array "
    "name is MAPFILE when no name operand is given.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* The letters are hand-parsed in execute, so these FLAG rows only feed the
   help text. */
FLAG(MAPFILE_TRIM, Bool, 't', "",
     "Strip the trailing newline from each line.");
FLAG(MAPFILE_COUNT, String, 'n', "",
     "Read at most count lines, zero for all of them.");

REGISTER_BUILTIN_FLAGS(Mapfile);

namespace shit {

Mapfile::Mapfile() = default;

pure Builtin::Kind Mapfile::kind() const wontthrow { return Kind::Mapfile; }

i32 Mapfile::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  bool strip_newline = false;
  StringView array_name = "MAPFILE";
  /* The maximum number of lines to read, where zero is bash's unlimited. */
  i64 max_lines = 0;
  for (usize i = 1; i < args.count(); i++) {
    const StringView arg = args[i].view();
    if (arg == "-t") {
      strip_newline = true;
    } else if (arg == "-n") {
      /* -n takes the count as the next argument. */
      if (i + 1 < args.count()) {
        if (let const count = utils::parse_decimal_integer(args[++i].view());
            !count.is_error() && count.value() >= 0)
          max_lines = count.value();
      }
    } else if (arg.length > 2 && arg[0] == '-' && arg[1] == 'n') {
      /* The attached form -nN carries the count in the same argument. */
      if (let const count = utils::parse_decimal_integer(arg.substring(2));
          !count.is_error() && count.value() >= 0)
        max_lines = count.value();
    } else if (!arg.is_empty() && arg[0] == '-') {
      continue;
    } else {
      array_name = arg;
    }
  }

  LOG(verbosity::Debug, "mapfile reading lines into array '%.*s'",
      static_cast<int>(array_name.length), array_name.data);

  let lines = ArrayList<String>{heap_allocator()};
  for (;;) {
    if (max_lines > 0 && static_cast<i64>(lines.count()) >= max_lines) break;
    bool was_newline_terminated = false;
    let const read = utils::read_line_from_fd(ec.in_fd.value_or(SHIT_STDIN),
                                              was_newline_terminated);
    if (!read) break;
    let element = String{StringView{*read}};
    if (!strip_newline && was_newline_terminated) element += "\n";
    lines.push(steal(element));
  }

  LOG(verbosity::Debug, "mapfile stored %zu lines", lines.count());
  cxt.set_indexed_array(array_name, steal(lines));
  return 0;
}

} /* namespace shit */
