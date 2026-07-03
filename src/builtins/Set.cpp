#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-aAbBCeEfGhIkmMnSTuvWx] [+aAbBCeEfGhIkmMnSTuvWx] "
                   "[-o name] [+o name] [-p] [--] [arg ...]");

HELP_DESCRIPTION_DECL(
    "The set builtin sets the shell options and the positional parameters.");

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

/* A null set with a live get is a startup fact set -o refuses to change. */
class SetOption
{
public:
  char letter;
  StringView name;
  void (EvalContext::*set)(bool);
  bool (EvalContext::*get)() const;
  StringView help;
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
    {'\0', "shitbox", &EvalContext::set_shitbox, &EvalContext::shitbox,
     "Resolve the bundled shitbox utility names directly as commands."},
    {'m', "monitor", &EvalContext::set_monitor, &EvalContext::monitor,
     "Run background jobs in their own process group with notifications."},
    {'\0', "failglob", &EvalContext::set_failglob, &EvalContext::failglob,
     "Fail a command whose glob matches nothing."},
    {'b', "notify", &EvalContext::set_notify, &EvalContext::notify,
     "Report a background job's completion immediately when it finishes."},
    {'\0', "vi", &EvalContext::set_vi_mode, &EvalContext::vi_mode,
     "Use vi-style command-line editing."},
    {'\0', "emacs", &EvalContext::set_emacs_mode, &EvalContext::emacs_mode,
     "Use emacs-style command-line editing, the default."},
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
    {'\0', "login", nullptr, &EvalContext::is_login_shell,
     "Whether the shell started as a login shell, fixed at startup."},
    {'\0', "rcfile", nullptr, &EvalContext::has_custom_rcfile,
     "Whether a custom rc file was named at startup, fixed at startup."},
};

fn parse_mood_name(StringView name) throws -> Maybe<mimic_mood>
{
  static constexpr static_string_entry<mimic_mood> ENTRIES[] = {
      {SSK("shit"),    mimic_mood::Default},
      {SSK("default"), mimic_mood::Default},
      {SSK("bash"),    mimic_mood::Bash   },
      {SSK("sh"),      mimic_mood::Posix  },
      {SSK("posix"),   mimic_mood::Posix  },
      {SSK("dash"),    mimic_mood::Posix  },
  };
  static constexpr StaticStringMap MOOD_NAMES{ENTRIES};
  return MOOD_NAMES.find(name);
}

fn mood_name(mimic_mood mood) throws -> StringView
{
  switch (mood) {
  case mimic_mood::Bash: return "bash";
  case mimic_mood::Posix: return "sh";
  case mimic_mood::Default: return "shit";
  }
  return "shit";
}

fn find_option_by_letter(char letter) throws -> const SetOption *
{
  for (let const &option : SET_OPTIONS)
    if (option.letter == letter) return &option;
  return nullptr;
}

fn find_option_by_name(StringView name) throws -> const SetOption *
{
  /* The alias is the command line's --help spelling, so set -o accepts both
     'noglob' and 'no-glob' while the listings print the canonical name a
     portable script replays. */
  for (let const &option : SET_OPTIONS) {
    if (option.name == name ||
        (!option.alias.is_empty() && option.alias == name))
      return &option;
  }
  return nullptr;
}

fn option_is_on(const EvalContext &cxt, const SetOption &option) throws -> bool
{
  return option.get != nullptr ? (cxt.*(option.get))() : false;
}

fn option_is_startup_fact(const SetOption &option) throws -> bool
{
  return option.set == nullptr && option.get != nullptr;
}

fn apply_or_reject_option(EvalContext &cxt, const SetOption &option,
                          bool enable) throws -> void
{
  if (option_is_startup_fact(option))
    throw Error{
        "Unable to change '" + String{cxt.scratch_allocator(), option.name}
          +
        "' because it is fixed at shell startup"
    };
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

/* The show-* names are excluded from set -o but kept by set -p. */
fn list_options(const EvalContext &cxt) throws -> String
{
  let out = String{heap_allocator()};
  for (let const &option : SET_OPTIONS) {
    if (option.name.starts_with(StringView{"show-"})) continue;
    if (option_is_startup_fact(option)) continue;
    out += option_is_on(cxt, option) ? "set -o " : "set +o ";
    out += option.name;
    out += '\n';
  }
  return out;
}

/* The bare set -o listing is a two-column name and state table, while set +o
   prints the replayable set +o form list_options builds. */
fn list_options_columnar(const EvalContext &cxt) throws -> String
{
  const usize name_field_width = 15;
  let out = String{heap_allocator()};
  for (let const &option : SET_OPTIONS) {
    if (option.name.starts_with(StringView{"show-"})) continue;
    if (option_is_startup_fact(option)) continue;
    out += option.name;
    for (usize pad = option.name.length; pad < name_field_width; pad++)
      out.push(' ');
    out.push('\t');
    out += option_is_on(cxt, option) ? "on" : "off";
    out.push('\n');
  }
  return out;
}

fn apply_long_option_by_name(const ExecContext &ec, EvalContext &cxt,
                             const ArrayList<String> &args, usize &i,
                             bool enable) throws -> void
{
  if (i + 1 >= args.count()) {
    ec.print_to_stdout(enable ? list_options_columnar(cxt) : list_options(cxt));
    return;
  }
  let const &name = args[++i];
  let const option = find_option_by_name(name);
  if (option == nullptr)
    throw Error{StringView{"Unknown -o option '"} + name + "'"};
  apply_or_reject_option(cxt, *option, enable);
}

fn format_option_table(const EvalContext *cxt,
                       bool include_alias_spellings) throws -> String
{
  const usize name_field_width = include_alias_spellings ? 30 : 18;
  let out = String{heap_allocator()};
  for (let const &option : SET_OPTIONS) {
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

fn format_option_switches_help() throws -> String
{
  let section = String{"OPTION SWITCHES\n"};
  section += "  A letter after a minus enables the option and after a plus "
             "disables it.\n  -o NAME and +o NAME do the same by long "
             "name.\n\n";
  section += format_option_table(nullptr, true);
  section += "\n  The -o long names:\n";
  let listed_names = String{heap_allocator()};
  for (let const &option : SET_OPTIONS) {
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

} // namespace

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
  static ArrayList<StringView> canonical = [] throws {
    let names = ArrayList<StringView>{heap_allocator()};
    for (let const &option : SET_OPTIONS)
      names.push(option.name);
    return names;
  }();
  static ArrayList<StringView> with_aliases = [] throws {
    let names = ArrayList<StringView>{heap_allocator()};
    for (let const &option : SET_OPTIONS) {
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
    let collected = String{heap_allocator()};
    for (let const &option : SET_OPTIONS)
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

pure fn Set::kind() const wontthrow -> Builtin::Kind { return Kind::Set; }

fn Set::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") {
    SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(ec,
                                       format_option_switches_help().view());
  }

  if (args.count() == 1) {
    let out = String{cxt.scratch_allocator()};
    for (let const &assignment : cxt.sorted_variable_assignments()) {
      out += assignment.view();
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  let operands = ArrayList<String>{heap_allocator()};
  bool is_collecting_operands = false;
  bool should_rebind = false;

  for (usize i = 1; i < args.count(); i++) {
    let const &arg = args[i];

    if (is_collecting_operands) {
      operands.push_managed(arg);
      continue;
    }

    if (arg == "--") {
      is_collecting_operands = true;
      should_rebind = true;
      continue;
    }

    if (arg == "--mood" || arg == "-M" ||
        arg.view().starts_with(StringView{"--mood="}) ||
        arg.view().starts_with(StringView{"-M="}))
    {
      StringView value{};
      bool has_value = false;
      if (let const eq = arg.view().find_character('='); eq.has_value()) {
        value = arg.view().substring(*eq + 1);
        has_value = true;
      } else if (i + 1 < args.count()) {
        value = args[++i].view();
        has_value = true;
      }
      if (!has_value) {
        ec.print_to_stdout(
            String{cxt.scratch_allocator(), mood_name(cxt.mood())} + "\n");
        continue;
      }
      let const parsed = parse_mood_name(value);
      if (!parsed.has_value())
        throw Error{
            String{cxt.scratch_allocator(), "Unknown --mood value '"}
            + value +
            "', expected 'shit', 'bash', or 'sh'"
        };
      cxt.set_mood(*parsed);
      cxt.apply_strictness_for_mood();
      cxt.note_explicit_mood();
      continue;
    }

    if (arg == "--init-moods" || arg == "-L" ||
        arg.view().starts_with(StringView{"--init-moods="}) ||
        arg.view().starts_with(StringView{"-L="}))
    {
      StringView value{};
      bool has_value = false;
      if (let const eq = arg.view().find_character('='); eq.has_value()) {
        value = arg.view().substring(*eq + 1);
        has_value = true;
      } else if (i + 1 < args.count()) {
        value = args[++i].view();
        has_value = true;
      }
      if (!has_value) {
        let out = String{cxt.scratch_allocator()};
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
      let moods = ArrayList<mimic_mood>{cxt.scratch_allocator()};
      usize name_start = 0;
      for (usize j = 0; j <= value.length; j++) {
        if (j != value.length && value[j] != ',') continue;
        let const name = value.substring_of_length(name_start, j - name_start);
        name_start = j + 1;
        if (name.is_empty()) continue;
        let const parsed = parse_mood_name(name);
        if (!parsed.has_value())
          throw Error{
              String{cxt.scratch_allocator(), "Unknown --init-moods value '"}
              +
              name + "', expected 'shit', 'bash', or 'sh'"
          };
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

    if (arg == "-o" || arg == "+o") {
      apply_long_option_by_name(ec, cxt, args, i, arg[0] == '-');
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

    /* A lone - or + ends option parsing, and a lone - also turns off xtrace
       and verbose. The positional parameters are rebound only when a word
       follows, so set - with no argument leaves them untouched. */
    if (arg == "-" || arg == "+") {
      if (arg[0] == '-') {
        apply_or_reject_option(cxt, *find_option_by_letter('x'), false);
        apply_or_reject_option(cxt, *find_option_by_letter('v'), false);
      }

      is_collecting_operands = true;
      should_rebind = i + 1 < args.count();
      continue;
    }

    if (arg.length() > 1 && (arg[0] == '-' || arg[0] == '+')) {
      let const enable = arg[0] == '-';
      for (usize c = 1; c < arg.length(); c++) {
        let const letter = arg[c];

        /* The o letter ends the bundle and names one option from the next
           argument, the way bash accepts set -euo pipefail. */
        if (letter == 'o') {
          apply_long_option_by_name(ec, cxt, args, i, enable);
          break;
        }

        let const option = find_option_by_letter(letter);
        if (option == nullptr) {
          let invalid_option = String{heap_allocator()};
          invalid_option += arg[0];
          invalid_option += letter;
          throw Error{StringView{"Unknown option '"} + invalid_option + "'"};
        }
        apply_or_reject_option(cxt, *option, enable);
      }
      continue;
    }

    is_collecting_operands = true;
    should_rebind = true;
    operands.push_managed(arg);
  }

  if (should_rebind) {
    LOG(Debug, "set rebinding %zu positional parameters", operands.count());
    cxt.set_positional_params(steal(operands));
  }

  return 0;
}

} // namespace shit
