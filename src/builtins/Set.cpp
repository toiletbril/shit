#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../StaticStringMap.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abefhkmnuvxBCEPTARWISG] [+abefhkmnuvxBCEPTARWISG] "
                   "[-o name] [+o name] [--options] [--] [arg ...]");

HELP_DESCRIPTION_DECL(
    "The set builtin sets the shell options and the positional parameters.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(OPTIONS, Bool, '\0', "options",
     "Display every option with its current state and description.");
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

enum class set_option_behavior : u8
{
  Stored,
  Posix,
  Vi,
  Emacs,
  Warnings,
  ForceDiagnostics,
  NoDiagnostics,
  Login,
  Rcfile,
};

struct set_option_descriptor
{
  shell_option_id id;
  set_option_behavior behavior;
  char letter;
  StringView name;
  StringView help;
  StringView alias{};
  bool is_in_shellopts{false};
  bool is_listed{true};
};

const set_option_descriptor SET_OPTIONS[] = {
    {shell_option_id::Allexport, set_option_behavior::Stored, 'a', "allexport",
     "Mark every assigned variable for the environment.", "export-all", true},
    {shell_option_id::Notify,
     set_option_behavior::Stored,
     'b', "notify",
     "Report a background job's completion immediately when it finishes.", {},
     true},
    {shell_option_id::Errexit, set_option_behavior::Stored, 'e', "errexit",
     "Exit on the first command that fails.", "error-exit", true},
    {shell_option_id::Noglob, set_option_behavior::Stored, 'f', "noglob",
     "Disable pathname expansion.", "no-glob", true},
    {shell_option_id::Hashall,
     set_option_behavior::Stored,
     'h', "hashall",
     "Retain command hashing mode for compatible option queries.", {},
     true},
    {shell_option_id::Keyword,
     set_option_behavior::Stored,
     'k', "keyword",
     "Retain keyword assignment mode for compatible option queries.", {},
     true},
    {shell_option_id::Monitor,
     set_option_behavior::Stored,
     'm', "monitor",
     "Run background jobs in their own process group with notifications.", {},
     true},
    {shell_option_id::Noexec, set_option_behavior::Stored, 'n', "noexec",
     "Read and parse commands but do not run them.", "no-exec", true},
    {shell_option_id::Nounset, set_option_behavior::Stored, 'u', "nounset",
     "Treat an unset variable as an error.", "no-unset", true},
    {shell_option_id::Verbose,
     set_option_behavior::Stored,
     'v', "verbose",
     "Write input to standard error as it is read.", {},
     true},
    {shell_option_id::Xtrace,
     set_option_behavior::Stored,
     'x', "xtrace",
     "Print each command after expansion before it runs.", {},
     true},
    {shell_option_id::Braceexpand,
     set_option_behavior::Stored,
     'B', "braceexpand",
     "Enable brace expansion.", {},
     true},
    {shell_option_id::Noclobber, set_option_behavior::Stored, 'C', "noclobber",
     "Refuse to overwrite an existing file through '>'.", "no-clobber", true},
    {shell_option_id::Errtrace,
     set_option_behavior::Stored,
     'E', "errtrace",
     "Retain ERR trap inheritance mode for compatible option queries.", {},
     true},
    {shell_option_id::Physical,
     set_option_behavior::Stored,
     'P', "physical",
     "Resolve symbolic links while changing directories.", {},
     true},
    {shell_option_id::Functrace,
     set_option_behavior::Stored,
     'T', "functrace",
     "Retain DEBUG and RETURN inheritance mode for compatible option queries.", {},
     true},
    {shell_option_id::Pipefail,
     set_option_behavior::Stored,
     '\0', "pipefail",
     "Report a pipeline's status as the rightmost stage that failed.", {},
     true},
    {shell_option_id::Failglob,
     set_option_behavior::Stored,
     '\0', "failglob",
     "Fail a command whose glob matches nothing.", {},
     true},
    {shell_option_id::Shitbox, set_option_behavior::Stored, '\0', "shitbox",
     "Resolve the bundled shitbox utility names directly as commands."},
    {shell_option_id::Vi,
     set_option_behavior::Vi,
     '\0', "vi",
     "Use vi-style command-line editing.", {},
     true},
    {shell_option_id::Emacs,
     set_option_behavior::Emacs,
     '\0', "emacs",
     "Use emacs-style command-line editing.", {},
     true},
    {shell_option_id::Count,
     set_option_behavior::Posix,
     '\0', "posix",
     "Switch to the bash posix mood.", {},
     true},
    {shell_option_id::ShowAst,
     set_option_behavior::Stored,
     'A', "show-ast",
     "Print the AST before each command runs.", {},
     false, false},
    {shell_option_id::ShowLexedWords,
     set_option_behavior::Stored,
     'R', "show-lexed-words",
     "Print the escape bitmap after each parse.", {},
     false, false},
    {shell_option_id::ShowExitCode,
     set_option_behavior::Stored,
     '\0', "show-exit-code",
     "Print the exit code after each command.", {},
     false, false},
    {shell_option_id::Count, set_option_behavior::Warnings, 'W',
     "force-warnings", "Report strict runtime errors as warnings and continue."},
    {shell_option_id::Mimicry, set_option_behavior::Stored, 'I', "mimicry",
     "Mimic the shell named by a script's shebang."},
    {shell_option_id::Count, set_option_behavior::ForceDiagnostics, '\0',
     "force-diagnostics", "Run the analysis stage before each chunk."},
    {shell_option_id::ShowStats,
     set_option_behavior::Stored,
     'S', "show-stats",
     "Print evaluation statistics after each run.", {},
     false, false},
    {shell_option_id::Count, set_option_behavior::NoDiagnostics, '\0',
     "no-diagnostics", "Skip the analysis stage before each chunk runs."},
    {shell_option_id::ShowMemory,
     set_option_behavior::Stored,
     'G', "show-memory",
     "Print a granular memory report at exit.", {},
     false, false},
    {shell_option_id::Count,
     set_option_behavior::Login,
     '\0', "login",
     "Whether the shell started as a login shell, fixed at startup.", {},
     false, false},
    {shell_option_id::Count,
     set_option_behavior::Rcfile,
     '\0', "rcfile",
     "Whether a custom rc file was named at startup, fixed at startup.", {},
     false, false},
};

constexpr static_string_entry<u8> SET_OPTION_NAME_ENTRIES[] = {
    {SSK("allexport"),         0 },
    {SSK("export-all"),        0 },
    {SSK("notify"),            1 },
    {SSK("errexit"),           2 },
    {SSK("error-exit"),        2 },
    {SSK("noglob"),            3 },
    {SSK("no-glob"),           3 },
    {SSK("hashall"),           4 },
    {SSK("keyword"),           5 },
    {SSK("monitor"),           6 },
    {SSK("noexec"),            7 },
    {SSK("no-exec"),           7 },
    {SSK("nounset"),           8 },
    {SSK("no-unset"),          8 },
    {SSK("verbose"),           9 },
    {SSK("xtrace"),            10},
    {SSK("braceexpand"),       11},
    {SSK("noclobber"),         12},
    {SSK("no-clobber"),        12},
    {SSK("errtrace"),          13},
    {SSK("physical"),          14},
    {SSK("functrace"),         15},
    {SSK("pipefail"),          16},
    {SSK("failglob"),          17},
    {SSK("shitbox"),           18},
    {SSK("vi"),                19},
    {SSK("emacs"),             20},
    {SSK("posix"),             21},
    {SSK("show-ast"),          22},
    {SSK("show-lexed-words"),  23},
    {SSK("show-exit-code"),    24},
    {SSK("force-warnings"),    25},
    {SSK("mimicry"),           26},
    {SSK("force-diagnostics"), 27},
    {SSK("show-stats"),        28},
    {SSK("no-diagnostics"),    29},
    {SSK("show-memory"),       30},
    {SSK("login"),             31},
    {SSK("rcfile"),            32},
};
constexpr StaticStringMap SET_OPTION_BY_NAME{SET_OPTION_NAME_ENTRIES};

fn find_option_by_letter(char letter) throws -> const set_option_descriptor *
{
  for (let const &option : SET_OPTIONS)
    if (option.letter == letter) return &option;
  return nullptr;
}

fn find_option_by_name(StringView name) throws -> const set_option_descriptor *
{
  let const position = SET_OPTION_BY_NAME.find(name);
  return position.has_value() ? &SET_OPTIONS[*position] : nullptr;
}

fn option_is_on(const EvalContext &cxt,
                const set_option_descriptor &option) throws -> bool
{
  switch (option.behavior) {
  case set_option_behavior::Stored: return cxt.shell_option_state(option.id);
  case set_option_behavior::Posix: return cxt.is_posix_option_on();
  case set_option_behavior::Vi: return cxt.vi_mode();
  case set_option_behavior::Emacs: return cxt.emacs_mode();
  case set_option_behavior::Warnings: return cxt.warnings_enabled();
  case set_option_behavior::ForceDiagnostics: return cxt.diagnostics_enabled();
  case set_option_behavior::NoDiagnostics: return cxt.diagnostics_disabled();
  case set_option_behavior::Login: return cxt.is_login_shell();
  case set_option_behavior::Rcfile: return cxt.has_custom_rcfile();
  }
  unreachable("Unhandled set option behavior");
}

fn option_is_startup_fact(const set_option_descriptor &option) throws -> bool
{
  return option.behavior == set_option_behavior::Login ||
         option.behavior == set_option_behavior::Rcfile;
}

fn apply_or_reject_option(EvalContext &cxt, const set_option_descriptor &option,
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
  switch (option.behavior) {
  case set_option_behavior::Stored:
    cxt.set_shell_option_state(option.id, enable);
    break;
  case set_option_behavior::Posix: cxt.set_posix_mode_via_option(enable); break;
  case set_option_behavior::Vi: cxt.set_vi_mode(enable); break;
  case set_option_behavior::Emacs: cxt.set_emacs_mode(enable); break;
  case set_option_behavior::Warnings: cxt.set_warnings_enabled(enable); break;
  case set_option_behavior::ForceDiagnostics:
    cxt.set_diagnostics_enabled(enable);
    break;
  case set_option_behavior::NoDiagnostics:
    cxt.set_diagnostics_disabled(enable);
    break;
  case set_option_behavior::Login:
  case set_option_behavior::Rcfile: unreachable("Startup fact was applied");
  }
  if (option.id == shell_option_id::Nounset)
    cxt.set_error_unset_explicit(enable);
  else if (option.id == shell_option_id::Failglob)
    cxt.set_failglob_explicit(enable);
  else if (option.id == shell_option_id::Pipefail)
    cxt.set_pipefail_explicit(enable);
}

fn list_options(const EvalContext &cxt) throws -> String
{
  let out = String{heap_allocator()};
  for (let const &option : SET_OPTIONS) {
    if (!option.is_listed) continue;
    out += option_is_on(cxt, option) ? "set -o " : "set +o ";
    out += option.name;
    out += '\n';
  }
  return out;
}

fn list_options_columnar(const EvalContext &cxt) throws -> String
{
  const usize name_field_width = 15;
  let out = String{heap_allocator()};
  for (let const &option : SET_OPTIONS) {
    if (!option.is_listed) continue;
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
    throw make_error_for_arg(ec, i,
                             StringView{"Unknown -o option '"} + name + "'");
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
  const set_option_descriptor *option = find_option_by_name(name);
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

fn enabled_shell_option_names(const EvalContext &cxt) throws -> String
{
  constexpr u8 INTERACTIVE_COMMENTS_POSITION = countof(SET_OPTIONS);
  constexpr u8 SHELLOPTS_ORDER[] = {
      0,  11, 20, 2, 13, 17, 15, 4,  INTERACTIVE_COMMENTS_POSITION,
      5,  6,  12, 7, 3,  1,  8,  14, 16,
      21, 9,  19, 10};

  let joined = String{heap_allocator()};
  let const do_append = [&](StringView name) throws {
    if (!joined.is_empty()) joined.push(':');
    joined.append(name);
  };
  for (let const position : SHELLOPTS_ORDER) {
    if (position == INTERACTIVE_COMMENTS_POSITION) {
      if (cxt.is_shopt_enabled("interactive_comments"))
        do_append("interactive-comments");
      continue;
    }
    let const &option = SET_OPTIONS[position];
    if (option_is_on(cxt, option)) do_append(option.name);
  }
  return joined;
}

fn enabled_shell_option_letters(const EvalContext &cxt) throws -> String
{
  let letters = String{heap_allocator()};
  for (let const &option : SET_OPTIONS) {
    if (option.letter == 'h' && cxt.shell_is_interactive()) letters.push('i');
    if (option.letter == '\0' || !option_is_on(cxt, option)) continue;
    letters.push(option.letter);
  }
  if (cxt.has_execution_string()) letters.push('c');
  return letters;
}

fn apply_shell_option(EvalContext &cxt, StringView name, bool enable) throws
    -> bool
{
  const set_option_descriptor *option = find_option_by_name(name);
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

  usize i = 1;
  let do_read_option_value =
      [&](const String &option_arg) -> Maybe<StringView> {
    if (let const eq = option_arg.view().find_character('='); eq.has_value())
      return option_arg.view().substring(*eq + 1);
    if (i + 1 < args.count()) return args[++i].view();
    return None;
  };

  for (; i < args.count(); i++) {
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
      let const value = do_read_option_value(arg);
      if (!value.has_value()) {
        ec.print_to_stdout(
            String{cxt.scratch_allocator(), mood_name(cxt.mood())} + "\n");
        continue;
      }
      let const parsed = parse_mood_name(*value);
      if (!parsed.has_value())
        throw make_error_for_arg(
            ec, i,
            String{cxt.scratch_allocator(), "Unknown --mood value '"} + *value +
                "', expected 'shit', 'bash', 'sh', or 'bash-posix'");
      cxt.set_mood(*parsed);
      cxt.apply_strictness_for_mood();
      cxt.note_explicit_mood();
      continue;
    }

    if (arg == "--init-moods" || arg == "-L" ||
        arg.view().starts_with(StringView{"--init-moods="}) ||
        arg.view().starts_with(StringView{"-L="}))
    {
      let const value = do_read_option_value(arg);
      if (!value.has_value()) {
        let out = String{cxt.scratch_allocator()};
        for (mimic_mood listed : {mimic_mood::Default, mimic_mood::Posix,
                                  mimic_mood::Bash, mimic_mood::BashPosix})
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
      for (usize j = 0; j <= value->length; j++) {
        if (j != value->length && (*value)[j] != ',') continue;
        let const name = value->substring_of_length(name_start, j - name_start);
        name_start = j + 1;
        if (name.is_empty()) continue;
        let const parsed = parse_mood_name(name);
        if (!parsed.has_value())
          throw make_error_for_arg(
              ec, i,
              String{cxt.scratch_allocator(), "Unknown --init-moods value '"} +
                  name + "', expected 'shit', 'bash', 'sh', or 'bash-posix'");
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

    if (arg == "--options") {
      ec.print_to_stdout(format_option_table(&cxt, false));
      continue;
    }

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
          throw make_error_for_arg(
              ec, i, StringView{"Unknown option '"} + invalid_option + "'");
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
