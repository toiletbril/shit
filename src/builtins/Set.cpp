#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* set toggles the shell options and rebinds the positional parameters. An
   option is named by a single letter after a minus to enable it or a plus to
   disable it, or by its long name after -o or +o. The letters and names that
   have no backing behavior yet are accepted without effect. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abCefhmnuvx] [+abCefhmnuvx] [-o name] [+o name] "
                   "[--] [arg ...]");

HELP_DESCRIPTION_DECL(
    "The set builtin enables a shell option named by a single letter after a "
    "minus or by its long name after -o, and disables it with a plus, and "
    "rebinds the positional parameters to its operands after a double dash or "
    "the first non-option word. With no argument it lists the shell "
    "variables.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace {

/* One shell option, named by its letter and its long name, with the command
   line's --help spelling as an accepted alias where the two differ. set and
   get are the accessors on EvalContext. Both null marks an option accepted
   without effect, while a null set with a live get marks a startup fact that
   set -o reports read-only and refuses to change. */
class SetOption
{
public:
  char letter;
  StringView name;
  void (EvalContext::*set)(bool);
  bool (EvalContext::*get)() const;
  StringView help;
  /* The default keeps the rows without a second spelling free of a trailing
     empty initializer. */
  StringView alias{};
};

const SetOption SET_OPTIONS[] = {
    {'e',  "errexit",          &EvalContext::set_error_exit,       &EvalContext::error_exit,
     "Exit on the first command that fails.",                       "error-exit"             },
    {'x',  "xtrace",           &EvalContext::set_echo_expanded,
     &EvalContext::should_echo_expanded,
     "Print each command after expansion before it runs."                                                                                },
    {'u',  "nounset",          &EvalContext::set_error_unset,      &EvalContext::error_unset,
     "Treat an unset variable as an error.",                        "no-unset"               },
    {'\0', "pipefail",         &EvalContext::set_pipefail,         &EvalContext::pipefail,
     "Report a pipeline's status as the rightmost stage that failed."                                                                    },
    {'\0', "posix",            &EvalContext::set_posix_mode,       &EvalContext::is_posix_mode,
     "Behave like dash, the POSIX mode."                                                                                                 },
    {'a',  "allexport",        &EvalContext::set_export_all,       &EvalContext::export_all,
     "Mark every assigned variable for the environment.",          "export-all"              },
    {'C',  "noclobber",        &EvalContext::set_no_clobber,       &EvalContext::no_clobber,
     "Refuse to overwrite an existing file through '>'.",          "no-clobber"              },
    {'f',  "noglob",           &EvalContext::set_no_glob,          &EvalContext::no_glob,
     "Disable pathname expansion.",                                "no-glob"                 },
    {'n',  "noexec",           &EvalContext::set_no_exec,          &EvalContext::no_exec,
     "Read and parse commands but do not run them.",               "no-exec"                 },
    {'m',  "monitor",          &EvalContext::set_monitor,          &EvalContext::monitor,
     "Run background jobs in their own process group with notifications."                                                                },
    /* failglob has no short letter, so '\0' keeps find_option_by_letter from
       ever matching a parsed option character. */
    {'\0', "failglob",         &EvalContext::set_failglob,         &EvalContext::failglob,
     "Fail a command whose glob matches nothing."                                                                                        },
    {'b',  "notify",           nullptr,                            nullptr,                     "Accepted without effect."               },
    {'h',  "hashall",          nullptr,                            nullptr,                     "Accepted without effect."               },
    {'v',  "verbose",          &EvalContext::set_echo,             &EvalContext::should_echo,
     "Write input to standard error as it is read, the -v flag."                              },
    /* The keyword-assignment and the DEBUG/RETURN trace toggles are accepted so
       a bash config that sets them keeps sourcing. Brace expansion is always on
       in shit, so -B is already the behavior and +B is accepted without turning
       it off. */
    {'k',  "keyword",          nullptr,                            nullptr,                     "Accepted without effect."               },
    {'T',  "functrace",        nullptr,                            nullptr,                     "Accepted without effect."               },
    {'B',  "braceexpand",      nullptr,                            nullptr,                     "Accepted without effect."               },
    /* The shell's own debug toggles, so set -A turns the AST dump on at runtime
       the same way the -A flag does at startup. */
    {'A',  "show-ast",         &EvalContext::set_show_ast,         &EvalContext::show_ast,
     "Print the AST before each command runs."                                                                                           },
    {'M',  "show-lexed-words", &EvalContext::set_show_lexed_words,
     &EvalContext::show_lexed_words,
     "Print the escape bitmap after each parse."                                                                                         },
    {'E',  "show-exit-code",   &EvalContext::set_show_exit_code,
     &EvalContext::show_exit_code,                                                              "Print the exit code after each command."},
    {'S',  "show-stats",       &EvalContext::set_stats_enabled,
     &EvalContext::stats_enabled,
     "Print evaluation statistics after each run."                                                                                       },
    {'\0', "bash-compatible",  &EvalContext::set_bash_compatible,
     &EvalContext::is_bash_compatible,
     "Behave like bash, the bash-compatible mode."                                            },
    {'\0', "no-diagnostics",   &EvalContext::set_diagnostics_disabled,
     &EvalContext::diagnostics_disabled,
     "Skip the analysis stage before each chunk runs."                                        },
    {'G',  "show-memory",      &EvalContext::set_memory_stats_enabled,
     &EvalContext::memory_stats_enabled,
     "Print a granular memory report at exit, the -G flag."                                   },
    /* The startup facts. A null set with a live get reports the state in the
       listings while set -o refuses to change it, since the choice happened at
       invocation. */
    {'\0', "login",            nullptr,                            &EvalContext::is_login_shell,
     "Whether the shell started as a login shell, fixed at startup."                          },
    {'\0', "init-as-bash",     nullptr,                            &EvalContext::inited_as_bash,
     "Whether the shell initialized from the bash configs, fixed at startup."                 },
    {'\0', "rcfile",           nullptr,                            &EvalContext::has_custom_rcfile,
     "Whether a custom rc file was named at startup, fixed at startup."                        },
};

const SetOption *find_option_by_letter(char letter) throws
{
  for (const SetOption &option : SET_OPTIONS)
    if (option.letter == letter) return &option;
  return nullptr;
}

const SetOption *find_option_by_name(StringView name) throws
{
  /* The alias is the command line's --help spelling, so set -o accepts both
     'noglob' and 'no-glob' while the listings print the canonical name a
     portable script replays. */
  for (const SetOption &option : SET_OPTIONS)
    if (option.name == name ||
        (!option.alias.is_empty() && option.alias == name))
      return &option;
  return nullptr;
}

bool option_is_on(const EvalContext &cxt, const SetOption &option) throws
{
  return option.get != nullptr ? (cxt.*(option.get))() : false;
}

/* A startup fact carries a live get and no set, so the state lists while a
   change is refused, since the choice happened at invocation. */
bool option_is_startup_fact(const SetOption &option) throws
{
  return option.set == nullptr && option.get != nullptr;
}

/* The one application path every entry point shares, so a startup fact is
   refused the same way from set -o and the letter form. */
void apply_or_reject_option(EvalContext &cxt, const SetOption &option,
                            bool enable) throws
{
  if (option_is_startup_fact(option))
    throw Error{"Unable to change '" + String{option.name} +
                "' because it is fixed at shell startup"};
  if (option.set != nullptr) (cxt.*(option.set))(enable);
}

/* The reusable command form that set -o and set +o print, one line each. The
   shell's own debug toggles, named show-*, are left out so set -o lists only
   the standard options a portable script expects, the way bash never prints a
   non-standard option name. set -p still lists them for discovery. */
String list_options(const EvalContext &cxt) throws
{
  let out = String{};
  for (const SetOption &option : SET_OPTIONS) {
    if (option.name.starts_with(StringView{"show-"})) continue;
    /* A startup fact cannot be replayed through set -o, so the replay listing
       leaves it out and set -p stays the place that shows it. */
    if (option_is_startup_fact(option)) continue;
    out += option_is_on(cxt, option) ? "set -o " : "set +o ";
    out += option.name;
    out += '\n';
  }
  return out;
}

/* The full table that set -p prints, every toggleable option with its letter,
   its long name, its current state, and a one-line description, so a user can
   discover what is available without leaving the shell. */
String list_options_with_help(const EvalContext &cxt) throws
{
  let out = String{};
  for (const SetOption &option : SET_OPTIONS) {
    out += "  ";
    if (option.letter != '\0') {
      out.push('-');
      out.push(option.letter);
    } else {
      out += "  ";
    }
    out += "  ";
    out += option.name;
    for (usize pad = option.name.length; pad < 18; pad++)
      out.push(' ');
    out += option_is_on(cxt, option) ? "[on]  " : "[off] ";
    out += option.help;
    out.push('\n');
  }
  return out;
}

} /* namespace */

fn query_shell_option(const EvalContext &cxt, StringView name) throws
    -> Maybe<bool>
{
  const SetOption *option = find_option_by_name(name);
  if (option == nullptr) return None;
  return option_is_on(cxt, *option);
}

fn apply_shell_option(EvalContext &cxt, StringView name, bool enable) throws
    -> bool
{
  const SetOption *option = find_option_by_name(name);
  if (option == nullptr) return false;
  apply_or_reject_option(cxt, *option, enable);
  return true;
}

Set::Set() = default;

pure Builtin::Kind Set::kind() const wontthrow { return Kind::Set; }

i32 Set::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* set with no arguments lists the shell variables. */
  if (args.count() == 1) {
    let out = String{};
    for (let const &assignment : cxt.sorted_variable_assignments()) {
      out += assignment.view();
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  let operands = ArrayList<String>{heap_allocator()};
  bool collecting_operands = false;
  bool should_rebind = false;

  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];

    if (collecting_operands) {
      operands.push_managed(arg);
      continue;
    }

    if (arg == "--") {
      collecting_operands = true;
      should_rebind = true;
      continue;
    }

    /* The long form -o name and +o name names one option, or lists them all
       when no name follows. */
    if (arg == "-o" || arg == "+o") {
      let const enable = arg[0] == '-';
      if (i + 1 >= args.count()) {
        ec.print_to_stdout(list_options(cxt));
        continue;
      }
      ASSERT(i + 1 < args.count());
      let const &name = args[++i];
      let const option = find_option_by_name(name);
      if (option == nullptr)
        throw Error{StringView{"unknown -o option '"} + name + "'"};
      apply_or_reject_option(cxt, *option, enable);
      continue;
    }

    /* set -p prints the toggleable options with their state and help, a
       discovery aid available in every mode. The +p form is accepted without
       effect, since this shell has no privileged mode to turn off. */
    if (arg == "-p") {
      ec.print_to_stdout(list_options_with_help(cxt));
      continue;
    }
    if (arg == "+p") continue;

    /* A minus enables each following letter, a plus disables each one. */
    if (arg.length() > 1 && (arg[0] == '-' || arg[0] == '+')) {
      let const enable = arg[0] == '-';
      for (usize c = 1; c < arg.length(); c++) {
        let const letter = arg[c];
        let const option = find_option_by_letter(letter);
        if (option == nullptr) {
          let invalid_option = String{};
          invalid_option += arg[0];
          invalid_option += letter;
          throw Error{StringView{"unknown option '"} + invalid_option + "'"};
        }
        apply_or_reject_option(cxt, *option, enable);
      }
      continue;
    }

    /* The first non-option argument and everything after it rebinds the
       positional parameters. */
    collecting_operands = true;
    should_rebind = true;
    operands.push_managed(arg);
  }

  if (should_rebind) cxt.set_positional_params(steal(operands));

  return 0;
}

} /* namespace shit */
