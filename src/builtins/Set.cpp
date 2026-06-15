#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

/* set toggles the shell options and rebinds the positional parameters. An
   option is named by a single letter after a minus to enable it or a plus to
   disable it, or by its long name after -o or +o. The letters and names that
   have no backing behavior yet are accepted without effect. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aAbBCeEfGhIkmMnSTuvWx] [+aAbBCeEfGhIkmMnSTuvWx] "
                   "[-o name] [+o name] [-p] [--] [arg ...]");

HELP_DESCRIPTION_DECL(
    "The set builtin enables a shell option named by a single letter after a "
    "minus or by its long name after -o, and disables it with a plus, and "
    "rebinds the positional parameters to its operands after a double dash or "
    "the first non-option word. With no argument it lists the shell "
    "variables. The OPTION SWITCHES section below describes every switch "
    "letter and every long name -o and +o accept, and -p prints the same "
    "table with each option's current state.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
/* The mood flags are parsed by hand in execute(), so these declarations only
   join the set builtin's flag list for completion and the help listing. */
FLAG(MOOD, String, 'M', "mood",
     "Set the runtime mood to shit, bash, or sh, or print it with no value.");
FLAG(INIT_MOODS, ManyStrings, 'L', "init-moods",
     "Source the startup files for the listed moods, or print the loaded ones "
     "with no value.");

REGISTER_BUILTIN_FLAGS(Set);

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
    {'e', "errexit", &EvalContext::set_error_exit, &EvalContext::error_exit,
     "Exit on the first command that fails.", "error-exit"},
    {'x', "xtrace", &EvalContext::set_echo_expanded,
     &EvalContext::should_echo_expanded,
     "Print each command after expansion before it runs."},
    {'u', "nounset", &EvalContext::set_error_unset, &EvalContext::error_unset,
     "Treat an unset variable as an error.", "no-unset"},
    {'\0', "pipefail", &EvalContext::set_pipefail, &EvalContext::pipefail,
     "Report a pipeline's status as the rightmost stage that failed."},
    {'a', "allexport", &EvalContext::set_export_all, &EvalContext::export_all,
     "Mark every assigned variable for the environment.", "export-all"},
    {'C', "noclobber", &EvalContext::set_no_clobber, &EvalContext::no_clobber,
     "Refuse to overwrite an existing file through '>'.", "no-clobber"},
    {'f', "noglob", &EvalContext::set_no_glob, &EvalContext::no_glob,
     "Disable pathname expansion.", "no-glob"},
    {'n', "noexec", &EvalContext::set_no_exec, &EvalContext::no_exec,
     "Read and parse commands but do not run them.", "no-exec"},
    {'m', "monitor", &EvalContext::set_monitor, &EvalContext::monitor,
     "Run background jobs in their own process group with notifications."},
    /* failglob has no short letter, so '\0' keeps find_option_by_letter from
       ever matching a parsed option character. */
    {'\0', "failglob", &EvalContext::set_failglob, &EvalContext::failglob,
     "Fail a command whose glob matches nothing."},
    {'b', "notify", &EvalContext::set_notify, &EvalContext::notify,
     "Report a background job's completion immediately instead of before the "
     "next prompt."},
    {'h', "hashall", nullptr, nullptr, "Accepted without effect."},
    {'v', "verbose", &EvalContext::set_echo, &EvalContext::should_echo,
     "Write input to standard error as it is read, the -v flag."},
    /* The keyword-assignment and the DEBUG/RETURN trace toggles are accepted so
       a bash config that sets them keeps sourcing. Brace expansion is always on
       in shit, so -B is already the behavior and +B is accepted without turning
       it off. */
    {'k', "keyword", nullptr, nullptr, "Accepted without effect."},
    {'T', "functrace", nullptr, nullptr, "Accepted without effect."},
    {'B', "braceexpand", nullptr, nullptr, "Accepted without effect."},
    /* The shell's own debug toggles, so set -A turns the AST dump on at runtime
       the same way the -A flag does at startup. */
    {'A', "show-ast", &EvalContext::set_show_ast, &EvalContext::show_ast,
     "Print the AST before each command runs."},
    {'R', "show-lexed-words", &EvalContext::set_show_lexed_words,
     &EvalContext::show_lexed_words,
     "Print the escape bitmap after each parse."},
    {'E', "show-exit-code", &EvalContext::set_show_exit_code,
     &EvalContext::show_exit_code, "Print the exit code after each command."},
    {'W', "force-warnings", &EvalContext::set_warnings_enabled,
     &EvalContext::warnings_enabled,
     "Report a strict runtime error as a warning and let the run proceed, the "
     "-W flag."},
    {'I', "mimicry", &EvalContext::set_mimicry, &EvalContext::mimicry,
     "Mimic the shell a script's shebang names, the -I flag."},
    {'\0', "force-diagnostics", &EvalContext::set_diagnostics_enabled,
     &EvalContext::diagnostics_enabled,
     "Run the analysis stage before each chunk, the inverse of "
     "no-diagnostics."},
    {'S', "show-stats", &EvalContext::set_stats_enabled,
     &EvalContext::stats_enabled,
     "Print evaluation statistics after each run."},
    {'\0', "no-diagnostics", &EvalContext::set_diagnostics_disabled,
     &EvalContext::diagnostics_disabled,
     "Skip the analysis stage before each chunk runs."},
    {'G', "show-memory", &EvalContext::set_memory_stats_enabled,
     &EvalContext::memory_stats_enabled,
     "Print a granular memory report at exit, the --show-memory flag."},
    /* The startup facts. A null set with a live get reports the state in the
       listings while set -o refuses to change it, since the choice happened at
       invocation. */
    {'\0', "login", nullptr, &EvalContext::is_login_shell,
     "Whether the shell started as a login shell, fixed at startup."},
    {'\0', "rcfile", nullptr, &EvalContext::has_custom_rcfile,
     "Whether a custom rc file was named at startup, fixed at startup."},
};

/* Parse a set --mood value, with 'shit' the strict default, 'bash' the bash
   extensions, and 'sh' or 'posix' the dash semantics. An unknown spelling
   returns None so the caller reports the usage error the same way --mood does
   at startup. */
Maybe<mimic_mood> parse_mood_name(StringView name) throws
{
  if (name == "shit" || name == "default") return mimic_mood::Default;
  if (name == "bash") return mimic_mood::Bash;
  if (name == "sh" || name == "posix" || name == "dash")
    return mimic_mood::Posix;
  return None;
}

/* The canonical name of a mood, for the set --mood readout with no value. */
StringView mood_name(mimic_mood mood) throws
{
  switch (mood) {
  case mimic_mood::Bash: return "bash";
  case mimic_mood::Posix: return "sh";
  case mimic_mood::Default: return "shit";
  }
  return "shit";
}

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
  LOG(Info, "set flipping option '%.*s' to %s",
      static_cast<int>(option.name.length), option.name.data,
      enable ? "on" : "off");
  if (option.set != nullptr) (cxt.*(option.set))(enable);
  /* An explicit set -u or set -o failglob is the script's own ask, so the -W
     downgrade leaves it fatal. The mark follows the toggle both ways. */
  if (option.set == &EvalContext::set_error_unset)
    cxt.set_error_unset_explicit(enable);
  else if (option.set == &EvalContext::set_failglob)
    cxt.set_failglob_explicit(enable);
  else if (option.set == &EvalContext::set_pipefail)
    cxt.set_pipefail_explicit(enable);
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

/* The full option table, one row per option with its letter, its long name,
   and its description. set -p passes the context and gains a state column,
   while set --help passes null and gains the alias spellings, so both
   listings read from the one table. */
String format_option_table(const EvalContext *cxt,
                           bool include_alias_spellings) throws
{
  /* The plain set -p column fits the bare names, while the --help column
     also holds the ", alias" spellings. */
  const usize name_field_width = include_alias_spellings ? 30 : 18;
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
    let name_cell = String{option.name};
    if (include_alias_spellings && !option.alias.is_empty()) {
      name_cell += ", ";
      name_cell += option.alias;
    }
    out += name_cell.view();
    for (usize pad = name_cell.count(); pad < name_field_width; pad++)
      out.push(' ');
    if (cxt != nullptr) out += option_is_on(*cxt, option) ? "[on]  " : "[off] ";
    out += option.help;
    out.push('\n');
  }
  return out;
}

/* The OPTION SWITCHES help section, the intro line, the full table with the
   alias spellings, and the long names -o accepts on one wrapped list, so set
   --help describes every switch. */
String format_option_switches_help() throws
{
  let section = String{"OPTION SWITCHES\n"};
  section += "  A letter after a minus enables the option and after a plus "
             "disables it.\n  -o NAME and +o NAME do the same by long "
             "name.\n\n";
  section += format_option_table(nullptr, true);
  section += "\n  The -o long names:\n";
  let listed_names = String{};
  for (const SetOption &option : SET_OPTIONS) {
    if (!listed_names.is_empty()) listed_names += ", ";
    listed_names += option.name;
    if (!option.alias.is_empty()) {
      listed_names += ", ";
      listed_names += option.alias;
    }
  }
  section += wrap_text(listed_names.view(), 4, HELP_WRAP_WIDTH);
  section += '\n';
  return section;
}

} /* namespace */

fn query_shell_option(const EvalContext &cxt, StringView name) throws
    -> Maybe<bool>
{
  const SetOption *option = find_option_by_name(name);
  if (option == nullptr) return None;
  return option_is_on(cxt, *option);
}

fn shell_option_names(bool include_alias_spellings) throws
    -> const ArrayList<StringView> &
{
  /* SET_OPTIONS is immutable and the completion engine reads these on every
     keystroke, so both spellings build once. */
  static ArrayList<StringView> canonical = [] throws {
    let names = ArrayList<StringView>{};
    for (const SetOption &option : SET_OPTIONS)
      names.push(option.name);
    return names;
  }();
  static ArrayList<StringView> with_aliases = [] throws {
    let names = ArrayList<StringView>{};
    for (const SetOption &option : SET_OPTIONS) {
      names.push(option.name);
      if (!option.alias.is_empty()) names.push(option.alias);
    }
    return names;
  }();
  return include_alias_spellings ? with_aliases : canonical;
}

fn shell_option_letters() throws -> const String &
{
  static String letters = [] throws {
    let collected = String{};
    for (const SetOption &option : SET_OPTIONS)
      if (option.letter != '\0') collected.push(option.letter);
    return collected;
  }();
  return letters;
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

  if (args.count() > 1 && args[1] == "--help")
    SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(ec,
                                       format_option_switches_help().view());

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

    /* set --mood [VALUE] changes the runtime mood, or prints the active mood
       when no value follows. The mood drives strictness, so the nounset,
       pipefail, and failglob defaults are reseeded from the new mood. */
    if (arg == "--mood" || arg == "-M" ||
        arg.view().starts_with(StringView{"--mood="}) ||
        arg.view().starts_with(StringView{"-M="}))
    {
      StringView value{};
      bool have_value = false;
      if (let const eq = arg.view().find_character('='); eq.has_value()) {
        value = arg.view().substring(*eq + 1);
        have_value = true;
      } else if (i + 1 < args.count()) {
        value = args[++i].view();
        have_value = true;
      }
      if (!have_value) {
        ec.print_to_stdout(String{mood_name(cxt.mood())} + "\n");
        continue;
      }
      let const parsed = parse_mood_name(value);
      if (!parsed.has_value())
        throw Error{String{"unknown --mood value '"} + value +
                    "', expected 'shit', 'bash', or 'sh'"};
      cxt.set_mood(*parsed);
      cxt.apply_strictness_for_mood();
      cxt.note_explicit_mood();
      continue;
    }

    /* set --init-moods=LIST re-runs the startup files for each listed flavor in
       the live session, the way the --init-moods flag does at startup, so a
       bash rc or a shit rc reloads on demand. */
    if (arg == "--init-moods" || arg == "-L" ||
        arg.view().starts_with(StringView{"--init-moods="}) ||
        arg.view().starts_with(StringView{"-L="}))
    {
      StringView value{};
      bool have_value = false;
      if (let const eq = arg.view().find_character('='); eq.has_value()) {
        value = arg.view().substring(*eq + 1);
        have_value = true;
      } else if (i + 1 < args.count()) {
        value = args[++i].view();
        have_value = true;
      }
      /* With no value the form reports the moods whose startup files have
         loaded this session, the way set --mood reports the active mood. */
      if (!have_value) {
        let out = String{};
        for (mimic_mood listed :
             {mimic_mood::Default, mimic_mood::Posix, mimic_mood::Bash})
        {
          if (!cxt.mood_initialized(listed)) continue;
          if (!out.is_empty()) out += " ";
          out += mood_name(listed);
        }
        out += "\n";
        ec.print_to_stdout(out);
        continue;
      }
      let moods = ArrayList<mimic_mood>{};
      usize name_start = 0;
      for (usize j = 0; j <= value.length; j++) {
        if (j != value.length && value[j] != ',') continue;
        let const name = value.substring_of_length(name_start, j - name_start);
        name_start = j + 1;
        if (name.is_empty()) continue;
        let const parsed = parse_mood_name(name);
        if (!parsed.has_value())
          throw Error{String{"unknown --init-moods value '"} + name +
                      "', expected 'shit', 'bash', or 'sh'"};
        moods.push(*parsed);
      }
      if (AST_ARENA == nullptr)
        throw Error{"Unable to source the init moods outside of a parse"};
      let const previous_mood = cxt.mood();
      source_init_moods(cxt, *AST_ARENA, moods, cxt.is_login_shell(),
                        cxt.shell_is_interactive());
      cxt.set_mood(previous_mood);
      cxt.apply_strictness_for_mood();
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
      ec.print_to_stdout(format_option_table(&cxt, false));
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

  if (should_rebind) {
    LOG(Debug, "set rebinding %zu positional parameters", operands.count());
    cxt.set_positional_params(steal(operands));
  }

  return 0;
}

} /* namespace shit */
