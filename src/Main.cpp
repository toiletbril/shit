#include "Arena.hpp"
#include "Cli.hpp"
#include "Colors.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Shellcheck.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <cstring>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-OPTIONS] [--] <file1> [file2, ...]", "[-OPTIONS] -",
                   "[-OPTIONS]");

FLAG(INTERACTIVE, Bool, 'i', "interactive",
     "Specify that the shell is interactive.");
FLAG(STDIN, Bool, 's', "stdin", "Execute command from stdin and exit.");
FLAG(COMMAND, ManyStrings, 'c', "command",
     "Execute specified command and exit. Can be used multiple times.");
FLAG(ERROR_EXIT, Bool, 'e', "error-exit", "Die on first error.");
FLAG(DISABLE_EXPANSION, Bool, 'f', "no-glob", "Disable path expansion.");
FLAG(ONE_COMMAND, Bool, 't', "one-command",
     "Exit after executing one command.");
FLAG(VERBOSE, Bool, 'v', "verbose",
     "Write input to standard error as it is read.");
FLAG(EXPAND_VERBOSE, Bool, 'x', "xtrace",
     "Write expanded input to standard error as it is read.");

FLAG(EXPORT_ALL, Bool, 'a', "export-all",
     "Mark every assigned variable for the environment.");
FLAG(NO_CLOBBER, Bool, 'C', "no-clobber",
     "Refuse to overwrite an existing file through '>'.");
FLAG(NO_EXEC, Bool, 'n', "no-exec",
     "Read and parse commands but do not run them.");
FLAG(NOUNSET, Bool, 'u', "no-unset", "Treat an unset variable as an error.");
/* The variable is POSIX_MODE rather than POSIX, since Platform.hpp defines
   POSIX as a platform-vector macro that would eat the flag name. */
FLAG(
    POSIX_MODE, Bool, 'P', "posix",
    "Run in POSIX mode, the way dash behaves. The analysis stage is skipped so "
    "a file with an analysis error still runs, an unmatched glob stays its "
    "literal pattern, and the style warnings are off.");
FLAG(BASH_COMPATIBLE, Bool, '\0', "bash-compatible",
     "Run in Bash-compatible mode. Bash extensions such as [[ ]], arrays, and "
     "brace expansion are enabled, the analysis stage is skipped, and an "
     "unmatched glob stays its literal pattern.");
FLAG(WARNINGS, Bool, 'W', "warnings",
     "Keep the analysis stage but report every error as a warning and let the "
     "run proceed, instead of stopping on the first error.");
FLAG(SUPPRESS_DIAGNOSTICS, Bool, '\0', "no-diagnostics",
     "Skip the analysis stage, so no warnings or pre-run diagnostics are "
     "reported and evaluation begins sooner.");
FLAG(LOGIN, Bool, 'l', "login",
     "Act as a login shell and source the profiles.");
FLAG(INIT_AS_BASH, Bool, 'L', "init-as-bash",
     "Initialize as bash, sourcing the bash config files in bash mode, then "
     "snap to the default mode with all diagnostics at the interactive prompt. "
     "Sources the bash login scripts only when combined with -l. The "
     "SHIT_INIT_AS_BASH environment variable enables this when set.");
FLAG(
    PRIVILEGED, Bool, 'p', "privileged",
    "Run in privileged mode and skip every startup config file, so a config a "
    "less-privileged user controls cannot run with raised privileges. Turned "
    "on automatically when the effective and the real user or group id differ, "
    "the setuid or setgid case.");
FLAG(MIMICRY, Bool, 'I', "mimicry",
     "Mimic the shell a script's shebang names. A program whose shebang is a "
     "shell shit can emulate runs in-process in the matching mode rather than "
     "launching the shell, where sh and dash run in POSIX mode, bash in bash "
     "mode, and shit in the default mode. A zsh, ksh, fish, or non-shell shebang "
     "still launches the real program.");

FLAG(IGNORED1, Bool, 'h', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", "Ignored, left for compatibility.");

FLAG(AST, Bool, 'A', "show-ast", "Print AST before executing each command.");
FLAG(ESCAPE_MAP, Bool, 'M', "show-lexed-words",
     "Print escape bitmap after each parsed command.");
FLAG(EXIT_CODE, Bool, 'E', "show-exit-code",
     "Print exit code after each executed command.");
FLAG(STATS, Bool, 'S', "show-stats",
     "Print statistics after each executed command, including commands "
     "evaluated, expansions, nodes evaluated, and AST arena bytes with the run "
     "peak.");
FLAG(MEMORY, Bool, 'G', "show-memory",
     "Print a granular memory report at exit, the AST and function arena bytes "
     "with their reserved capacity and the malloc heap in use.");
FLAG(NO_COMPLETION, Bool, 'T', "no-completion",
     "Disable interactive tab completion and ghost-text.");
FLAG(DUMB, Bool, '\0', "dumb",
     "Makes shit extremely dumb. Equals to -PT --no-diagnostics.");
FLAG(LIST_CHECKS, Bool, '\0', "list",
     "List the shellcheck-style checks the analysis stage reports, then exit.");
FLAG(LOG, Bool, 'X', "enable-debug-logging",
     "Enable verbose internal logging to stderr.");

FLAG(VERSION, Bool, '\0', "version", "Display program version and notices.");
FLAG(SHORT_VERSION, Bool, 'V', "short-version",
     "Display version in a short form.");
FLAG(HELP, Bool, '\0', "help", "Display help message.");

#if SHIT_PLATFORM_IS COSMO
FLAG(COSMO_FTRACE, Bool, '\0', "ftrace", "Cosmopolitan: Trace functions.");
FLAG(COSMO_STRACE, Bool, '\0', "strace", "Cosmopolitan: Trace system calls.");
#endif

namespace shit {

/* Set when the shell is invoked through a name whose basename is sh or dash,
   the way a system ln -s shit sh does. A script that names the shell after a
   system POSIX shell then runs compatibility-clean, the way bash run as sh
   switches to its POSIX mode. */
static bool INVOKED_AS_POSIX_SHELL = false;

/* Set when the basename is bash, so a script that names the shell bash gets the
   bash extensions the way real bash does. The invocation name splits here, sh
   and dash select POSIX mode while bash selects bash mode. */
static bool INVOKED_AS_BASH = false;

/* True when POSIX behavior is in effect, from --posix or the sh invocation
   name. The failglob default, the analysis skip, and the style-warning
   suppression all read it. */
pure static fn should_run_in_posix_mode() wontthrow -> bool
{
  return FLAG_POSIX_MODE.is_enabled() || INVOKED_AS_POSIX_SHELL;
}

/* True when bash behavior is in effect, from --bash-compatible or the bash
   invocation name. Like POSIX mode it skips the analysis stage and leaves an
   unmatched glob literal, and it additionally enables the bash extensions. */
pure static fn should_run_in_bash_mode() wontthrow -> bool
{
  return FLAG_BASH_COMPATIBLE.is_enabled() || INVOKED_AS_BASH;
}

/* True in either compatibility mode, POSIX or bash. Both skip the analysis
   stage and leave an unmatched glob literal, the way dash and bash do, so the
   analysis gate and the failglob default read this rather than spelling out the
   pair. */
pure static fn should_run_in_compat_mode() wontthrow -> bool
{
  return should_run_in_posix_mode() || should_run_in_bash_mode();
}

/* Print the help or version text and return the exit code when one of those
   flags is set, otherwise None so the shell proceeds to normal startup. */
static fn print_help_or_version_status(const String &program_path) -> Maybe<int>
{
  if (FLAG_HELP.is_enabled()) {
    let h = String{};
    h += "\n";
    h += wrap_text(
        "Shit, a pedantic, super-fast and awesome POSIX-compatible command "
        "line interpreter, or a friendly interactive shell for gigachads.\n\n",
        HELP_INDENT, HELP_WRAP_WIDTH);
    h += make_synopsis(program_path.view(), HELP_SYNOPSIS);
    h += '\n';
    h += make_flag_help(FLAG_LIST);
    h += '\n';
    print_error(h);
    return EXIT_SUCCESS;
  }
  if (FLAG_LIST_CHECKS.is_enabled()) {
    let l = String{};
    for (const shellcheck_check &check : SHELLCHECK_CHECKS) {
      l += check.code;
      l += "  ";
      l += check.summary;
      l += '\n';
    }
    print(l);
    return EXIT_SUCCESS;
  }
  if (FLAG_VERSION.is_enabled()) {
    show_version();
    return EXIT_SUCCESS;
  }
  if (FLAG_SHORT_VERSION.is_enabled()) {
    show_short_version();
    return EXIT_SUCCESS;
  }

  return None;
}

/* Report a break, continue, or return that reached the top with no loop,
   function, or sourced script to consume it. The jump carries the source and
   the origin it was made in, so the caret points at the exact builtin and the
   note names where it ran. */
static fn report_escaped_control_flow(EvalContext &context,
                                      const String &fallback_source) -> void
{
  if (!context.has_pending_control_flow()) return;

  const control_flow &control = context.pending_control_flow();
  let what = String{};
  switch (control.kind) {
  case control_flow::Kind::Break:
    what = "'break' used outside of a loop";
    break;
  case control_flow::Kind::Continue:
    what = "'continue' used outside of a loop";
    break;
  case control_flow::Kind::Return: {
    /* A return that reaches the top of a non-interactive script ends the shell
       with its status, the way dash treats a top-level return. It stays an
       error at an interactive prompt, where there is nothing to return from. */
    if (!context.shell_is_interactive()) {
      i32 return_status = static_cast<i32>(control.value);
      context.clear_control_flow();
      context.run_exit_trap();
      utils::quit(return_status, true);
    }
    what = "'return' used outside of a function or a sourced script";
    break;
  }
  case control_flow::Kind::Exit:
  case control_flow::Kind::Normal: context.clear_control_flow(); return;
  }

  const String *source =
      control.source != nullptr ? control.source : &fallback_source;
  let const located = ErrorWithLocation{control.location, what};
  show_message(located.to_string(*source));

  context.clear_control_flow();
}

/* Lex, parse, validate, and evaluate one chunk of shell source in the given
   context. The main loop and source_file share this so a sourced file runs the
   same pipeline as an interactive line. Returns the resulting exit code. */
static fn run_script_contents(const String &script_contents,
                              EvalContext &context, BumpArena &ast_arena,
                              Maybe<StringView> filename = None) -> int
{
  i32 exit_code = EXIT_FAILURE;

  try {
    defer { context.end_command(); };

    /* Reclaim the previous command's arena storage before the next parse, and
       destroy the eval and dot ASTs that point into it. Function bodies live in
       the separate function arena, so they survive this reset and a function
       defined on one command stays callable on the next. */
    context.clear_retained_sources();
    ast_arena.reset();
    context.reset_scratch_arena();

    let p = Parser{
        Lexer{String{script_contents.view()}, ast_arena,
              context.show_lexed_words(), filename,
              context.is_bash_compatible()}
    };

    /* Recover from each parse error so the whole file is reported at once. A
       file with any parse error must not run, so a non-empty error list prints
       every error and fails without evaluating the partial tree. */
    let parse_errors = ArrayList<shit::String>{heap_allocator()};
    Expression *ast = p.construct_ast(parse_errors);

    if (!parse_errors.is_empty()) {
      for (const shit::String &e : parse_errors)
        show_message(e);
      context.set_last_exit_status(EXIT_FAILURE);
      return EXIT_FAILURE;
    }

    if (context.show_ast()) {
      print(ast->to_ast_string());
      print("\n");
    }

    if (context.show_lexed_words()) {
      for (const auto &word : p.debug_words()) {
        print(word.to_pretty_string());
        print("\n");
      }
    }

    /* Validate the whole tree before running anything. An unconditional problem
       stops execution and a conditional one only warns. POSIX mode and bash
       mode both skip the whole stage, since neither dash nor bash runs a
       shellcheck pass, so the tree runs without validation or constant folding
       and the prompt reaches evaluation sooner. -W forces the stage back on in
       either mode and reports every error as a warning, and --no-diagnostics
       always skips it. The default mode keeps the analysis, so the diagnostics
       stay intact. */
    /* The compat decision reads the live context rather than the static mode
       helper, so init-as-bash, which runs the config in bash mode then turns
       bash mode off at the interactive seam, flips the analysis stage back on
       for the session. The context fields are seeded from the same helpers at
       startup, so a non-snapping run behaves exactly as before. */
    let const run_analysis =
        (!(context.is_bash_compatible() || context.is_posix_mode()) ||
         FLAG_WARNINGS.is_enabled()) &&
        !FLAG_SUPPRESS_DIAGNOSTICS.is_enabled();
    if (run_analysis &&
        !analyze_ast(ast, script_contents, context.function_names(),
                     context.alias_names(), FLAG_WARNINGS.is_enabled()))
    {
      exit_code = EXIT_FAILURE;
    } else if (context.no_exec()) {
      /* Under -n the tree is parsed and validated but never run. */
      exit_code = EXIT_SUCCESS;
    } else {
      context.set_current_source(&script_contents, "the script");
      /* Run, timing the wall clock so the \D prompt segment can show how long
         the last command took. */
      const auto command_start_ns = shit::os::monotonic_nanos();
      exit_code = static_cast<int>(ast->evaluate(context));
      context.set_last_command_duration_ns(shit::os::monotonic_nanos() -
                                           command_start_ns);
      /* A trapped signal delivered during the last command of the chunk has no
         following node to trigger its action, so the pending traps drain here
         before the chunk ends, the way dash runs a pending trap before it reads
         the next command or exits. */
      if (shit::os::SIGNAL_PENDING) context.run_pending_traps();
      report_escaped_control_flow(context, script_contents);
      /* script_contents is local to this call, so drop the frame before it goes
         out of scope and leaves a dangling pointer behind. */
      context.set_current_source(nullptr, "");
    }
    context.set_last_exit_status(static_cast<i32>(exit_code));

    if (context.show_exit_code())
      print("[Code " + utils::int_to_text(exit_code) + "]\n");

    if (context.stats_enabled()) {
      print(context.make_stats_string());
      print("\n");
    }
  } catch (const ErrorWithLocationAndDetails &e) {
    show_message(e.to_string(script_contents));
    show_message(e.details_to_string(script_contents));
  } catch (const ErrorWithLocation &e) {
    show_message(e.to_string(script_contents));
  } catch (const Error &e) {
    show_message(e.to_string());
  } catch (const std::exception &e) {
    show_message(
        "Uncaught exception while executing the AST. Aborting the command.");
    show_message("Last system message: '" + os::last_system_error_message() +
                 "'.");
    show_message("Context: '" + String{e.what()} + "'.");
  } catch (...) {
    show_message(
        "Unexpected system explosion while executing the AST. Exiting.");
    show_message("Last system message: " + os::last_system_error_message());
    utils::quit(EXIT_FAILURE);
  }

  return exit_code;
}

/* Read a whole file and run it in the given context. A missing file is not an
   error, since a login shell sources profiles that may not exist. Returns
   whether the file existed and ran, so a caller that wants the first existing
   of several candidates, the way bash picks one login profile, can stop after
   the first hit. */
static fn source_file(const Path &path, EvalContext &context,
                      BumpArena &ast_arena) -> bool
{
  Maybe<String> contents = utils::read_entire_file(path.text());
  if (!contents) return false;

  /* The profile path names the source, so a parse error in it and a backtrace
     caret for a file it sources both carry the file rather than a bare
     line:col. */
  run_script_contents(*contents, context, ast_arena, path.text().view());
  return true;
}

/* Source a file named under the home directory, such as .bashrc or .shitrc. A
   shell with no home directory or no such file sources nothing. */
static fn source_home_file(StringView name, EvalContext &context,
                           BumpArena &ast_arena) throws -> void
{
  if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
    Path path = home->clone();
    path.push_component(name);
    source_file(path, context, ast_arena);
  }
}

/* Source the dash login files in POSIX login order, /etc/profile then
   ~/.profile. The file named by ENV is an interactive feature read separately.
 */
static fn source_posix_login_files(EvalContext &context,
                                   BumpArena &ast_arena) throws -> void
{
  source_file(Path{"/etc/profile"}, context, ast_arena);
  source_home_file(".profile", context, ast_arena);
}

/* Source the bash login files in bash login order, /etc/profile then the first
   existing of ~/.bash_profile, ~/.bash_login, ~/.profile. A login shell in bash
   mode and an init-as-bash login shell both read this set. */
static fn source_bash_login_files(EvalContext &context,
                                  BumpArena &ast_arena) throws -> void
{
  source_file(Path{"/etc/profile"}, context, ast_arena);
  if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
    for (const char *name : {".bash_profile", ".bash_login", ".profile"}) {
      Path candidate = home->clone();
      candidate.push_component(name);
      if (source_file(candidate, context, ast_arena)) break;
    }
  }
}

} /* namespace shit */

fn main(int argc, char **argv) -> int
{
#if SHIT_PLATFORM_IS COSMO
  ShowCrashReports();
  unused(FLAG_COSMO_FTRACE);
  unused(FLAG_COSMO_STRACE);
#endif

  bool is_login_shell = false;
  let file_names = shit::ArrayList<shit::String>{};

  try {
    file_names = shit::parse_flags(FLAG_LIST, argc, argv);
  } catch (const shit::Error &e) {
    shit::show_message(e.to_string());
    /* A flag error is a usage error, so the shell exits with the POSIX usage
       status rather than success, matching dash. */
    return 2;
  }

  /* --dumb is the union of -P, -T, and --no-diagnostics, so it enables those
     three component flags once here and the rest of the startup reads them
     directly. It also turns color off the same as NO_COLOR set in the
     environment, so the prompt and the diagnostics stay plain on a dumb
     terminal. */
  if (FLAG_DUMB.is_enabled()) {
    if (!FLAG_POSIX_MODE.is_enabled()) FLAG_POSIX_MODE.toggle();
    if (!FLAG_NO_COMPLETION.is_enabled()) FLAG_NO_COMPLETION.toggle();
    if (!FLAG_SUPPRESS_DIAGNOSTICS.is_enabled())
      FLAG_SUPPRESS_DIAGNOSTICS.toggle();
    shit::os::set_environment_variable("NO_COLOR", "1");
  }

  /* Raise the runtime log level before any helper runs, so the trace covers
     startup. The default stays Warn, so a run without -X pays one comparison
     per LOG call and prints nothing. */
  if (FLAG_LOG.is_enabled()) shit::LOGGER_VERBOSITY = shit::verbosity::All;

  /* Program path is the first argument. Pull it out and get rid of it. */
  let program_path = shit::String{};

  if (file_names.count() > 0) {
    program_path = file_names[0];
    /* Drop the program path, the first element. The list has no erase, so the
       rest is rebuilt from the second element on. */
    let rest = shit::ArrayList<shit::String>{};
    for (usize i = 1; i < file_names.count(); i++)
      rest.push(shit::String{shit::heap_allocator(), file_names[i]});
    file_names = steal(rest);
  } else {
    program_path = "<unknown>";
  }

  /* A basename of sh or dash selects POSIX mode and a basename of bash selects
     bash mode, so a symlink that names the shell after a system shell behaves
     like that shell. Real bash run as sh switches to POSIX, which the split
     mirrors. */
  let const last_slash = program_path.find_last_character('/');
  shit::StringView program_basename =
      last_slash.has_value() ? program_path.substring(*last_slash + 1)
                             : program_path.view();
  /* A login shell receives argv[0] prefixed with a dash, such as -bash, so the
     leading dash is dropped before the name is matched, the way bash strips it
     to recognize its own invocation name. */
  if (!program_basename.is_empty() && program_basename[0] == '-')
    program_basename = program_basename.substring(1);
  shit::INVOKED_AS_POSIX_SHELL =
      program_basename == "sh" || program_basename == "dash";
  shit::INVOKED_AS_BASH = program_basename == "bash";

  if (shit::Maybe<int> code = shit::print_help_or_version_status(program_path))
    return *code;

  if (FLAG_LOGIN.is_enabled() || program_path == "-") is_login_shell = true;

  /* init-as-bash initializes from the bash config files in bash mode, then
     snaps to the default at the interactive prompt. The SHIT_INIT_AS_BASH
     environment variable enables it when set and not empty, the same as passing
     -L. */
  let init_as_bash = FLAG_INIT_AS_BASH.is_enabled();
  if (!init_as_bash) {
    if (shit::Maybe<shit::String> env =
            shit::os::get_environment_variable("SHIT_INIT_AS_BASH");
        env.has_value() && !env->is_empty())
      init_as_bash = true;
  }

  /* init-as-bash and POSIX mode contradict each other, one initializes as bash
     and the other forces the dash semantics. POSIX takes precedence, the way
     the shell resolves the other incompatible option pairs, and a warning names
     the fallback. */
  if (init_as_bash && shit::should_run_in_posix_mode()) {
    shit::show_message("Both '--init-as-bash' and POSIX mode were specified. "
                       "Falling back to POSIX mode.");
    init_as_bash = false;
  }

  /* A privileged shell skips every startup config file, so a profile or rc that
     a less-privileged user controls cannot run with the raised privileges. The
     -p flag forces it, and a setuid or setgid invocation turns it on by
     default. */
  let const is_privileged =
      FLAG_PRIVILEGED.is_enabled() || shit::os::is_running_setuid();

  /* Both stdin and interactive flags are enabled, but there will be only the
   * last man standing. */
  if (FLAG_STDIN.is_enabled() && FLAG_INTERACTIVE.is_enabled()) {
    bool is_tty = shit::os::is_stdin_a_tty();

    let s = shit::String{};
    s += "Both '-s' and '-i' options were specified. Falling back to ";
    s += is_tty ? "'-i'" : "'-s' because stdin is not a tty.";
    shit::show_message(s);

    if (is_tty)
      FLAG_STDIN.toggle();
    else
      FLAG_INTERACTIVE.toggle();
  }

  bool should_read_stdin = false, should_execute_commands = false,
       should_read_files = false, should_be_interactive = false;

  /* Figure out what to do. Note that "-c" can be specified multiple times.
   * Option precedence should behave as follows: "-s", then "-c", then files
   * (arguments), then "-i" (or no arguments). */
  if (FLAG_STDIN.is_enabled()) {
    /* Operands after -s are the positional parameters, so they are not
       incompatible. Only the command and interactive flags conflict with -s. */
    if (!FLAG_COMMAND.is_empty() || FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message(
          "Incompatible options or arguments were specified along "
          "with '-s' option. "
          "Falling back to '-s'.");
    }
    should_read_stdin = true;
  } else if (!FLAG_COMMAND.is_empty()) {
    /* Operands after the command string are not incompatible, since POSIX reads
       the first as $0 and the rest as the positional parameters. Only the
       interactive flag conflicts with -c. */
    if (FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message(
          "Incompatible options or arguments were specified along "
          "with '-c' options. "
          "Falling back to '-c'.");
    }
    should_execute_commands = true;
  } else if (!file_names.is_empty()) {
    if (FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message("Both file argument and '-i' option were given. "
                         "Falling back to reading files.");
    }
    should_read_files = true;
  } else {
    should_be_interactive = true;
  }

  /* Resolve $0 and the positional parameters $1 upward from the operands per
     POSIX, since the rule differs by invocation mode. When running a script
     file the first operand is the script path and becomes $0, while the rest
     are its arguments. Under -c the first operand names $0 and the rest are the
     arguments. An interactive or -s shell keeps the shell name as $0 and takes
     every operand as a positional parameter. The context owns both. */
  let shell_name = program_path.clone();
  let positional_params = shit::ArrayList<shit::String>{};

  usize first_param_index = 0;
  if (should_read_files && !file_names.is_empty()) {
    shell_name = file_names[0].clone();
    first_param_index = 1;
  } else if (should_execute_commands && !file_names.is_empty()) {
    shell_name = file_names[0].clone();
    first_param_index = 1;
  }

  positional_params.reserve(file_names.count() - first_param_index);
  for (usize i = first_param_index; i < file_names.count(); i++)
    positional_params.push(shit::String{
        shit::heap_allocator(),
        shit::StringView{file_names[i].data(), file_names[i].count()}
    });

  let context = shit::EvalContext{FLAG_DISABLE_EXPANSION.is_enabled(),
                                  FLAG_VERBOSE.is_enabled(),
                                  FLAG_EXPAND_VERBOSE.is_enabled(),
                                  should_be_interactive,
                                  FLAG_ERROR_EXIT.is_enabled(),
                                  shell_name.clone(),
                                  steal(positional_params)};

  /* quit is a free function with no context in scope, so it is handed a pointer
     to the one context to read the interactive state and the memory-report flag
     from, rather than mirroring them into globals. */
  shit::utils::set_quit_context(&context);

  /* Apply the remaining option flags that the constructor does not take. */
  context.set_stats_enabled(FLAG_STATS.is_enabled());
  context.set_show_ast(FLAG_AST.is_enabled());
  context.set_show_lexed_words(FLAG_ESCAPE_MAP.is_enabled());
  context.set_show_exit_code(FLAG_EXIT_CODE.is_enabled());
  context.set_memory_stats_enabled(FLAG_MEMORY.is_enabled());
  /* The startup config files, the profiles and the rc, source with nounset and
     pipefail off the way a script does, since they are written for a lax shell
     and read unset variables such as $BASH_VERSION on the /etc/profile path. The
     strict interactive defaults are applied at the seam below once the config
     has loaded, so a typo in a variable name and a failing pipeline stage both
     fail loudly at the prompt. An explicit set -u or set -o pipefail is honored
     throughout. */
  context.set_error_unset(FLAG_NOUNSET.is_enabled());
  context.set_pipefail(false);
  context.set_no_clobber(FLAG_NO_CLOBBER.is_enabled());
  context.set_export_all(FLAG_EXPORT_ALL.is_enabled());
  context.set_no_exec(FLAG_NO_EXEC.is_enabled());
  context.set_failglob(!shit::should_run_in_compat_mode());
  context.set_bash_compatible(shit::should_run_in_bash_mode());
  context.set_posix_mode(shit::should_run_in_posix_mode());
  /* Mimicry is mirrored onto the context, since the execution path in Utils
     reads it there rather than the static flag, which is internal to this file.
   */
  context.set_mimicry(FLAG_MIMICRY.is_enabled());
  /* init-as-bash runs the bash config files in bash mode, so the parser accepts
     their bash syntax and the analysis stage stays off while they source. The
     interactive seam below snaps this back off for the session itself. */
  if (init_as_bash) context.set_bash_compatible(true);
  /* Monitor mode is on by default in an interactive shell, the way job control
     is enabled at a prompt. */
  context.set_monitor(should_be_interactive);

  /* Seed the standard and shell-specific variables a script may read. The
     version and runtime values come from the build. */
  context.set_shell_variable("SHELL", program_path);
  context.set_shell_variable("PWD", shit::Path::current_directory().text());
  let version_string = shit::String{};
  version_string += shit::utils::int_to_text(SHIT_VER_MAJOR);
  version_string += ".";
  version_string += shit::utils::int_to_text(SHIT_VER_MINOR);
  version_string += ".";
  version_string += shit::utils::int_to_text(SHIT_VER_PATCH);
  version_string += "-" SHIT_VER_EXTRA;
  context.set_shell_variable("SHIT_VERSION", version_string);
  context.set_shell_variable("SHIT_COMMIT", SHIT_COMMIT_HASH);
  context.set_shell_variable("SHIT_BUILD_MODE", SHIT_BUILD_MODE);
  context.set_shell_variable("SHIT_OS", SHIT_OS_INFO);

  /* Shell identity, so a script that probes for its host shell finds a known
     name and takes a working branch rather than a fragile fallback. The mimicry
     run seeds the same set for the shell it mimics, so the seeding is shared. The
     shit version above stays present in every mode. */
  context.seed_shell_identity_variables(shit::should_run_in_bash_mode() ||
                                        init_as_bash);

  /* SHLVL counts shell nesting. It is read from the inherited environment,
     incremented, and exported so a child shell continues the count. */
  i64 shell_level = 0;
  if (shit::Maybe<shit::String> inherited =
          shit::os::get_environment_variable("SHLVL");
      inherited.has_value())
  {
    if (shit::ErrorOr<i64> parsed =
            shit::utils::parse_decimal_integer(inherited->view());
        !parsed.is_error() && parsed.value() > 0)
      shell_level = parsed.value();
  }
  /* A corrupt or absurd inherited level is clamped so the increment cannot
     overflow, the way bash bounds SHLVL to a sane range. */
  if (shell_level > 999) shell_level = 0;
  shit::os::set_environment_variable("SHLVL",
                                     shit::utils::int_to_text(shell_level + 1));
  /* SHLVL lives in the environment, so the exported set must know it even on a
     first shell that did not inherit one. */
  context.mark_exported("SHLVL");

  /* The default prompt lives in PS1 so it is visible and editable, unless the
     environment already supplies one. An interactive unset of PS1 still falls
     back to the same default when the prompt is built. */
  if (!shit::os::get_environment_variable("PS1").has_value())
    context.set_shell_variable("PS1", toiletline::default_prompt_template());

  bool should_quit = FLAG_ONE_COMMAND.is_enabled() ? true : false;
  i32 exit_code = EXIT_SUCCESS;

  /* Clear and set up cache. Don't prematurely initialize the whole path map,
   * since it's only really noticeable in interactive mode. This way,
   * subsequent calls to the same program will still be cached in any mode,
   * but we won't waste any milliseconds traversing directories for very
   * simple scripts! */
  shit::utils::clear_path_map();
  shit::os::set_default_signal_handlers();

  /* The parse arena holds the AST and its tokens for one command, and is reset
     between commands. It outlives each tree it builds. */
  let ast_arena = shit::BumpArena{};
  shit::AST_ARENA = &ast_arena;

  /* The function arena holds function bodies, which outlive the command that
     defined them, so it is never reset during the run. */
  let function_arena = shit::BumpArena{};
  shit::FUNCTION_ARENA = &function_arena;

  if (is_privileged) {
    /* A privileged shell sources nothing, the way bash's privileged mode leaves
       the profiles and rc files unread. */
  } else if (init_as_bash) {
    /* init-as-bash sources the bash config files in bash mode so an existing
       bash setup loads. With -l it reads /etc/profile then the first existing
       of
       ~/.bash_profile, ~/.bash_login, and ~/.profile, the bash login order. An
       interactive shell reads ~/.bashrc. The shit rc and the POSIX ENV are
       skipped, since the intent is to initialize from the bash files. */
    if (is_login_shell) source_bash_login_files(context, ast_arena);
    if (should_be_interactive) source_home_file(".bashrc", context, ast_arena);
  } else {
    /* A login shell reads the login files of the shell it emulates. Bash mode
       reads the bash login order, so --bash-compatible -l or a bash invocation
       gets the bash profiles. POSIX mode reads the strict dash login order,
       /etc/profile then ~/.profile, while the file named by ENV is an
       interactive feature read below rather than part of the login set. The
       default shit mode reads the same dash order and then ENV. A missing file
       is silently skipped. */
    if (is_login_shell && shit::should_run_in_bash_mode()) {
      source_bash_login_files(context, ast_arena);
    } else if (is_login_shell && shit::should_run_in_posix_mode()) {
      source_posix_login_files(context, ast_arena);
    } else if (is_login_shell) {
      source_posix_login_files(context, ast_arena);
      if (shit::Maybe<shit::String> env = context.get_variable_value("ENV");
          env.has_value() && !env->is_empty())
        source_file(shit::Path{env->view()}, context, ast_arena);
    }

    /* An interactive shell reads ~/.shitrc, the home for interactive config
       such as aliases, options, and the prompt. A login shell reads it too,
       after the profiles, so a setting lands in every interactive session. A
       missing file is silently skipped. */
    if (should_be_interactive) source_home_file(".shitrc", context, ast_arena);

    /* A compatibility mode reads the interactive rc its host shell would. Bash
       mode reads ~/.bashrc, and POSIX mode reads the file named by ENV whether
       or not the shell is a login one, since the POSIX login path above leaves
       ENV to here. The shit rc above runs first in the default mode. A missing
       file is silently skipped. */
    if (should_be_interactive && shit::should_run_in_bash_mode()) {
      source_home_file(".bashrc", context, ast_arena);
    } else if (should_be_interactive && shit::should_run_in_posix_mode()) {
      if (shit::Maybe<shit::String> env = context.get_variable_value("ENV");
          env.has_value() && !env->is_empty())
        source_file(shit::Path{env->view()}, context, ast_arena);
    }
  }

  /* The startup files have finished, so a command typed at the prompt may now
     retitle the terminal. */
  context.set_startup_finished();

  /* The startup config has loaded, so the strict interactive defaults take over
     for the session. An interactive shit-native shell turns nounset and pipefail
     on so a typo or a failing pipeline stage fails loudly at the prompt, while a
     compatibility mode keeps the lax bash or dash defaults. init-as-bash also
     leaves bash mode here, so the strict parser and the analysis stage take over
     from the first prompt. A non-interactive run has no prompt, so it keeps the
     lenient sourcing defaults for the whole run. */
  if (should_be_interactive) {
    if (init_as_bash) context.set_bash_compatible(false);
    let const strict = !shit::should_run_in_compat_mode();
    context.set_error_unset(FLAG_NOUNSET.is_enabled() || strict);
    context.set_pipefail(strict);
  }

  /* A simple return cannot be used after this point, since we need a special
   * cleanup for toiletline. utils::quit() should be used instead. */
  for (;;) {
    ASSERT(!shit::os::is_child_process());

    let script_contents = shit::String{};
    /* The named script file flows into the diagnostics so an error reads
       path:line:col. A command string, standard input, or an interactive line
       from the editor carries no path, so a prompt error stays a bare
       line:col. */
    shit::Maybe<shit::StringView> source_filename = shit::None;

    /* Figure out what to do and retrieve the code. */
    try {
      if (should_read_files || should_read_stdin) {
        /* The shell runs exactly one script, the first operand, with the rest
           of the operands as its positional parameters. If "-s" is used, or
           when that operand is "-", read standard input, otherwise read the
           named file, both through the descriptor layer so no iostream file
           stream is pulled in. */
        if (should_read_stdin || file_names[0] == "-") {
          script_contents = shit::utils::read_entire_standard_input();
        } else {
          const shit::String &file_name = file_names[0];
          shit::Maybe<shit::String> contents =
              shit::utils::read_entire_file(file_name.view());
          if (!contents) {
            throw shit::Error{"Could not open '" + file_name.view() +
                              "': " + shit::os::last_system_error_message()};
          }
          script_contents = steal(*contents);
          source_filename = file_name.view();
        }

        should_quit = true;
      } else if (should_execute_commands) {
        shit::StringView command_view = FLAG_COMMAND.next();
        script_contents = shit::String{command_view};
        if (FLAG_COMMAND.at_end()) should_quit = true;
      } else if (should_be_interactive) {
        if (!toiletline::is_active()) {
          shit::utils::initialize_path_map();
          toiletline::initialize();
          /* The line editor only completes at an interactive prompt, so the
             engine is registered here and never on the script or -c path. The
             -T flag leaves it unregistered, so the editor runs with no
             completion callback and no ghost-text. */
          if (!FLAG_NO_COMPLETION.is_enabled())
            toiletline::enable_completion(context);
          /* The no-completion flag also silences the ghost suggestion, so the
             history source does not keep offering one after completion is off.
           */
          toiletline::set_ghost_enabled(!FLAG_NO_COMPLETION.is_enabled());
          shit::show_message(
              init_as_bash                       ? "Bash me harder?"
              : shit::should_run_in_posix_mode() ? "POSIX me harder!"
              : shit::should_run_in_bash_mode()  ? "Bash me harder!"
                                                 : "Welcome :3");
        } else {
          /* NOTE: avoid this branch if exit_raw_mode() wasn't called
           * previosly! */
          toiletline::enter_raw_mode();
        }

        /* Report any background job that finished while the previous command
           ran, the way bash prints a Done line before the next prompt. This is
           the interactive branch, so a script never reaches it. */
        context.notify_done_jobs();

        shit::String prompt = toiletline::build_prompt(context);

        /* Ask for input until we get one. */
        for (;;) {
          let[code, input] = toiletline::get_input(prompt);

          switch (code) {
          case TL_PRESSED_TAB:
            /* The completion engine handles TAB inside the editor and returns
               TL_PRESSED_ENTER or TL_SUCCESS, so this fires only when there was
               nothing to complete. Re-feed the line and keep prompting on the
               same row rather than inserting a literal tab. */
            toiletline::set_input(input);
            continue;
          case TL_PRESSED_EOF:
            /* EOF logs out only on an empty line, the way dash and bash treat
               CTRL-D. On a non-empty line the EOF is ignored and the editor
               keeps the typed text, so the user can finish the command. */
            if (input.is_empty()) {
              shit::print("^D");
              shit::flush();
              toiletline::emit_newlines(input);
              shit::utils::quit(exit_code, true);
            } else {
              toiletline::set_input(input);
              continue;
            }
            break;
          case TL_PRESSED_INTERRUPT:
            /* Ignore Ctrl-C. */
            shit::print("^C");
            shit::flush();
            break;
          case TL_PRESSED_SUSPEND:
            /* Ignore Ctrl-Z. */
            shit::print("^Z");
            shit::flush();
            break;
          default:;
          }

          toiletline::emit_newlines(input);

          /* Execute the command without raw mode. */
          if (code == TL_PRESSED_ENTER && !input.is_empty()) {
            script_contents = steal(input);
            break;
          }
        }

        toiletline::exit_raw_mode();
      } else {
        unreachable();
      }
    } catch (const shit::Error &e) {
      shit::show_message(e.to_string());
      shit::utils::quit(EXIT_FAILURE);
    } catch (const std::exception &e) {
      shit::show_message(
          "Uncaught exception while getting the input. Exiting.");
      shit::show_message("Context: '" + shit::String{e.what()} + "'.");
      shit::utils::quit(EXIT_FAILURE);
    } catch (...) {
      shit::show_message(
          "Unexpected system explosion while getting the input. Exiting.");
      shit::show_message("Last system message: " +
                         shit::os::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

    /* Drop any interrupt that landed while the prompt was waiting, so a Ctrl-C
       used to clear the input line does not abort the command about to run. */
    shit::os::INTERRUPT_REQUESTED = 0;

    /* This is the final chunk to run when should_quit is set, so the shell
       exits with its status next. A terminal external command in it may replace
       the shell process rather than fork, exec, and wait, the way dash execs
       the last command under EV_EXIT. An interactive prompt keeps reading, and
       an EXIT trap must run before the shell ends, so both keep the fork. The
       exit-code and stats trailers print after the command runs, so the shell
       keeps the fork to regain control and emit them. The flag rides only the
       tail position from here, since the compound nodes clear it on every path
       but the terminal simple command. */
    const bool prints_post_run_trailer =
        context.show_exit_code() || context.stats_enabled();
    context.set_terminal_exec_allowed(
        should_quit && !context.shell_is_interactive() &&
        !context.has_exit_trap() && !prints_post_run_trailer);

    /* Execute the contents through the shared pipeline. */
    exit_code = run_script_contents(script_contents, context, ast_arena,
                                    source_filename);

    /* TODO: Make ExecutionErrorWithLocation to distinguish execution
     * errors? Or statically check commands before they are executed? */

    /* We can get here from child process if they didn't exec()
     * properly to print error. */
    if (should_quit || shit::os::is_child_process() ||
        (FLAG_ERROR_EXIT.is_enabled() && exit_code != 0))
    {
      if (!shit::os::is_child_process()) context.run_exit_trap();
      shit::utils::quit(exit_code, FLAG_ERROR_EXIT.is_enabled());
    }
  }

  unreachable();
}
