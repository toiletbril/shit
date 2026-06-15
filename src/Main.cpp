#include "Arena.hpp"
#include "Cli.hpp"
#include "Colors.hpp"
#include "Common.hpp"
#include "Completion.hpp"
#include "Debug.hpp"
#include "Diagnostics.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Shitbox.hpp"
#include "Toiletline.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <cstring>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-OPTIONS] [--] <file1> [file2, ...]", "[-OPTIONS] -",
                   "[-OPTIONS]");

FLAG(VERSION, Bool, '\0', "version", "Display program version and notices.");
FLAG(SHORT_VERSION, Bool, 'V', "short-version",
     "Display version in a short form.");
FLAG(HELP, Bool, '\0', "help", "Display help message.");

FLAG(INTERACTIVE, Bool, 'i', "interactive", Posix,
     "Specify that the shell is interactive.");
FLAG(STDIN, Bool, 's', "stdin", Posix, "Execute command from stdin and exit.");
FLAG(COMMAND, ManyStrings, 'c', "command", Posix,
     "Execute specified command and exit. Can be used multiple times.");
FLAG(ERROR_EXIT, Bool, 'e', "error-exit", Posix, "Die on first error.");
FLAG(DISABLE_EXPANSION, Bool, 'f', "no-glob", Posix, "Disable path expansion.");
FLAG(ONE_COMMAND, Bool, 't', "one-command", Posix,
     "Exit after executing one command.");
FLAG(VERBOSE, Bool, 'v', "verbose", Posix,
     "Write input to standard error as it is read.");
FLAG(EXPAND_VERBOSE, Bool, 'x', "xtrace", Posix,
     "Write expanded input to standard error as it is read.");
FLAG(EXPORT_ALL, Bool, 'a', "export-all", Posix,
     "Mark every assigned variable for the environment.");
FLAG(NO_CLOBBER, Bool, 'C', "no-clobber", Posix,
     "Refuse to overwrite an existing file through '>'.");
FLAG(NO_EXEC, Bool, 'n', "no-exec", Posix,
     "Read and parse commands but do not run them.");
FLAG(NOUNSET, Bool, 'u', "no-unset", Posix,
     "Treat an unset variable as an error.");
FLAG(LOGIN, Bool, 'l', "login", Posix,
     "Act as a login shell and source the profiles.");
FLAG(IGNORED1, Bool, 'h', "\0", Posix, "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", Posix, "Ignored, left for compatibility.");

FLAG(RCFILE, String, '\0', "rcfile", Bash,
     "Source FILE as the interactive rc instead of ~/.bashrc, the way bash "
     "reads "
     "a named rc. The shit rc still runs, and a non-interactive run reads no "
     "rc.");
FLAG(
    PRIVILEGED, Bool, 'p', "privileged", Bash,
    "Run in privileged mode and skip every startup config file, so a config a "
    "less-privileged user controls cannot run with raised privileges. Turned "
    "on automatically when the effective and the real user or group id differ, "
    "the setuid or setgid case.");

FLAG(MOOD, String, 'M', "mood", Compat,
     "Select the runtime mood, one of 'shit', 'bash', or 'sh'. The default "
     "'shit' mood is the strict bash superset with the analysis stage on, "
     "'bash' runs the bash extensions with the analysis stage off, and 'sh' "
     "behaves like dash. The mood drives strictness, the analysis stage, and "
     "the parser features, and set --mood changes it at runtime.");
FLAG(INIT_MOODS, ManyStrings, 'L', "init-moods", Compat,
     "Source the startup files for each listed mood, in order, given comma "
     "separated or by repeating the flag. 'shit' reads /etc/shitrc and "
     "~/.shitrc, 'bash' reads the bash rc and completion, and 'sh' reads the "
     "ENV file, with the login profiles added under -l. Defaults to the value "
     "of --mood.");
FLAG(
    INIT_AS_BASH, Bool, '\0', "init-as-bash", Compat,
    "Deprecated alias for --init-moods=bash. The SHIT_INIT_AS_BASH environment "
    "variable enables it when set.");
FLAG(
    MIMICRY, Bool, 'I', "mimicry", Compat,
    "Mimic the shell a script's shebang names, for speed. A program whose "
    "shebang is a shell shit can emulate runs in-process in the matching mode "
    "rather than launching the shell, so a script-heavy run skips the fork and "
    "the shell startup, where sh and dash run in POSIX mode, bash in bash "
    "mode, "
    "and shit in the default mode. A zsh, ksh, fish, or non-shell shebang "
    "still "
    "launches the real program.");
FLAG(DUMB, Bool, '\0', "dumb", Compat,
     "Makes shit extremely dumb. Equals to --mood sh -T --no-diagnostics.");

FLAG(WARNINGS, Bool, 'W', "force-warnings", Shit,
     "Keep the analysis stage but report every error as a warning and let the "
     "run proceed, instead of stopping on the first error.");
FLAG(LIST_CHECKS, Bool, '\0', "list-diagnostics", Shit,
     "List the shellcheck-style checks the analysis stage reports, then exit.");
FLAG(SUPPRESS_DIAGNOSTICS, Bool, '\0', "no-diagnostics", Shit,
     "Skip the analysis stage, so no warnings or pre-run diagnostics are "
     "reported and evaluation begins sooner.");
FLAG(
    SUPPRESS_INIT_DIAGNOSTICS, Bool, '\0', "no-init-diagnostics", Shit,
    "Suppress diagnostics and warnings only while the startup profiles and rc "
    "files source, then restore them for the prompt. Pairs with -W so a strict "
    "shell loads a lax bash config quietly yet keeps its checks afterward.");
FLAG(NO_COMPLETION, Bool, 'T', "no-completion", Shit,
     "Disable interactive tab completion and ghost-text.");
FLAG(ENABLE_SHITBOX, Bool, '\0', "enable-shitbox", Shit,
     "Resolve the bundled shitbox utility names such as ls and mkdir directly "
     "as commands, the same as set -o shitbox.");

FLAG(AST, Bool, 'A', "show-ast", Debug,
     "Print AST before executing each command.");
FLAG(DEBUG_OPTIMIZER, Bool, '\0', "debug-optimizer", Debug,
     "Trace the optimizer prepass decisions to standard error.");
FLAG(EXIT_CODE, Bool, 'E', "show-exit-code", Debug,
     "Print exit code after each executed command.");
FLAG(ESCAPE_MAP, Bool, 'R', "show-lexed-words", Debug,
     "Print escape bitmap after each parsed command.");
FLAG(STATS, Bool, '\0', "show-stats", Debug,
     "Print statistics after each executed command, including commands "
     "evaluated, expansions, nodes evaluated, and AST arena bytes with the run "
     "peak.");
FLAG(MEMORY, Bool, '\0', "show-memory", Debug,
     "Print a granular memory report at exit, the AST and function arena bytes "
     "with their reserved capacity and the malloc heap in use.");
/* The logging flags exist only in a debug build, the build whose LOG calls
   compile in. A release binary rejects -X as an unknown flag. */
#if !defined NDEBUG
FLAG(LOG, String, 'X', "debug-logging", Debug,
     "Enable internal logging at the given level, one of 'info', 'debug', or "
     "'all'. An unknown spelling is an error.");
FLAG(DEBUG_OUTPUT_FILE, String, '\0', "debug-logging-file", Debug,
     "Create the named file when missing and append the debug log to it "
     "instead of stderr, so an interactive session logs without painting "
     "over the prompt.");
FLAG(DEBUG_COMPLETE_AT, String, '\0', "debug-complete-at", Debug,
     "Print the completion candidates for the given line with the cursor at "
     "its end, one per line the way an explicit tab lists them, after every "
     "-c chunk has run, then exit. The completion test driver.");
#endif

#if SHIT_PLATFORM_IS COSMO
FLAG(COSMO_FTRACE, Bool, '\0', "ftrace", Debug,
     "Cosmopolitan: Trace functions.");
FLAG(COSMO_STRACE, Bool, '\0', "strace", Debug,
     "Cosmopolitan: Trace system calls.");
#endif

namespace shit {

fn shit_binary_flag_list() wontthrow -> const ArrayList<Flag *> &
{
  return FLAG_LIST;
}

#if !defined NDEBUG
/* The completion test driver behind --debug-complete-at. It lists the
   candidates for the given line with the cursor at its end, one per line the
   way an explicit tab would, and the run exits with its status. */
static fn run_debug_completion_driver(StringView driver_line,
                                      EvalContext &context) throws -> i32
{
  utils::initialize_path_map();
  let const driver_result =
      completion::complete(driver_line, driver_line.length, context,
                           Path::current_directory(), true);
  let listing = String{};
  for (let const &candidate : driver_result.candidates) {
    listing += candidate.view();
    listing += '\n';
  }
  print(listing);
  flush();
  return 0;
}
#endif

/* Set when the shell is invoked through a name whose basename is sh or dash,
   the way a system ln -s shit sh does. A script that names the shell after a
   system POSIX shell then runs compatibility-clean, the way bash run as sh
   switches to its POSIX mode. */
static bool INVOKED_AS_POSIX_SHELL = false;

/* Set when the basename is bash, so a script that names the shell bash gets the
   bash extensions the way real bash does. The invocation name splits here, sh
   and dash select POSIX mode while bash selects bash mode. */
static bool INVOKED_AS_BASH = false;

/* Parse a --mood value into a mood, with 'shit' the strict default, 'bash' the
   bash extensions, and 'sh' or 'posix' the dash semantics. An unknown spelling
   returns None so the caller reports the usage error. */
pure static fn parse_mood_name(StringView name) wontthrow -> Maybe<mimic_mood>
{
  if (name == "shit" || name == "default") {
    return mimic_mood::Default;
  }
  if (name == "bash") return mimic_mood::Bash;
  if (name == "sh" || name == "posix" || name == "dash") {
    return mimic_mood::Posix;
  }
  return None;
}

/* The runtime mood the session runs in, from --mood when given, then the
   invocation basename, then the strict default. --dumb forces the sh mood when
   --mood is absent. An invalid --mood value fails startup before this, so a
   stray one falls back to the default. */
pure static fn resolve_session_mood() wontthrow -> mimic_mood
{
  if (FLAG_MOOD.is_set()) {
    if (Maybe<mimic_mood> parsed_mood = parse_mood_name(FLAG_MOOD.value());
        parsed_mood.has_value())
    {
      return *parsed_mood;
    }
    return mimic_mood::Default;
  }
  if (FLAG_DUMB.is_enabled()) return mimic_mood::Posix;
  if (INVOKED_AS_POSIX_SHELL) return mimic_mood::Posix;
  if (INVOKED_AS_BASH) return mimic_mood::Bash;
  return mimic_mood::Default;
}

/* Print the help or version text and return the exit code when one of those
   flags is set, otherwise None so the shell proceeds to normal startup. */
static fn print_help_or_version_status(const String &program_path) -> Maybe<int>
{
  if (FLAG_HELP.is_enabled()) {
    let h = String{};
    h += "SHIT";
    h += "\n";
    h += wrap_text(
        "Shit, a pedantic, super-fast and awesome POSIX-compatible command "
        "line interpreter, or a friendly interactive shell for gigachads.\n\n",
        HELP_INDENT, HELP_WRAP_WIDTH);
    h += make_synopsis(program_path.view(), HELP_SYNOPSIS);
    h += '\n';
    h += wrap_text(
        "Options are also read from the SHIT_FLAGS environment variable, so a "
        "flag set there is inherited by every invocation, while a flag given "
        "on the command line still has the final say.\n\n",
        HELP_INDENT, HELP_WRAP_WIDTH);
    h += make_flag_help(FLAG_LIST);
    h += '\n';
    print_error(h);
    return EXIT_SUCCESS;
  }
  if (FLAG_LIST_CHECKS.is_enabled()) {
    let l = String{"SHELLCHECK CHECKS\n"};
    for (let const &check : SHELLCHECK_CHECKS) {
      l += "  ";
      l += check.code;
      l += "  ";
      l += check.summary;
      l += '\n';
    }
    l += "\nSTRICTNESS WARNINGS\n";
    for (let const &warning : STRICTNESS_WARNINGS) {
      l += "  ";
      l += warning.name;
      l += '\n';
      l += wrap_text(warning.summary, HELP_INDENT + 4, HELP_WRAP_WIDTH);
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
                              Maybe<StringView> filename = None,
                              Expression *precompiled_ast = nullptr,
                              Expression **out_ast = nullptr) -> int
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

    /* A precompiled tree, the cached PROMPT_COMMAND hook, skips the lex and the
       parse and the analysis below, since those already ran when it was first
       seen. The tree lives in a caller-owned arena that outlives this call, so
       it is reused across prompts rather than rebuilt each one. */
    Expression *ast = precompiled_ast;
    if (precompiled_ast == nullptr) {
      LOG(Debug, "parsing a chunk of %zu bytes", script_contents.count());

      let p = Parser{
          Lexer{String{script_contents.view()}, ast_arena,
                context.show_lexed_words(), filename, context.mood()}
      };

      /* Recover from each parse error so the whole file is reported at once. A
         file with any parse error must not run, so a non-empty error list
         prints every error and fails without evaluating the partial tree. */
      let parse_errors = ArrayList<shit::String>{heap_allocator()};
      ast = p.construct_ast(parse_errors);

      if (!parse_errors.is_empty()) {
        for (let const &e : parse_errors)
          show_message(e);
        context.set_last_exit_status(EXIT_FAILURE);
        return EXIT_FAILURE;
      }

      if (context.show_ast()) {
        print(ast->to_ast_string());
        print("\n");
      }

      if (context.show_lexed_words()) {
        for (let const &word : p.debug_words()) {
          print(word.to_pretty_string());
          print("\n");
        }
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
    /* The skip reads the context's diagnostics flag rather than only the
       static one, so set -o no-diagnostics flips the stage at runtime. */
    /* --debug-optimizer forces the prepass to run whatever the mood, since its
       whole purpose is to trace the prepass, so an interactive session that
       settled into bash mood through its rc still shows the optimizer trace. */
    let const run_analysis =
        precompiled_ast == nullptr &&
        (FLAG_DEBUG_OPTIMIZER.is_enabled() ||
         ((!(context.is_bash_compatible() || context.is_posix_mode()) ||
           FLAG_WARNINGS.is_enabled()) &&
          !context.diagnostics_disabled()));
    LOG(Debug, "the analysis stage %s for this chunk",
        run_analysis ? "runs" : "is skipped");
    /* An interactive -W chunk runs right away and the runtime resolution
       reports a missing command itself, so the analysis copy of that report
       stays quiet to avoid the doubled error at the prompt. */
    /* The live shell is handed to the prepass so the no-local check can
       query, only when it is about to warn, whether the name is already a
       shell or environment variable, an update rather than a leak. The
       query is lazy, so the analysis pays nothing per chunk for it. */
    let const analysis_failed =
        run_analysis &&
        !analyze_ast(
            ast, script_contents, context.function_names(),
            context.alias_names(), &context, FLAG_WARNINGS.is_enabled(),
            FLAG_WARNINGS.is_enabled() && context.shell_is_interactive(),
            FLAG_DEBUG_OPTIMIZER.is_enabled());
    /* A freshly parsed tree that parses and analyzes clean is handed back so a
       caller that wants to reuse it, the PROMPT_COMMAND hook, caches it. */
    if (!analysis_failed && out_ast != nullptr) {
      *out_ast = ast;
    }

    if (analysis_failed) {
      exit_code = EXIT_FAILURE;
    } else if (context.no_exec()) {
      /* Under -n the tree is parsed and validated but never run. */
      exit_code = EXIT_SUCCESS;
    } else {
      LOG(Debug, "evaluating the chunk");
      context.set_current_source(&script_contents, "the script");
      /* Run, timing the wall clock so the \D prompt segment can show how long
         the last command took. */
      const auto command_start_ns = shit::os::monotonic_nanos();
      exit_code = static_cast<int>(ast->evaluate(context));
      context.set_last_command_duration_ns(shit::os::monotonic_nanos() -
                                           command_start_ns);
      LOG(Debug, "the chunk finished with exit code %d", exit_code);
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
    /* An error thrown from a function body was rendered at the call boundary
       against the file that defined it, so it is not printed again here. */
    if (!e.was_rendered()) {
      show_message(e.to_string(script_contents));
      show_message(e.details_to_string(script_contents));
    }
    /* POSIX mode follows dash, which exits 2 on a fatal expansion or runtime
       error such as a set -u unset reference, while bash and the default mode
       keep the status-1 convention. */
    exit_code = context.is_posix_mode() ? 2 : EXIT_FAILURE;
  } catch (const ErrorWithLocation &e) {
    if (!e.was_rendered()) show_message(e.to_string(script_contents));
    /* bash exits 127 on the script-fatal expansion aborts, the set -u read
       and the ${name:?} report, while POSIX follows dash with 2. */
    exit_code = context.is_posix_mode() ? 2
                : context.is_bash_compatible() && e.is_script_fatal()
                    ? 127
                    : EXIT_FAILURE;
  } catch (const Error &e) {
    show_message(e.to_string());
    exit_code = context.is_posix_mode() ? 2
                : context.is_bash_compatible() && e.is_script_fatal()
                    ? 127
                    : EXIT_FAILURE;
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

/* Run the PROMPT_COMMAND hook before a primary prompt is drawn, the bash
   mechanism a prompt framework such as starship uses to recompute PS1 on every
   prompt. The hook reads the exit status of the last command in $?, so the
   saved status and the measured command duration are restored after it runs,
   which keeps the prompt escapes and the next command reading the real values
   rather than the ones the hook left behind. An empty or unset PROMPT_COMMAND
   runs nothing. */
/* The PROMPT_COMMAND text and the tree parsed from it, kept across prompts so a
   hook whose text does not change parses once rather than every prompt. The
   tree lives in its own arena, which the general per-command arena reset never
   touches, so the cached pointer stays valid between prompts. */
static BumpArena PROMPT_COMMAND_ARENA{};
static String PROMPT_COMMAND_CACHED_TEXT{};
static Expression *PROMPT_COMMAND_CACHED_AST = nullptr;

static fn run_prompt_command(EvalContext &context, BumpArena &ast_arena) -> void
{
  Maybe<String> command = context.get_variable_value("PROMPT_COMMAND");
  if (!command.has_value() || command->is_empty()) {
    return;
  }

  LOG(Info, "running the PROMPT_COMMAND hook, %zu bytes", command->count());

  const i32 saved_exit_status = context.last_exit_status();
  const u64 saved_command_duration_ns = context.last_command_duration_ns();

  if (PROMPT_COMMAND_CACHED_AST != nullptr &&
      PROMPT_COMMAND_CACHED_TEXT.view() == command->view())
  {
    /* The text is unchanged, so the cached tree runs against its own retained
       text rather than reparsing the hook. */
    run_script_contents(PROMPT_COMMAND_CACHED_TEXT, context, ast_arena,
                        StringView{"PROMPT_COMMAND"},
                        PROMPT_COMMAND_CACHED_AST);
  } else {
    /* A new or changed hook parses once into the prompt arena, which is reset
       first so the previous hook's tree is reclaimed. The freshly parsed tree
       comes back through out_ast and caches for the next prompt. */
    PROMPT_COMMAND_ARENA.reset();
    PROMPT_COMMAND_CACHED_AST = nullptr;
    PROMPT_COMMAND_CACHED_TEXT = String{command->view()};
    run_script_contents(PROMPT_COMMAND_CACHED_TEXT, context,
                        PROMPT_COMMAND_ARENA, StringView{"PROMPT_COMMAND"},
                        nullptr, &PROMPT_COMMAND_CACHED_AST);
  }

  context.set_last_exit_status(saved_exit_status);
  context.set_last_command_duration_ns(saved_command_duration_ns);
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
  if (!contents) {
    LOG(Info, "skipping '%s' because the file is missing or unreadable",
        path.c_str());
    return false;
  }

  LOG(Info, "sourcing '%s', %zu bytes", path.c_str(), contents->count());

  /* The file runs through run_source, the same path the dot builtin uses, so it
     parses into the active arena alongside the tree already being evaluated
     rather than resetting it. A set --init-moods inside a sourced rc reaches
     here while that rc's own tree is live, so resetting the arena would free
     the node mid-walk. run_source also bounds the nesting and retains the AST.
     The path names the source, so a parse error in it and a backtrace caret
     carry the file rather than a bare line:col. */
  unused(ast_arena);
  context.run_source(*contents, path.text().view(), /*consume_return=*/true,
                     /*call_site=*/None, path.text().view());
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
  LOG(Info, "sourcing the posix login files");
  source_file(Path{"/etc/profile"}, context, ast_arena);
  source_home_file(".profile", context, ast_arena);
}

/* Source the bash login files in bash login order, /etc/profile then the first
   existing of ~/.bash_profile, ~/.bash_login, ~/.profile. A login shell in bash
   mode and an init-as-bash login shell both read this set. */
static fn source_bash_login_files(EvalContext &context,
                                  BumpArena &ast_arena) throws -> void
{
  LOG(Info, "sourcing the bash login files in bash order");
  source_file(Path{"/etc/profile"}, context, ast_arena);
  if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
    for (let const name : {".bash_profile", ".bash_login", ".profile"}) {
      Path candidate = home->clone();
      candidate.push_component(name);
      if (source_file(candidate, context, ast_arena)) break;
    }
  }
}

/* Source the system bashrc the way bash compiled with SYS_BASHRC reads it for
   an interactive shell, the Void /etc/bash/bashrc or the Debian
   /etc/bash.bashrc, whichever exists first. The system rc is what loads
   bash-completion on most hosts, so skipping it leaves the programmable
   completion unregistered. */
static fn source_bash_system_rc(EvalContext &context,
                                BumpArena &ast_arena) throws -> void
{
  LOG(Info, "looking for the system bashrc");
  for (let const path : {"/etc/bash/bashrc", "/etc/bash.bashrc"})
    if (source_file(Path{path}, context, ast_arena)) break;
}

/* bash-completion registers the complete -D dynamic loader when it loads. A
   host whose rc chain never sources it, a stripped /etc/bash or a bare
   ~/.bashrc, still gets the programmable completion under -L and in bash mode
   by sourcing the stock script directly. The probe reads the default spec and
   the script's own guard variable, so a chain that already loaded it does not
   load it twice. */
static fn ensure_bash_completion_loaded(EvalContext &context,
                                        BumpArena &ast_arena) throws -> void
{
  if (context.default_completion_spec() != nullptr) {
    LOG(Info, "skipping the bash-completion bootstrap because a "
              "default completion spec is already registered");
    return;
  }
  if (context.get_variable_value("BASH_COMPLETION_VERSINFO").has_value()) {
    LOG(Info, "skipping the bash-completion bootstrap because the "
              "rc chain already loaded the script");
    return;
  }
  LOG(Info, "sourcing the stock bash-completion script");
  source_file(Path{"/usr/share/bash-completion/bash_completion"}, context,
              ast_arena);
}

fn source_init_moods(EvalContext &context, BumpArena &ast_arena,
                     const ArrayList<mimic_mood> &moods, bool is_login_shell,
                     bool should_be_interactive) throws -> void
{
  /* Each flavor sources under its own mood, so a bash rc parses with the bash
     grammar and a posix profile with the dash grammar. The caller restores the
     session mood afterward. A missing file is silently skipped. */
  bool did_source_bash_rc = false;
  for (let flavor : moods) {
    /* A flavor already on the sourcing stack is skipped, so a set --init-moods
       inside the very ~/.shitrc this is sourcing cannot re-source it and
       recurse until the stack overflows. The bit clears when the flavor
       finishes, even on a throw. */
    if (context.init_mood_sourcing(flavor)) {
      LOG(Info,
          "skipping the %s flavor, its startup files are already sourcing",
          flavor == mimic_mood::Bash    ? "bash"
          : flavor == mimic_mood::Posix ? "posix"
                                        : "shit");
      continue;
    }
    context.set_init_mood_sourcing(flavor, true);
    defer { context.set_init_mood_sourcing(flavor, false); };
    context.set_mood(flavor);
    LOG(Info, "sourcing the startup files for the %s flavor",
        flavor == mimic_mood::Bash    ? "bash"
        : flavor == mimic_mood::Posix ? "posix"
                                      : "shit");
    switch (flavor) {
    case mimic_mood::Default:
      /* The shit flavor reads the dash login profiles, then the system and the
         home shit rc for an interactive shell. */
      if (is_login_shell) source_posix_login_files(context, ast_arena);
      if (should_be_interactive) {
        source_file(Path{"/etc/shitrc"}, context, ast_arena);
        source_home_file(".shitrc", context, ast_arena);
      }
      break;
    case mimic_mood::Posix:
      /* The posix flavor reads the dash login order, then the file named by ENV
         for an interactive shell. */
      if (is_login_shell) source_posix_login_files(context, ast_arena);
      if (should_be_interactive) {
        if (Maybe<String> env = context.get_variable_value("ENV");
            env.has_value() && !env->is_empty())
          source_file(Path{env->view()}, context, ast_arena);
      }
      break;
    case mimic_mood::Bash:
      /* The bash flavor reads the bash login order, then the system rc and the
         user rc, the latter replaced by --rcfile. bash runs the system rc
         before the user one even under --rcfile, so the order mirrors that. */
      if (is_login_shell) source_bash_login_files(context, ast_arena);
      if (should_be_interactive) {
        did_source_bash_rc = true;
        source_bash_system_rc(context, ast_arena);
        if (FLAG_RCFILE.is_set())
          source_file(Path{FLAG_RCFILE.value()}, context, ast_arena);
        else
          source_home_file(".bashrc", context, ast_arena);
      }
      break;
    }
    /* A flavor counts as initialized only when it actually sourced a file, so
       the set --init-moods readout reports what loaded rather than every flavor
       the resolver listed for a non-interactive run that sourced nothing. */
    if (is_login_shell || should_be_interactive) {
      context.mark_mood_initialized(flavor);
    }
  }

  /* The bash programmable completion loads once after a bash rc sourced, so the
     script parses under the bash grammar and its specs survive into the
     session. */
  if (did_source_bash_rc) {
    LOG(Info, "bootstrapping the bash programmable completion");
    ensure_bash_completion_loaded(context, ast_arena);
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

  /* SHIT_FLAGS supplies command line options through the environment, such as
     SHIT_FLAGS='-ahmu --bash-compatible -I', so a user sets the shell's
     defaults once rather than on every invocation. The tokens are split on
     whitespace and spliced in right after the program name, before the real
     arguments, so a flag given on the command line still has the final say. The
     token strings and the spliced pointer array outlive the parse below, so the
     views stay valid. */
  shit::ArrayList<shit::String> shit_flags_tokens{};
  shit::ArrayList<const char *> spliced_argv{};
  if (shit::Maybe<shit::String> shit_flags =
          shit::os::get_environment_variable("SHIT_FLAGS");
      shit_flags.has_value() && !shit_flags->is_empty())
  {
    let const view = shit_flags->view();
    usize token_start = 0;
    /* A -c in SHIT_FLAGS would splice a command string into every invocation,
       which the variable is not for, so the -c token and the command word
       after it are dropped while a real command-line -c stays untouched. */
    bool should_skip_next_command_word = false;

    for (usize i = 0; i <= view.length; i++) {
      let const c = i < view.length ? view[i] : ' ';
      if (c == ' ' || c == '\t' || c == '\n') {
        if (i > token_start) {
          let const token =
              view.substring_of_length(token_start, i - token_start);
          if (should_skip_next_command_word) {
            should_skip_next_command_word = false;
          } else if (token == "-c") {
            should_skip_next_command_word = true;
          } else {
            shit_flags_tokens.push(shit::String{token});
          }
        }
        token_start = i + 1;
      }
    }
  }

  if (!shit_flags_tokens.is_empty() && argc > 0) {
    spliced_argv.push(argv[0]);
    for (let const &token : shit_flags_tokens)
      spliced_argv.push(token.c_str());
    for (int i = 1; i < argc; i++)
      spliced_argv.push(argv[i]);
  }

  const char *const *parse_argv =
      spliced_argv.is_empty() ? argv : spliced_argv.begin();
  let const parse_argc =
      spliced_argv.is_empty() ? argc : static_cast<int>(spliced_argv.count());

  /* A terminal that launches the shell with a broken flag config, such as a
     removed flag left in SHIT_FLAGS, must not exit and lock the user out of the
     pane. When standard input is a terminal the shell drops to a rescue prompt
     on default settings instead, so the config can be fixed from inside. The
     rc chain is skipped in rescue so a broken rc does not compound the failure.
     A non-interactive run keeps the usage exit the way dash does. */
  bool is_rescue_mode = false;
  let const do_enter_rescue = [&]() {
    shit::show_message("Entering rescue.");
    is_rescue_mode = true;
    shit::reset_flags(FLAG_LIST);
    try {
      file_names = shit::parse_flags(FLAG_LIST, argc, argv);
    } catch (...) {
      /* The real argv carried the bad flag too, so even the clean reparse
         fails. The program name is kept as the sole operand the way the success
         path keeps argv[0], so $0 and SHELL stay the real name rather than
         degrading to the unknown-program placeholder. */
      shit::reset_flags(FLAG_LIST);
      file_names = shit::ArrayList<shit::String>{};
      if (argc > 0) file_names.push(shit::String{argv[0]});
    }
  };

  try {
    file_names = shit::parse_flags(FLAG_LIST, parse_argc, parse_argv);
  } catch (const shit::ErrorWithLocation &e) {
    /* An unknown flag carries a caret into the joined command line, so the
       reader sees which argument the parser rejected. */
    shit::show_message(
        e.to_string(shit::join_command_line(parse_argc, parse_argv)));
    if (!(shit::os::is_stdin_a_tty() || shit::os::is_stdout_a_tty())) {
      return 2;
    }
    do_enter_rescue();
  } catch (const shit::Error &e) {
    shit::show_message(e.to_string());
    /* A flag error is a usage error, so a non-interactive shell exits with the
       POSIX usage status rather than success, matching dash. */
    if (!(shit::os::is_stdin_a_tty() || shit::os::is_stdout_a_tty())) {
      return 2;
    }
    do_enter_rescue();
  }

  /* --dumb is the union of -P, -T, and --no-diagnostics, so it enables those
     three component flags once here and the rest of the startup reads them
     directly. It also turns color off the same as NO_COLOR set in the
     environment, so the prompt and the diagnostics stay plain on a dumb
     terminal. */
  if (FLAG_DUMB.is_enabled()) {
    /* The sh mood is selected by resolve_session_mood when --dumb is set, so
       the block only turns off completion and diagnostics and forces plain
       output. */
    if (!FLAG_NO_COMPLETION.is_enabled()) FLAG_NO_COMPLETION.toggle();
    if (!FLAG_SUPPRESS_DIAGNOSTICS.is_enabled())
      FLAG_SUPPRESS_DIAGNOSTICS.toggle();
    shit::os::set_environment_variable("NO_COLOR", "1");
  }

  /* Raise the runtime log level before any helper runs, so the trace covers
     startup. The default stays Nothing, so a run without -X pays one
     comparison per LOG call and prints nothing. An unknown level spelling is
     a usage error, the way an unknown flag is. A release build compiled the
     flag and every LOG call out. */
#if !defined NDEBUG
  if (FLAG_LOG.is_set()) {
    struct log_level_name
    {
      const char *name;
      shit::verbosity level;
    };
    static const log_level_name LOG_LEVEL_NAMES[] = {
        {"info",  shit::verbosity::Info },
        {"debug", shit::verbosity::Debug},
        {"all",   shit::verbosity::All  },
    };
    let is_known_level = false;
    for (let const &entry : LOG_LEVEL_NAMES)
      if (FLAG_LOG.value() == entry.name) {
        shit::LOGGER_VERBOSITY = entry.level;
        is_known_level = true;
        break;
      }
    if (!is_known_level) {
      shit::show_message(shit::Error{"Unknown debug logging level '" +
                                     shit::String{FLAG_LOG.value()} +
                                     "', expected 'info', 'debug', or 'all'"}
                             .to_string());
      return 2;
    }
  }

  /* The log sink opens before anything logs, in append mode so consecutive
     runs accumulate into one trace a tail -f can follow. A file that cannot
     open leaves the sink on stderr rather than dropping the trace. */
  if (FLAG_DEBUG_OUTPUT_FILE.is_set() &&
      !FLAG_DEBUG_OUTPUT_FILE.value().is_empty())
  {
    let const log_file_name = shit::String{FLAG_DEBUG_OUTPUT_FILE.value()};
    if (std::FILE *log_file = std::fopen(log_file_name.c_str(), "a");
        log_file != nullptr)
    {
      shit::LOGGER_OUTPUT = log_file;
    }
  }
#endif

  let program_path = shit::String{};

  if (file_names.count() > 0) {
    /* The program path is the first argument. Move it out, then drop that slot
       so the operands behind it shift to the front, with no copy of either the
       path or the operands. */
    program_path = steal(file_names[0]);
    file_names.remove(0);
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
  /* A login shell receives argv[0] prefixed with a dash, such as -bash. The
     dash is inspected once here, dropped before the name is matched the way
     bash strips it to recognize its own invocation name, and read again
     below as the login mark. $0 keeps the dashed spelling. */
  const bool does_name_mark_login =
      !program_basename.is_empty() && program_basename[0] == '-';
  if (does_name_mark_login) program_basename = program_basename.substring(1);
  shit::INVOKED_AS_POSIX_SHELL =
      program_basename == "sh" || program_basename == "dash";
  shit::INVOKED_AS_BASH = program_basename == "bash";
  LOG(Info, "invocation basename is '%.*s'",
      static_cast<int>(program_basename.length), program_basename.data);
  LOG(Info, "selecting the %s mood",
      shit::resolve_session_mood() == shit::mimic_mood::Posix  ? "posix"
      : shit::resolve_session_mood() == shit::mimic_mood::Bash ? "bash"
                                                               : "default");

  if (shit::Maybe<int> code = shit::print_help_or_version_status(program_path))
    return *code;

  /* A dash-prefixed invocation name, -bash or a bare -, is the login spawn
     convention tmux and login use, the same mark the -l flag sets. */
  if (FLAG_LOGIN.is_enabled() || does_name_mark_login) {
    is_login_shell = true;
  }
  LOG(Info, "the shell %s a login shell", is_login_shell ? "is" : "is not");

  /* The runtime mood and the startup-file moods. --mood selects the session
     mood the shell runs in, while --init-moods lists which moods' startup files
     to source, in order, and defaults to the session mood. An invalid spelling
     in either is a usage error the way an unknown flag is. */
  if (FLAG_MOOD.is_set() && !shit::parse_mood_name(FLAG_MOOD.value())) {
    /* The bad value is rendered with a caret, the located form the rest of the
       diagnostics use, against a one-line source built from the flag. */
    shit::String source = "--mood ";
    let const value_position = source.count();
    source += FLAG_MOOD.value();
    shit::show_message(shit::ErrorWithLocation{
        shit::SourceLocation{value_position, FLAG_MOOD.value().length},
        "Unknown --mood value, expected one of 'shit', 'bash', or 'sh'"
    }
                           .to_string(source.view()));
    return 2;
  }
  let const session_mood = shit::resolve_session_mood();

  let init_moods = shit::ArrayList<shit::mimic_mood>{};
  for (usize i = 0; i < FLAG_INIT_MOODS.count(); i++) {
    shit::StringView entry = FLAG_INIT_MOODS.get(i);
    /* A single --init-moods value may itself be a comma-separated list, so each
       comma-separated name is parsed in turn. */
    usize name_start = 0;
    for (usize j = 0; j <= entry.length; j++) {
      if (j != entry.length && entry[j] != ',') {
        continue;
      }
      shit::StringView name =
          entry.substring_of_length(name_start, j - name_start);
      name_start = j + 1;
      if (name.is_empty()) continue;
      shit::Maybe<shit::mimic_mood> parsed_mood = shit::parse_mood_name(name);
      if (!parsed_mood.has_value()) {
        shit::String source = "--init-moods ";
        let const value_position = source.count();
        source += name;
        shit::show_message(shit::ErrorWithLocation{
            shit::SourceLocation{value_position, name.length},
            "Unknown --init-moods value, expected one of 'shit', 'bash', "
            "or 'sh'"
        }
                               .to_string(source.view()));
        return 2;
      }
      init_moods.push(*parsed_mood);
    }
  }

  /* init-as-bash is the deprecated alias that adds the bash startup files. The
     SHIT_INIT_AS_BASH environment variable enables it when set and not empty.
   */
  bool should_init_as_bash = FLAG_INIT_AS_BASH.is_enabled();
  if (!should_init_as_bash) {
    if (shit::Maybe<shit::String> env =
            shit::os::get_environment_variable("SHIT_INIT_AS_BASH");
        env.has_value() && !env->is_empty())
      should_init_as_bash = true;
  }

  /* With no explicit --init-moods the session mood's files source. */
  if (init_moods.is_empty()) init_moods.push(session_mood);

  /* The bash alias appends bash when the list does not already carry it. */
  if (should_init_as_bash) {
    bool has_bash = false;
    for (let listed : init_moods)
      if (listed == shit::mimic_mood::Bash) has_bash = true;
    if (!has_bash) init_moods.push(shit::mimic_mood::Bash);
  }

  /* A privileged shell skips every startup config file, so a profile or rc that
     a less-privileged user controls cannot run with the raised privileges. The
     -p flag forces it, and a setuid or setgid invocation turns it on by
     default. */
  let const is_privileged =
      FLAG_PRIVILEGED.is_enabled() || shit::os::is_running_setuid();
  LOG(Info, "privileged mode is %s", is_privileged ? "on" : "off");

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
#if !defined NDEBUG
  /* The completion test driver never prompts, so a bare driver run with no
     other input reads the empty standard input and exits through the
     driver's hook rather than initializing the editor. */
  if (FLAG_DEBUG_COMPLETE_AT.is_set() && should_be_interactive) {
    should_be_interactive = false;
    should_read_stdin = true;
  }
#endif
  LOG(Info, "the input source is %s",
      should_read_stdin         ? "standard input"
      : should_execute_commands ? "the -c command strings"
      : should_read_files       ? "the named script file"
                                : "the interactive prompt");

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
  context.set_diagnostics_disabled(FLAG_SUPPRESS_DIAGNOSTICS.is_enabled());
  /* The startup facts mirror onto the context once, so set -o lists them
     read-only next to the live options. */
  context.set_login_shell(is_login_shell);
  context.set_custom_rcfile(FLAG_RCFILE.is_set());
  /* The session runs in the resolved mood. The startup files source with
     strictness off, since they are written for a lax shell and read unset
     variables such as $BASH_VERSION on the /etc/profile path, and the chain
     swaps the mood per flavor so a bash rc parses under the bash grammar. The
     strictness for the session mood is applied at the seam below once the
     config has loaded, so a default-mood prompt fails loudly on a typo or a
     failing pipeline stage. A non-interactive run sources nothing, so the seam
     still runs and seeds its strictness from the mood. */
  context.set_mood(session_mood);
  /* The CLI -u is the user's own ask the way set -u is, so the -W downgrade
     leaves it fatal and the mood seam keeps it on. -W mirrors onto the context
     so the runtime strictness checks downgrade and set -W can flip it mid-run.
   */
  context.set_error_unset(FLAG_NOUNSET.is_enabled());
  if (FLAG_NOUNSET.is_enabled()) context.set_error_unset_explicit(true);
  context.set_warnings_enabled(FLAG_WARNINGS.is_enabled());
  context.set_pipefail(false);
  context.set_no_clobber(FLAG_NO_CLOBBER.is_enabled());
  context.set_export_all(FLAG_EXPORT_ALL.is_enabled());
  context.set_no_exec(FLAG_NO_EXEC.is_enabled());
  /* The --enable-shitbox flag turns the bundled utility names into commands for
     the whole session, the same state set -o shitbox toggles at run time. */
  context.set_shitbox(FLAG_ENABLE_SHITBOX.is_enabled());
  context.set_failglob(false);
  /* Mimicry is mirrored onto the context, since the execution path in Utils
     reads it there rather than the static flag, which is internal to this file.
   */
  context.set_mimicry(FLAG_MIMICRY.is_enabled());
  /* Monitor mode is on by default in an interactive shell, the way job control
     is enabled at a prompt. */
  context.set_monitor(should_be_interactive);

  /* Seed the standard and shell-specific variables a script may read. The
     version and runtime values come from the build. */
  context.set_shell_variable("SHELL", program_path);
  context.set_shell_variable("PWD", shit::Path::current_directory().text());
  context.set_shell_variable("SHIT_VERSION", SHIT_VERSION_STRING);
  context.set_shell_variable("SHIT_COMMIT", SHIT_COMMIT_HASH);
  context.set_shell_variable("SHIT_BUILD_MODE", SHIT_BUILD_MODE);
  context.set_shell_variable("SHIT_OS", SHIT_OS_INFO);

  /* Shell identity, so a script that probes for its host shell finds a known
     name and takes a working branch rather than a fragile fallback. The mimicry
     run seeds the same set for the shell it mimics, so the seeding is shared.
     The shit version above stays present in every mood. A bash session or a
     bash flavor in the init list advertises BASH_VERSION so a bash rc detects
     it. */
  bool should_seed_bash_identity = session_mood == shit::mimic_mood::Bash;
  for (let listed : init_moods)
    if (listed == shit::mimic_mood::Bash) should_seed_bash_identity = true;
  context.seed_shell_identity_variables(should_seed_bash_identity);

  /* A binary reached through a shitbox utility name, such as a symlink named
     ls, acts as that utility and exits rather than starting a shell. This is
     the busybox multicall, so a build that renames the binary gets the
     coreutility with no prefix. */
  if (shit::shitbox::find_util(program_basename).has_value()) {
    LOG(Info, "acting as the shitbox utility '%.*s'",
        static_cast<int>(program_basename.length), program_basename.data);
    return shit::shitbox::run_as_multicall(program_basename, file_names.clone(),
                                           context);
  }

  /* SHLVL counts shell nesting. It is read from the inherited environment,
     incremented, and exported so a child shell continues the count. */
  i64 shell_level = 0;
  if (shit::Maybe<shit::String> inherited =
          shit::os::get_environment_variable("SHLVL");
      inherited.has_value())
  {
    if (shit::ErrorOr<i64> parsed_level =
            shit::utils::parse_decimal_integer(inherited->view());
        !parsed_level.is_error() && parsed_level.value() > 0)
      shell_level = parsed_level.value();
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

  /* COLUMNS and LINES carry the terminal size the way bash sets them in an
     interactive shell, so a config that divides by COLUMNS, such as ble.sh,
     sees a non-zero width. They are seeded once here and not tracked across a
     later resize. */
  if (should_be_interactive) {
    u32 columns = 0, rows = 0;
    if (shit::os::terminal_size(columns, rows)) {
      context.set_shell_variable("COLUMNS", shit::utils::uint_to_text(columns));
      context.set_shell_variable("LINES", shit::utils::uint_to_text(rows));
    }
  }

  bool should_quit = FLAG_ONE_COMMAND.is_enabled() ? true : false;
  i32 exit_code = EXIT_SUCCESS;

  /* Clear and set up cache. Don't prematurely initialize the whole path map,
   * since it's only really noticeable in interactive mode. This way,
   * subsequent calls to the same program will still be cached in any mode,
   * but we won't waste any milliseconds traversing directories for very
   * simple scripts! */
  shit::utils::clear_path_map();
  shit::os::set_default_signal_handlers();
  LOG(Info, "installed the default signal handlers");

  /* The parse arena holds the AST and its tokens for one command, and is reset
     between commands. It outlives each tree it builds. */
  let ast_arena = shit::BumpArena{};
  shit::AST_ARENA = &ast_arena;

  /* The function arena holds function bodies, which outlive the command that
     defined them, so it is never reset during the run. */
  let function_arena = shit::BumpArena{};
  shit::FUNCTION_ARENA = &function_arena;

  /* The startup files source for each mood in the init list, in order, with the
     mood swapped per flavor so a bash rc parses under the bash grammar. A
     privileged shell sources nothing, the way bash's privileged mode leaves the
     profiles and rc files unread. */
  if (is_privileged || is_rescue_mode) {
    LOG(Info, "skipping every startup config file in %s mode",
        is_rescue_mode ? "rescue" : "privileged");
  } else {
    /* --no-init-diagnostics turns diagnostics and warnings off for the duration
       of the init sourcing, so a -W shell loads a lax bash config without
       printing its unset-variable and glob warnings. A function defined while
       this is off captures the quiet state, so it stays quiet at the prompt
       too. The state returns afterward, restoring -W for the session. */
    let const saved_diagnostics_disabled = context.diagnostics_disabled();
    let const saved_warnings = context.warnings_enabled();
    if (FLAG_SUPPRESS_INIT_DIAGNOSTICS.is_enabled()) {
      context.set_diagnostics_disabled(true);
      context.set_warnings_enabled(false);
    }
    shit::source_init_moods(context, ast_arena, init_moods, is_login_shell,
                            should_be_interactive);
    if (FLAG_SUPPRESS_INIT_DIAGNOSTICS.is_enabled()) {
      context.set_diagnostics_disabled(saved_diagnostics_disabled);
      context.set_warnings_enabled(saved_warnings);
    }
  }

  /* The startup files have finished, so a command typed at the prompt may now
     retitle the terminal. */
  context.set_startup_finished();

  /* The startup config has loaded, so the session mood takes over and seeds its
     strictness. A default-mood shell turns nounset, pipefail, and failglob on
     so a typo or a failing pipeline stage fails loudly, while a compatibility
     mood keeps the lax bash or dash defaults. An explicit set -u survives. The
     sourcing above swaps the mood per flavor, so the session mood is restored
     here, unless the rc itself picked a mood with set --mood, which wins over
     the startup default the way a command-line --mood would. */
  if (!context.mood_set_explicitly()) context.set_mood(session_mood);
  context.apply_strictness_for_mood();

  /* The profiles and rc files sourced through run_source each retained a heap
     copy of their whole text and their parsed tree, held until the next
     top-level command clears them. With the startup chain finished, that text
     is dropped now rather than carried through the idle prompt, since a
     function a profile defined keeps its body in the function arena and its
     source in an owned copy, so nothing live indexes the dropped buffers. */
  context.clear_retained_sources();

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
          LOG(Info, "reading the whole standard input");
          script_contents = shit::utils::read_entire_standard_input();
        } else {
          const shit::String &file_name = file_names[0];
          LOG(Info, "reading the script file '%s'", file_name.c_str());
          shit::Maybe<shit::String> contents =
              shit::utils::read_entire_file(file_name.view());
          if (!contents) {
            /* The caret points at the operand in the joined invocation, the
               same line the flag parser renders its errors against, so the
               unopenable name is located rather than reported bare. */
            usize operand_offset = 0;
            for (int a = 0; a < parse_argc; a++) {
              if (file_name.view() == parse_argv[a]) break;
              operand_offset += std::strlen(parse_argv[a]) + 1;
            }
            shit::show_message(
                shit::ErrorWithLocation{
                    shit::SourceLocation{operand_offset, file_name.count(),
                                         shit::None},
                    "Could not open '" + file_name.view() +
                        "': " + shit::os::last_system_error_message()
            }
                    .to_string(shit::join_command_line(parse_argc, parse_argv)
                                   .view()));
            shit::utils::quit(127, true);
          }
          script_contents = steal(*contents);
          source_filename = file_name.view();
          /* A script-file run bottoms FUNCNAME out at "main" the way bash
             marks it, while -c and stdin runs leave it off. */
          context.set_script_run(true);
        }

        should_quit = true;
      } else if (should_execute_commands) {
        shit::StringView command_view = FLAG_COMMAND.next();
        script_contents = shit::String{command_view};
        context.set_execution_string(command_view);
        LOG(Info, "taking the next -c command string, %zu bytes",
            script_contents.count());
        if (FLAG_COMMAND.at_end()) should_quit = true;
      } else if (should_be_interactive) {
        if (!toiletline::is_active()) {
          LOG(Info, "initializing the line editor and the path map");
          shit::utils::initialize_path_map();
          toiletline::initialize();
          /* The set -b wake hook registers whenever the editor runs, even
             under -T, since job reporting is not completion. */
          toiletline::enable_job_notifications(context);
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
              session_mood == shit::mimic_mood::Posix  ? "POSIX me harder!"
              : session_mood == shit::mimic_mood::Bash ? "Bash me harder!"
              : should_init_as_bash                    ? "Bash me harder?"
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

        /* Run the PROMPT_COMMAND hook before the template is expanded, so a
           framework that assigns PS1 inside the hook has its assignment in
           place by the time the prompt is built. */
        run_prompt_command(context, ast_arena);

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

        LOG(Info, "accepted an interactive line of %zu bytes",
            script_contents.count());
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
    const bool should_print_post_run_trailer =
        context.show_exit_code() || context.stats_enabled();
    context.set_terminal_exec_allowed(
        should_quit && !context.shell_is_interactive() &&
        !context.has_exit_trap() && !should_print_post_run_trailer);

    /* Execute the contents through the shared pipeline. */
    exit_code = run_script_contents(script_contents, context, ast_arena,
                                    source_filename);

    /* We can get here from child process if they didn't exec()
     * properly to print error. */
    if (should_quit || shit::os::is_child_process() ||
        (FLAG_ERROR_EXIT.is_enabled() && exit_code != 0))
    {
#if !defined NDEBUG
      /* The completion test driver runs after the staged chunks, so a -c
         that registered specs or sourced a completion file is visible to
         the engine, and the exit code reflects the driver alone. */
      if (FLAG_DEBUG_COMPLETE_AT.is_set() && !shit::os::is_child_process()) {
        exit_code = shit::run_debug_completion_driver(
            FLAG_DEBUG_COMPLETE_AT.value(), context);
      }
#endif
      LOG(Info, "exiting after the final chunk with code %d", exit_code);
      if (!shit::os::is_child_process()) context.run_exit_trap();
      shit::utils::quit(exit_code, FLAG_ERROR_EXIT.is_enabled());
    }
  }

  unreachable();
}
