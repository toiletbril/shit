#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* shopt sets, unsets, and queries the bash shell options such as extglob,
   globstar, nullglob, and dotglob. The states are tracked on the evaluator. An
   option whose pattern engine is not yet wired still records its state so a
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

/* The bash shell option names shopt accepts. An unknown name is an error, the
   way bash rejects one rather than silently treating it as off. */
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
  for (const StringView known : SHOPT_OPTION_NAMES)
    if (known == name) return true;
  return false;
}

/* The bash display line for one option, the name left-justified in a field then
   a tab and the on or off state. */
String shopt_status_line(StringView name, bool on) throws
{
  constexpr usize NAME_FIELD_WIDTH = 20;
  let line = String{name};
  while (line.count() < NAME_FIELD_WIDTH)
    line += ' ';
  line += on ? "\ton\n" : "\toff\n";
  return line;
}

/* The OPTION NAMES help section, every shopt name in columns that fit a
   usual terminal width, the same layout builtin -l prints. */
String format_option_names_help() throws
{
  usize longest = 0;
  for (const StringView name : SHOPT_OPTION_NAMES)
    if (name.length > longest) longest = name.length;
  let const column_width = longest + 2;
  let const columns = column_width >= 78 ? usize{1} : 78 / column_width;

  let section = String{"OPTION NAMES\n"};
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

/* The bash -p display line, a command the shell replays to restore the
   option's state. A completion script captures this through $(shopt -p name)
   and runs it later, so the line must execute, shopt -s or -u for a shopt
   name and set -o or +o for a set option behind the -o bridge. */
String shopt_reusable_line(StringView name, bool on,
                           bool as_set_option) throws
{
  let line = String{};
  if (as_set_option)
    line += on ? "set -o " : "set +o ";
  else
    line += on ? "shopt -s " : "shopt -u ";
  line += name;
  line += '\n';
  return line;
}

} /* namespace */

fn shopt_option_name_list() throws -> ArrayList<StringView>
{
  let names = ArrayList<StringView>{};
  names.reserve(sizeof(SHOPT_OPTION_NAMES) / sizeof(SHOPT_OPTION_NAMES[0]));
  for (const StringView name : SHOPT_OPTION_NAMES)
    names.push(name);
  return names;
}

Shopt::Shopt() = default;

pure Builtin::Kind Shopt::kind() const wontthrow { return Kind::Shopt; }

i32 Shopt::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help")
    SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(ec, format_option_names_help().view());

  bool enable = false;
  bool disable = false;
  bool quiet = false;
  bool operate_on_set_options = false;
  bool print_reusable = false;
  let names = ArrayList<StringView>{heap_allocator()};

  for (usize i = 1; i < args.count(); i++) {
    const StringView arg = args[i].view();
    /* The options combine into one argument, such as the -qo of shopt -qo
       posix, so each letter is read in turn. Any other letter is accepted
       without effect. */
    if (arg.length >= 2 && arg[0] == '-') {
      for (usize k = 1; k < arg.length; k++) {
        if (arg[k] == 's')
          enable = true;
        else if (arg[k] == 'u')
          disable = true;
        else if (arg[k] == 'q')
          quiet = true;
        else if (arg[k] == 'o')
          operate_on_set_options = true;
        else if (arg[k] == 'p')
          print_reusable = true;
      }
    } else {
      names.push(arg);
    }
  }

  /* -p prints in the replayable command form rather than the status form, so a
     query keeps printing while the line changes shape. */
  let const format_status_line = [&](StringView name, bool on) throws {
    return print_reusable
               ? shopt_reusable_line(name, on, operate_on_set_options)
               : shopt_status_line(name, on);
  };

  i32 status = 0;
  auto reject_unknown = [&](StringView name) throws -> bool {
    if (is_known_shopt_option(name)) return false;
    /* A -q probe wants the status without the message, so it stays silent. Any
       other invocation throws a located error the dispatch renders with a caret
       at the command, kept short rather than the long Unable-to-because form.
     */
    if (quiet) {
      status = 1;
      return true;
    }
    throw Error{StringView{"unknown shopt option '"} + name + "'"};
  };

  /* shopt -o operates on the set -o options rather than the shopt names, the
     bridge bash provides so the same options answer either builtin. A config
     probes shopt -qo posix to detect the POSIX mode. */
  if (operate_on_set_options) {
    /* A bare shopt -o or shopt -po lists every set option, the way the named
       query prints one. */
    if (names.is_empty()) {
      if (!quiet)
        for (const StringView name : shell_option_names(false))
          if (Maybe<bool> on = query_shell_option(cxt, name); on.has_value())
            ec.print_to_stdout(format_status_line(name, *on).view());
      return 0;
    }
    for (const StringView name : names) {
      if (enable || disable) {
        if (!apply_shell_option(cxt, name, enable)) {
          if (quiet)
            status = 1;
          else
            throw Error{StringView{"unknown shopt option '"} + name + "'"};
        }
      } else if (Maybe<bool> on = query_shell_option(cxt, name); on.has_value())
      {
        if (!*on) status = 1;
        if (!quiet) ec.print_to_stdout(format_status_line(name, *on).view());
      } else {
        if (quiet)
          status = 1;
        else
          throw Error{StringView{"unknown shopt option '"} + name + "'"};
      }
    }
    return status;
  }

  if (enable || disable) {
    for (const StringView name : names) {
      if (reject_unknown(name)) continue;
      LOG(verbosity::Info, "shopt setting '%.*s' to %s",
          static_cast<int>(name.length), name.data, enable ? "on" : "off");
      cxt.set_shopt_option(name, enable);
    }
    return status;
  }

  /* A query with no names lists every known option through the same read a
     named query uses, so the bash default-on rows show without ever being
     set. A named query prints each option and reports a non-zero status when
     any is off, which the -q form relies on. */
  if (names.is_empty()) {
    if (!quiet) {
      for (const StringView name : SHOPT_OPTION_NAMES)
        ec.print_to_stdout(
            format_status_line(name, cxt.is_shopt_enabled(name)).view());
    }
    return 0;
  }

  for (const StringView name : names) {
    if (reject_unknown(name)) continue;
    const bool on = cxt.is_shopt_enabled(name);
    if (!on) status = 1;
    if (!quiet) ec.print_to_stdout(format_status_line(name, on).view());
  }
  return status;
}

} /* namespace shit */
