#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* An option whose pattern engine is not yet wired still records its state so a
   later query reads it back. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-supqo] [optname ...]");

HELP_DESCRIPTION_DECL(
    "The shopt builtin sets, unsets, and queries the bash shell options such "
    "as extglob, globstar, nullglob, and dotglob. With no flag a named "
    "option is queried, and with no name every option is listed with its "
    "state. The OPTION NAMES section below lists every name shopt accepts.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* The letters are hand-parsed in execute since they combine bash-style, so
   these FLAG rows only feed the help text. */
FLAG(SHOPT_SET, Bool, 's', "", "Enable each named option.");
FLAG(SHOPT_UNSET, Bool, 'u', "", "Disable each named option.");
FLAG(SHOPT_QUIET, Bool, 'q', "",
     "Suppress the status output, the scripted probe form.");
FLAG(SHOPT_PRINT, Bool, 'p', "",
     "Print in the replayable form, shopt -s or -u per line, and set -o or "
     "+o behind -o.");
FLAG(SHOPT_SET_OPTIONS, Bool, 'o', "",
     "Operate on the set -o option names instead of the shopt names.");

REGISTER_BUILTIN_FLAGS(Shopt);

namespace shit {

namespace {

const StringView SHOPT_OPTION_NAMES[] = {
    "autocd",
    "assoc_expand_once",
    "cdable_vars",
    "cdspell",
    "checkhash",
    "checkjobs",
    "checkwinsize",
    "cmdhist",
    "complete_fullquote",
    "direxpand",
    "dirspell",
    "dotglob",
    "execfail",
    "expand_aliases",
    "extdebug",
    "extglob",
    "extquote",
    "failglob",
    "force_fignore",
    "globasciiranges",
    "globskipdots",
    "globstar",
    "gnu_errfmt",
    "histappend",
    "histreedit",
    "histverify",
    "hostcomplete",
    "huponexit",
    "inherit_errexit",
    "interactive_comments",
    "lastpipe",
    "lithist",
    "localvar_inherit",
    "localvar_unset",
    "login_shell",
    "mailwarn",
    "no_empty_cmd_completion",
    "nocaseglob",
    "nocasematch",
    "nullglob",
    "progcomp",
    "progcomp_alias",
    "promptvars",
    "restricted_shell",
    "shift_verbose",
    "sourcepath",
    "varredir_close",
    "xpg_echo",
};

bool is_known_shopt_option(StringView name) throws
{
  for (let const &known : SHOPT_OPTION_NAMES)
    if (known == name) return true;
  return false;
}

String shopt_status_line(StringView name, bool on, Allocator allocator) throws
{
  constexpr usize NAME_FIELD_WIDTH = 20;
  let line = String{allocator, name};
  while (line.count() < NAME_FIELD_WIDTH)
    line += ' ';
  line += on ? "\ton\n" : "\toff\n";
  return line;
}

String format_option_names_help(Allocator allocator) throws
{
  usize longest = 0;
  for (let const &name : SHOPT_OPTION_NAMES)
    if (name.length > longest) longest = name.length;
  let const column_width = longest + 2;
  let const columns = column_width >= 78 ? usize{1} : 78 / column_width;

  let section = String{allocator, "OPTION NAMES\n"};
  let const total = sizeof(SHOPT_OPTION_NAMES) / sizeof(SHOPT_OPTION_NAMES[0]);
  for (usize i = 0; i < total; i++) {
    if (i % columns == 0) section += "  ";
    section += SHOPT_OPTION_NAMES[i];
    let const last_in_row = i % columns == columns - 1 || i + 1 == total;
    if (last_in_row) {
      section += "\n";
    } else {
      for (usize pad = SHOPT_OPTION_NAMES[i].length; pad < column_width; pad++)
        section += " ";
    }
  }
  return section;
}

/* The bash -p line is a command the shell replays to restore the state, so it
   must execute when a completion script captures it through $(shopt -p name).
 */
String shopt_reusable_line(StringView name, bool on, bool as_set_option,
                           Allocator allocator) throws
{
  let line = String{allocator};
  if (as_set_option)
    line += on ? "set -o " : "set +o ";
  else
    line += on ? "shopt -s " : "shopt -u ";
  line += name;
  line += '\n';
  return line;
}

} // namespace

fn shopt_option_name_list() throws -> const ArrayList<StringView> &
{
  static ArrayList<StringView> names = [] throws {
    let collected = ArrayList<StringView>{heap_allocator()};
    collected.reserve(sizeof(SHOPT_OPTION_NAMES) /
                      sizeof(SHOPT_OPTION_NAMES[0]));
    for (let const &name : SHOPT_OPTION_NAMES)
      collected.push(name);
    return collected;
  }();
  return names;
}

Shopt::Shopt() = default;

pure fn Shopt::kind() const wontthrow -> Builtin::Kind { return Kind::Shopt; }

fn Shopt::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help")
    SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(
        ec, format_option_names_help(cxt.scratch_allocator()).view());

  bool should_enable = false;
  bool should_disable = false;
  bool is_quiet = false;
  bool should_operate_on_set_options = false;
  bool should_print_reusable = false;
  let names = ArrayList<StringView>{cxt.scratch_allocator()};

  for (usize i = 1; i < args.count(); i++) {
    const StringView arg = args[i].view();
    /* The options combine into one argument, such as the -qo of shopt -qo
       posix. Any other letter is accepted without effect. */
    if (arg.length >= 2 && arg[0] == '-') {
      for (usize k = 1; k < arg.length; k++) {
        if (arg[k] == 's')
          should_enable = true;
        else if (arg[k] == 'u')
          should_disable = true;
        else if (arg[k] == 'q')
          is_quiet = true;
        else if (arg[k] == 'o')
          should_operate_on_set_options = true;
        else if (arg[k] == 'p')
          should_print_reusable = true;
      }
    } else {
      names.push(arg);
    }
  }

  let const do_format_status_line = [&](StringView name, bool on) throws {
    return should_print_reusable
               ? shopt_reusable_line(name, on, should_operate_on_set_options,
                                     cxt.scratch_allocator())
               : shopt_status_line(name, on, cxt.scratch_allocator());
  };

  i32 status = 0;
  let do_reject_unknown = [&](StringView name) throws -> bool {
    if (is_known_shopt_option(name)) return false;
    /* A -q probe stays silent and reports only through the status. */
    if (is_quiet) {
      status = 1;
      return true;
    }
    throw Error{StringView{"Unknown shopt option '"} + name + "'"};
  };

  /* shopt -o operates on the set -o options, the bridge bash provides so the
     same options answer either builtin. A config probes shopt -qo posix. */
  if (should_operate_on_set_options) {
    if (names.is_empty()) {
      if (!is_quiet)
        for (let const &name : shell_option_names(false))
          if (Maybe<bool> on = query_shell_option(cxt, name); on.has_value())
            ec.print_to_stdout(do_format_status_line(name, *on).view());
      return 0;
    }
    for (let const &name : names) {
      if (should_enable || should_disable) {
        if (!apply_shell_option(cxt, name, should_enable)) {
          if (is_quiet)
            status = 1;
          else
            throw Error{StringView{"Unknown shopt option '"} + name + "'"};
        }
      } else if (Maybe<bool> on = query_shell_option(cxt, name); on.has_value())
      {
        if (!*on) status = 1;
        if (!is_quiet)
          ec.print_to_stdout(do_format_status_line(name, *on).view());
      } else {
        if (is_quiet)
          status = 1;
        else
          throw Error{StringView{"Unknown shopt option '"} + name + "'"};
      }
    }
    return status;
  }

  if (should_enable || should_disable) {
    for (let const &name : names) {
      if (do_reject_unknown(name)) continue;
      LOG(Info, "shopt setting '%.*s' to %s", static_cast<int>(name.length),
          name.data, should_enable ? "on" : "off");
      cxt.set_shopt_option(name, should_enable);
    }
    return status;
  }

  /* A named query reports a non-zero status when any option is off, which the
     -q form relies on. */
  if (names.is_empty()) {
    if (!is_quiet) {
      for (let const &name : SHOPT_OPTION_NAMES)
        ec.print_to_stdout(
            do_format_status_line(name, cxt.is_shopt_enabled(name)).view());
    }
    return 0;
  }

  for (let const &name : names) {
    if (do_reject_unknown(name)) continue;
    const bool on = cxt.is_shopt_enabled(name);
    if (!on) status = 1;
    if (!is_quiet) ec.print_to_stdout(do_format_status_line(name, on).view());
  }
  return status;
}

} // namespace shit
