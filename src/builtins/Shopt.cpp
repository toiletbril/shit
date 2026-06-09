#include "../Builtin.hpp"
#include "../Eval.hpp"

/* shopt sets, unsets, and queries the bash shell options such as extglob,
   globstar, nullglob, and dotglob. The states are tracked on the evaluator. An
   option whose pattern engine is not yet wired still records its state so a
   later query reads it back. */

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

  bool enable = false;
  bool disable = false;
  bool quiet = false;
  let names = ArrayList<StringView>{heap_allocator()};

  for (usize i = 1; i < args.count(); i++) {
    const StringView arg = args[i].view();
    if (arg == "-s") {
      enable = true;
    } else if (arg == "-u") {
      disable = true;
    } else if (arg == "-q") {
      quiet = true;
    } else if (!arg.is_empty() && arg[0] == '-') {
      /* Other flags such as -p or -o are accepted without effect. */
      continue;
    } else {
      names.push(arg);
    }
  }

  i32 status = 0;
  auto reject_unknown = [&](StringView name) throws -> bool {
    if (is_known_shopt_option(name)) return false;
    shit::print_error(StringView{"shopt: "} + name +
                      ": invalid shell option name\n");
    status = 1;
    return true;
  };

  if (enable || disable) {
    for (const StringView name : names) {
      if (reject_unknown(name)) continue;
      cxt.set_shopt_option(name, enable);
    }
    return status;
  }

  /* A query with no names lists every option that has a recorded state. A named
     query prints each option and reports a non-zero status when any is off,
     which the -q form relies on. */
  if (names.is_empty()) {
    if (!quiet) {
      cxt.shopt_options().for_each([&](StringView name, const bool &on) {
        ec.print_to_stdout(shopt_status_line(name, on).view());
      });
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
