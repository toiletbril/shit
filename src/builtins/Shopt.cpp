#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* shopt sets, unsets, and queries the bash shell options such as extglob,
   globstar, nullglob, and dotglob. The states are tracked on the evaluator. An
   option whose pattern engine is not yet wired still records its state so a
   later query reads it back. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-suqo] [optname ...]");

HELP_DESCRIPTION_DECL(
    "The shopt builtin sets, unsets, and queries the bash shell options such "
    "as "
    "extglob, globstar, nullglob, and dotglob. The -s flag enables an option, "
    "-u disables it, -q suppresses the status output for a scripted probe, and "
    "-o operates on the set -o options instead of the shopt names. With no "
    "flag "
    "a named option is queried, and with no name every option that has a "
    "recorded state is listed.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

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

} /* namespace */

Shopt::Shopt() = default;

pure Builtin::Kind Shopt::kind() const wontthrow { return Kind::Shopt; }

i32 Shopt::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  bool enable = false;
  bool disable = false;
  bool quiet = false;
  bool operate_on_set_options = false;
  let names = ArrayList<StringView>{heap_allocator()};

  for (usize i = 1; i < args.count(); i++) {
    const StringView arg = args[i].view();
    /* The options combine into one argument, such as the -qo of shopt -qo
       posix, so each letter is read in turn. A -p or any other letter is
       accepted without effect. */
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
      }
    } else {
      names.push(arg);
    }
  }

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
        if (!quiet) ec.print_to_stdout(shopt_status_line(name, *on).view());
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
            shopt_status_line(name, cxt.is_shopt_enabled(name)).view());
    }
    return 0;
  }

  for (const StringView name : names) {
    if (reject_unknown(name)) continue;
    const bool on = cxt.is_shopt_enabled(name);
    if (!on) status = 1;
    if (!quiet) ec.print_to_stdout(shopt_status_line(name, on).view());
  }
  return status;
}

} /* namespace shit */
