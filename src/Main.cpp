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

FLAG_LIST_DECL();

/* clang-format off */
HELP_SYNOPSIS_DECL("[-OPTIONS] [--] <file1> [file2, ...]",
                   "[-OPTIONS] -c <script1> [-c <script2> ...]",
                   "[-OPTIONS] -",
                   "[-OPTIONS]");
/* clang-format on */

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
     "Source FILE as the interactive rc. In the shit mood it replaces "
     "/etc/shitrc "
     "and ~/.shitrc, and in the bash mood it replaces ~/.bashrc. A "
     "non-interactive "
     "run reads no rc.");
FLAG(PRIVILEGED, Bool, 'p', "privileged", Bash,
     "Run in privileged mode and skip every startup config file. Turned on "
     "automatically when the effective and the real user or group id differ.");
FLAG(
    CLEAN, Bool, '\0', "clean", Shit,
    "Start a clean shell. No startup file is read in any mood, and PATH is set "
    "to a minimal default.");
FLAG(POSIX_COMPAT, Bool, '\0', "posix", Bash,
     "Run in POSIX mode, equivalent to --mood sh.");

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
    MIMICRY, Bool, 'I', "mimicry", Compat,
    "Mimic the shell a script's shebang names. A program whose "
    "shebang is a shell shit can emulate runs in-process in the matching mode. "
    "sh and dash run in POSIX mode, bash in bash mode, and shit in the default "
    "mode. A zsh, ksh, fish, or non-shell shebang still launches the real "
    "program.");
FLAG(DUMB, Bool, '\0', "dumb", Compat,
     "Make shit extremely dumb. Equivalent to --mood sh -T --no-diagnostics.");

FLAG(
    WARNINGS, RepeatedBool, 'W', "", Shit,
    "Keep the analysis stage but demote a lenient analysis error to a warning "
    "and let the run proceed. A single -W demotes an unresolved command and a "
    "variable read before it is assigned. A strict error such as a malformed "
    "glob stays fatal under a single -W. A repeated -WW demotes every analysis "
    "error, the strict ones included. The runtime warnings are reported in the "
    "strict default mood under -W and in every mood under -WW.");
FLAG(LIST_CHECKS, Bool, '\0', "list-diagnostics", Shit,
     "List the shellcheck-style checks the analysis stage reports, then exit.");
FLAG(SUPPRESS_DIAGNOSTICS, Bool, '\0', "no-diagnostics", Shit,
     "Skip the analysis stage. No warnings or pre-run diagnostics are "
     "reported.");
FLAG(SUPPRESS_INIT_DIAGNOSTICS, Bool, '\0', "no-init-diagnostics", Shit,
     "Suppress diagnostics and warnings only while the startup profiles and rc "
     "files source, then restore them for the prompt. Pairs with -W to load a "
     "lax "
     "bash config quietly yet keep the checks afterward.");
FLAG(NO_COMPLETION, Bool, 'T', "no-completion", Shit,
     "Disable interactive tab completion and ghost-text.");
FLAG(NO_SYNTAX_HIGHLIGHTING, Bool, '\0', "no-syntax-highlighting", Shit,
     "Disable the syntax coloring and the ghost suggestion, leaving tab "
     "completion working.");
FLAG(ENABLE_SHITBOX, Bool, '\0', "enable-shitbox", Shit,
     "Resolve the bundled shitbox utility names such as ls and mkdir directly "
     "as commands, the same as set -o shitbox.");

FLAG(AST, Bool, 'A', "show-ast", Debug,
     "Print AST before executing each command.");
FLAG(SHOW_OPTIMIZER_STATE, Bool, '\0', "show-optimizer-state", Debug,
     "Trace the optimizer prepass decisions and print a located line for every "
     "node the analysis stage eliminated to standard error, then a final "
     "summary.");
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
/* A release binary rejects these flags as unknown, since its LOG calls compile
   out. */
#if !defined NDEBUG
FLAG(LOG, String, 'X', "debug-logging", Debug,
     "Enable internal logging at the given level, one of 'info', 'debug', or "
     "'all'. An unknown spelling is an error.");
FLAG(DEBUG_OUTPUT_FILE, String, '\0', "debug-logging-file", Debug,
     "Create the named file when missing and append the debug log to it, "
     "keeping an interactive session's log off the prompt. The default target "
     "is stderr.");
FLAG(DEBUG_COMPLETE_AT, String, '\0', "debug-complete-at", Debug,
     "Print the completion candidates for the given line with the cursor at "
     "its end, one per line the way an explicit tab lists them, after every "
     "-c chunk has run, then exit. This is the completion test driver.");
FLAG(DEBUG_HIGHLIGHT_AT, String, '\0', "debug-highlight-at", Debug,
     "Print the syntax-highlight spans for the given line, one per line as the "
     "span text and the escape that colors it with the escape byte shown as "
     "\\e, then exit. This is the highlighter test driver.");
#endif

#if SHIT_PLATFORM_IS COSMO
FLAG(COSMO_FTRACE, Bool, '\0', "ftrace", Debug,
     "Trace functions under Cosmopolitan.");
FLAG(COSMO_STRACE, Bool, '\0', "strace", Debug,
     "Trace system calls under Cosmopolitan.");
#endif

namespace shit {

fn shit_binary_flag_list() wontthrow -> const ArrayList<Flag *> &
{
  return FLAG_LIST;
}

#if !defined NDEBUG
static fn run_debug_completion_driver(StringView driver_line,
                                      EvalContext &context) throws -> i32
{
  utils::initialize_path_map();
  let const driver_result =
      completion::complete(driver_line, driver_line.length, context,
                           Path::current_directory(), true);
  let listing = String{heap_allocator()};
  for (let const &candidate : driver_result.candidates) {
    listing += candidate.view();
    listing += '\n';
  }
  print(listing);
  flush();
  return 0;
}

/* The escape byte is shown as \e so the golden stays readable. */
static fn run_debug_highlight_driver(StringView driver_line,
                                     EvalContext &context) throws -> i32
{
  let const spans = completion::highlight_line(driver_line, context);
  let listing = String{heap_allocator()};
  for (let const &span : spans) {
    listing +=
        driver_line.substring_of_length(span.start, span.end - span.start);
    listing += '\t';
    for (usize i = 0; i < span.sgr.length; i++) {
      if (span.sgr[i] == '\x1b') {
        listing += "\\e";
      } else {
        listing.push(span.sgr[i]);
      }
    }
    listing += '\n';
  }
  print(listing);
  flush();
  return 0;
}
#endif

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

/* The session mood, from --mood when given, then the invocation mood, then the
   strict default. --dumb forces the sh mood when --mood is absent. */
pure static fn resolve_session_mood(mimic_mood invocation_mood) wontthrow
    -> mimic_mood
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
  if (FLAG_POSIX_COMPAT.is_enabled()) return mimic_mood::Posix;
  return invocation_mood;
}

static fn print_help_or_version_status(const String &program_path) -> Maybe<int>
{
  if (FLAG_HELP.is_enabled()) {
    let h = String{heap_allocator()};
    h += "SHIT";
    h += "\n";
    h += wrap_text(
        "Shit is a pedantic, POSIX-compatible command line interpreter and a "
        "friendly interactive shell.\n\n",
        HELP_INDENT, HELP_WRAP_WIDTH);
    h += make_synopsis(program_path.view(), HELP_SYNOPSIS);
    h += '\n';
    h += wrap_text("Options are also read from the SHIT_FLAGS environment "
                   "variable. A flag "
                   "on the command line overrides one set there.\n\n",
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

static fn report_escaped_control_flow(EvalContext &context,
                                      const String &fallback_source) -> void
{
  if (!context.has_pending_control_flow()) return;

  const control_flow &control = context.pending_control_flow();
  let what = String{heap_allocator()};
  switch (control.kind) {
  case control_flow::Kind::Break:
    what = "'break' used outside of a loop";
    break;
  case control_flow::Kind::Continue:
    what = "'continue' used outside of a loop";
    break;
  case control_flow::Kind::Return: {
    /* A return at the top of a non-interactive script ends the shell with its
       status, the way dash treats a top-level return. */
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

static fn run_script_contents(const String &script_contents,
                              EvalContext &context, BumpArena &ast_arena,
                              Maybe<StringView> filename = None,
                              Expression *precompiled_ast = nullptr,
                              Expression **out_ast = nullptr) -> int
{
  i32 exit_code = EXIT_FAILURE;

  try {
    defer { context.end_command(); };

    /* Function bodies live in the separate function arena, so they survive this
       reset. */
    context.clear_retained_sources();
    ast_arena.reset();
    context.reset_scratch_arena();

    /* A precompiled tree lives in a caller-owned arena that outlives this call.
     */
    Expression *ast = precompiled_ast;
    if (precompiled_ast == nullptr) {
      LOG(Debug, "parsing a chunk of %zu bytes", script_contents.count());

      let p = Parser{
          Lexer{String{script_contents.view()}, ast_arena,
                context.show_lexed_words(), filename, context.mood()}
      };

      /* A file with any parse error must not run, so every error is collected
         and reported at once. */
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

    /* POSIX and bash mode skip the analysis stage, -W forces it on as warnings,
       and --no-diagnostics always skips it. The live context is read so a mood
       switch or a runtime set -o no-diagnostics flips it. */
    let const run_analysis =
        precompiled_ast == nullptr &&
        (FLAG_SHOW_OPTIMIZER_STATE.is_enabled() ||
         ((!(context.is_bash_compatible() || context.is_posix_mode()) ||
           FLAG_WARNINGS.count() > 0) &&
          !context.diagnostics_disabled()));
    LOG(Debug, "the analysis stage %s for this chunk",
        run_analysis ? "runs" : "is skipped");
    /* An interactive -W chunk runs right away and the runtime reports a missing
       command itself, so the analysis copy stays quiet to avoid a doubled
       error. */
    let const analysis_failed =
        run_analysis &&
        !analyze_ast(ast, script_contents, context.function_names(),
                     context.alias_names(), &context, context.warning_level(),
                     FLAG_WARNINGS.count() > 0 &&
                         context.shell_is_interactive(),
                     FLAG_SHOW_OPTIMIZER_STATE.is_enabled());
    if (!analysis_failed && out_ast != nullptr) {
      *out_ast = ast;
    }

    if (analysis_failed) {
      exit_code = EXIT_FAILURE;
    } else if (context.no_exec()) {
      exit_code = EXIT_SUCCESS;
    } else {
      LOG(Debug, "evaluating the chunk");
      context.set_current_source(&script_contents, "the script");
      const auto command_start_ns = shit::os::monotonic_nanos();
      exit_code = static_cast<int>(ast->evaluate(context));
      context.set_last_command_duration_ns(shit::os::monotonic_nanos() -
                                           command_start_ns);
      LOG(Debug, "the chunk finished with exit code %d", exit_code);
      /* A signal trapped during the last command has no following node to
         trigger its action, so the pending traps drain here. */
      if (shit::os::SIGNAL_PENDING) context.run_pending_traps();
      report_escaped_control_flow(context, script_contents);
      /* script_contents is local, so the frame is dropped before it dangles. */
      context.set_current_source(nullptr, "");
    }
    context.set_last_exit_status(static_cast<i32>(exit_code));

    if (context.show_exit_code())
      print("[Code " + String::from(exit_code, heap_allocator()) + "]\n");

    if (context.stats_enabled()) {
      print(context.make_stats_string());
      print("\n");
    }
  } catch (const ErrorWithLocationAndDetails &e) {
    /* An error thrown from a function body was already rendered at the call
       boundary against the file that defined it. */
    if (!e.was_rendered()) {
      show_message(e.to_string(script_contents));
      show_message(e.details_to_string(script_contents));
    }
    exit_code = context.is_posix_mode() ? 2 : EXIT_FAILURE;
  } catch (const ErrorWithLocation &e) {
    if (!e.was_rendered()) show_message(e.to_string(script_contents));
    /* bash exits 127 on script-fatal expansion aborts, POSIX follows dash with
       2. */
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

/* The cached text and parsed tree are kept across prompts so an unchanged hook
   parses once, in an arena the per-command reset never touches so the cached
   pointer stays valid. */
static BumpArena PROMPT_COMMAND_ARENA{};
static String PROMPT_COMMAND_CACHED_TEXT{heap_allocator()};
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
    run_script_contents(PROMPT_COMMAND_CACHED_TEXT, context, ast_arena,
                        StringView{"PROMPT_COMMAND"},
                        PROMPT_COMMAND_CACHED_AST);
  } else {
    /* A changed hook parses once into the prompt arena, reset first so the
       previous tree is reclaimed, and caches through out_ast. */
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

/* A missing file is not an error, since a login shell sources profiles that may
   not exist. Returns whether the file existed and ran. */
static fn source_file(const Path &path, EvalContext &context,
                      BumpArena &ast_arena) -> bool
{
  Maybe<String> contents = path.read_entire_file();
  if (!contents) {
    LOG(Info, "skipping '%s' because the file is missing or unreadable",
        path.c_str());
    return false;
  }

  LOG(Info, "sourcing '%s', %zu bytes", path.c_str(), contents->count());

  /* run_source parses into the active arena rather than resetting it, since a
     set --init-moods inside a sourced rc reaches here while that rc's tree is
     live and a reset would free the node mid-walk. */
  unused(ast_arena);
  context.run_source(*contents, path.text().view(), /*consume_return=*/true,
                     /*call_site=*/None, path.text().view());
  return true;
}

static fn source_home_file(StringView name, EvalContext &context,
                           BumpArena &ast_arena) throws -> void
{
  if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
    Path path = home->clone();
    path.push_component(name);
    source_file(path, context, ast_arena);
  }
}

/* The dash login files in POSIX order, /etc/profile then ~/.profile. */
static fn source_posix_login_files(EvalContext &context,
                                   BumpArena &ast_arena) throws -> void
{
  LOG(Info, "sourcing the posix login files");
  source_file(Path{"/etc/profile"}, context, ast_arena);
  source_home_file(".profile", context, ast_arena);
}

/* The bash login files in bash order, /etc/profile then the first existing of
   ~/.bash_profile, ~/.bash_login, ~/.profile. */
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

/* The system bashrc the way bash compiled with SYS_BASHRC reads it, the Void
   /etc/bash/bashrc or the Debian /etc/bash.bashrc, whichever exists first. */
static fn source_bash_system_rc(EvalContext &context,
                                BumpArena &ast_arena) throws -> void
{
  LOG(Info, "looking for the system bashrc");
  for (let const path : {"/etc/bash/bashrc", "/etc/bash.bashrc"})
    if (source_file(Path{path}, context, ast_arena)) break;
}

/* The default spec and the guard variable are probed so an already-loaded chain
   is not sourced twice. */
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
     grammar and a posix profile with the dash grammar. */
  bool did_source_bash_rc = false;
  for (let flavor : moods) {
    /* A flavor already on the sourcing stack is skipped, so a set --init-moods
       inside the rc this is sourcing cannot recurse to overflow. */
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
      /* A --rcfile replaces the shit rc with the named file. */
      if (is_login_shell) source_posix_login_files(context, ast_arena);
      if (should_be_interactive) {
        if (FLAG_RCFILE.is_set()) {
          source_file(Path{FLAG_RCFILE.value()}, context, ast_arena);
        } else {
          source_file(Path{"/etc/shitrc"}, context, ast_arena);
          source_home_file(".shitrc", context, ast_arena);
        }
      }
      break;
    case mimic_mood::Posix:
      if (is_login_shell) source_posix_login_files(context, ast_arena);
      if (should_be_interactive) {
        if (Maybe<String> env = context.get_variable_value("ENV");
            env.has_value() && !env->is_empty())
          source_file(Path{env->view()}, context, ast_arena);
      }
      break;
    case mimic_mood::Bash:
      /* bash runs the system rc first even under --rcfile, so the order mirrors
         that. */
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
    if (is_login_shell || should_be_interactive) {
      context.mark_mood_initialized(flavor);
    }
  }

  /* The bash programmable completion loads once after a bash rc sourced, so it
     parses under the bash grammar. */
  if (did_source_bash_rc) {
    LOG(Info, "bootstrapping the bash programmable completion");
    ensure_bash_completion_loaded(context, ast_arena);
  }
}

} // namespace shit

fn main(int argc, char **argv) -> int
{
#if SHIT_PLATFORM_IS COSMO
  ShowCrashReports();
  unused(FLAG_COSMO_FTRACE);
  unused(FLAG_COSMO_STRACE);
#endif

  /* A symlink or rename to a shitbox utility name runs that utility directly,
     before any flag parsing, so `ls -l` reaches ls and its own flag parser. */
  if (argc > 0) {
    shit::StringView invocation = shit::StringView{argv[0]};
    usize basename_start = 0;
    for (usize i = 0; i < invocation.length; i++)
      if (invocation[i] == '/') basename_start = i + 1;
    invocation = invocation.substring(basename_start);
    if (!invocation.is_empty() && invocation[0] == '-') {
      invocation = invocation.substring(1);
    }

    if (shit::shitbox::find_util(invocation).has_value()) {
      LOG(Info, "acting as the shitbox utility '%.*s' from argv[0]",
          static_cast<int>(invocation.length), invocation.data);
      shit::os::set_default_signal_handlers(false);
      shit::utils::clear_path_map();

      let ast_arena = shit::BumpArena{};
      shit::AST_ARENA = &ast_arena;
      let function_arena = shit::BumpArena{};
      shit::FUNCTION_ARENA = &function_arena;

      let context = shit::EvalContext{false, false, false,
                                      false, false, shit::String{invocation}};

      shit::ArrayList<shit::String> operands{shit::heap_allocator()};
      operands.reserve(static_cast<usize>(argc - 1));
      for (int i = 1; i < argc; i++)
        operands.push(shit::String{shit::StringView{argv[i]}});

      return static_cast<int>(shit::shitbox::run_as_multicall(
          invocation, steal(operands), context));
    }
  }

  bool is_login_shell = false;
  let file_names = shit::ArrayList<shit::String>{shit::heap_allocator()};

  /* SHIT_FLAGS supplies options through the environment. The whitespace-split
     tokens are spliced in right after the program name, so a command-line flag
     still has the final say. The token strings and the spliced pointer array
     outlive the parse below. */
  shit::ArrayList<shit::String> shit_flags_tokens{shit::heap_allocator()};
  shit::ArrayList<const char *> spliced_argv{shit::heap_allocator()};
  if (shit::Maybe<shit::String> shit_flags =
          shit::os::get_environment_variable("SHIT_FLAGS");
      shit_flags.has_value() && !shit_flags->is_empty())
  {
    let const view = shit_flags->view();
    usize token_start = 0;
    /* A -c in SHIT_FLAGS is dropped with the command word after it, since the
       variable must not splice a command into every invocation. */
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

  /* A login shell that launches with a broken flag config drops to a rescue
     prompt rather than exiting and locking the user out. The lockout-risk case
     is marked by a dash-prefixed argv[0], a bare - or -bash, so rescue is
     offered only there and any other invocation keeps the usage exit. */
  let const invocation_path =
      argc > 0 ? shit::Path{shit::StringView{argv[0]}} : shit::Path{};
  let const invocation_name = invocation_path.filename();
  const bool is_login_invocation =
      !invocation_name.is_empty() && invocation_name[0] == '-';

  bool is_rescue_mode = false;
  let const do_enter_rescue = [&]() {
    shit::show_message("Entering rescue.");
    is_rescue_mode = true;
    shit::reset_flags(FLAG_LIST);
    try {
      file_names = shit::parse_flags(FLAG_LIST, argc, argv, 0, &FLAG_COMMAND);
    } catch (...) {
      /* The real argv carried the bad flag too, so even the clean reparse
         fails. The program name is kept as the sole operand so $0 and SHELL
         stay the real name. */
      shit::reset_flags(FLAG_LIST);
      file_names = shit::ArrayList<shit::String>{shit::heap_allocator()};
      if (argc > 0) file_names.push(shit::String{argv[0]});
    }
  };

  try {
    file_names =
        shit::parse_flags(FLAG_LIST, parse_argc, parse_argv, 0, &FLAG_COMMAND);
  } catch (const shit::ErrorWithLocation &e) {
    shit::show_message(
        e.to_string(shit::join_command_line(parse_argc, parse_argv)));
    if (!is_login_invocation) {
      return 2;
    }
    do_enter_rescue();
  } catch (const shit::Error &e) {
    shit::show_message(e.to_string());
    if (!is_login_invocation) {
      return 2;
    }
    do_enter_rescue();
  }

  /* --dumb enables -T and --no-diagnostics and turns color off. The sh mood is
     selected by resolve_session_mood. */
  if (FLAG_DUMB.is_enabled()) {
    if (!FLAG_NO_COMPLETION.is_enabled()) FLAG_NO_COMPLETION.toggle();
    if (!FLAG_SUPPRESS_DIAGNOSTICS.is_enabled())
      FLAG_SUPPRESS_DIAGNOSTICS.toggle();
    shit::os::set_environment_variable("NO_COLOR", "1");
  }

  /* --clean resets PATH to a minimal default before the context seeds its
     variables from the environment. */
  if (FLAG_CLEAN.is_enabled()) {
    shit::os::set_environment_variable("PATH", "/usr/bin:/bin");
  }

  /* Raise the runtime log level before any helper runs, so the trace covers
     startup. */
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

  /* The sink opens in append mode. A file that cannot open leaves it on
     stderr. */
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

  let program_path = shit::String{shit::heap_allocator()};

  if (file_names.count() > 0) {
    program_path = steal(file_names[0]);
    file_names.remove(0);
  } else {
    program_path = "<unknown>";
  }

  /* A basename of sh or dash selects POSIX mode and a basename of bash selects
     bash mode, so a symlink named after a system shell behaves like it. */
  let const last_slash = program_path.find_last_character('/');
  shit::StringView program_basename =
      last_slash.has_value() ? program_path.substring(*last_slash + 1)
                             : program_path.view();
  /* A login shell receives argv[0] prefixed with a dash, such as -bash. The
     dash is stripped before the name is matched, and read again below as the
     login mark. $0 keeps the dashed spelling. */
  const bool does_name_mark_login =
      !program_basename.is_empty() && program_basename[0] == '-';
  if (does_name_mark_login) program_basename = program_basename.substring(1);

  /* SHELL and BASH must name a runnable file a child can exec, so the login
     dash is dropped here for the executable identity while $0 keeps the dashed
     spelling below. A dash inside a directory path is a real filename, and a
     bare dash keeps its spelling since it names nothing to run. */
  let executable_path = program_path.clone();
  if (does_name_mark_login && !last_slash.has_value() &&
      program_path.view().length > 1)
  {
    executable_path = shit::String{program_path.view().substring(1)};
  }

  const shit::mimic_mood invocation_mood =
      (program_basename == "sh" || program_basename == "dash")
          ? shit::mimic_mood::Posix
      : program_basename == "bash" ? shit::mimic_mood::Bash
                                   : shit::mimic_mood::Default;
  LOG(Info, "invocation basename is '%.*s'",
      static_cast<int>(program_basename.length), program_basename.data);
  let const session_mood = shit::resolve_session_mood(invocation_mood);
  LOG(Info, "selecting the %s mood",
      session_mood == shit::mimic_mood::Posix  ? "posix"
      : session_mood == shit::mimic_mood::Bash ? "bash"
                                               : "default");

  if (shit::Maybe<int> code = shit::print_help_or_version_status(program_path))
    return *code;

  /* A dash-prefixed invocation name, -bash or a bare -, is the login spawn
     convention, the same mark -l sets. */
  if (FLAG_LOGIN.is_enabled() || does_name_mark_login) {
    is_login_shell = true;
  }
  LOG(Info, "the shell %s a login shell", is_login_shell ? "is" : "is not");

  if (FLAG_MOOD.is_set() && !shit::parse_mood_name(FLAG_MOOD.value())) {
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

  let init_moods = shit::ArrayList<shit::mimic_mood>{shit::heap_allocator()};
  for (usize i = 0; i < FLAG_INIT_MOODS.count(); i++) {
    shit::StringView entry = FLAG_INIT_MOODS.get(i);
    /* A single --init-moods value may itself be comma-separated. */
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

  if (init_moods.is_empty()) init_moods.push(session_mood);

  /* A privileged shell skips every startup config file, so a profile a
     less-privileged user controls cannot run with the raised privileges. A
     setuid or setgid invocation turns it on. */
  let const is_privileged =
      FLAG_PRIVILEGED.is_enabled() || shit::os::is_running_setuid();
  LOG(Info, "privileged mode is %s", is_privileged ? "on" : "off");

  if (FLAG_STDIN.is_enabled() && FLAG_INTERACTIVE.is_enabled()) {
    bool is_tty = shit::os::is_stdin_a_tty();

    let s = shit::String{shit::heap_allocator()};
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

  /* The input source is chosen by flag precedence, -s first, then -c, then a
     file operand, then -i or no arguments. */
  if (FLAG_STDIN.is_enabled()) {
    if (!FLAG_COMMAND.is_empty() || FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message(
          "Incompatible options or arguments were specified along "
          "with '-s' option. "
          "Falling back to '-s'.");
    }
    should_read_stdin = true;
  } else if (!FLAG_COMMAND.is_empty()) {
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
  } else if (FLAG_INTERACTIVE.is_enabled() || shit::os::is_stdin_a_tty()) {
    should_be_interactive = true;
  } else {
    should_read_stdin = true;
  }
#if !defined NDEBUG
  /* The test drivers never prompt, so a bare driver run reads the empty
     standard input and exits through the driver's hook. */
  if ((FLAG_DEBUG_COMPLETE_AT.is_set() || FLAG_DEBUG_HIGHLIGHT_AT.is_set()) &&
      should_be_interactive)
  {
    should_be_interactive = false;
    should_read_stdin = true;
  }
#endif
  LOG(Info, "the input source is %s",
      should_read_stdin         ? "standard input"
      : should_execute_commands ? "the -c command strings"
      : should_read_files       ? "the named script file"
                                : "the interactive prompt");

  /* A script file or a -c run takes its first operand as $0 and the rest as the
     arguments, while an interactive or -s shell keeps the shell name as $0 and
     takes every operand as a positional parameter. */
  let shell_name = program_path.clone();
  let positional_params = shit::ArrayList<shit::String>{shit::heap_allocator()};

  usize first_param_index = 0;
  if ((should_read_files || should_execute_commands) && !file_names.is_empty())
  {
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

  shit::utils::set_quit_context(&context);

  context.set_stats_enabled(FLAG_STATS.is_enabled());
  context.set_show_ast(FLAG_AST.is_enabled());
  context.set_show_lexed_words(FLAG_ESCAPE_MAP.is_enabled());
  context.set_show_exit_code(FLAG_EXIT_CODE.is_enabled());
  context.set_memory_stats_enabled(FLAG_MEMORY.is_enabled());
  context.set_diagnostics_disabled(FLAG_SUPPRESS_DIAGNOSTICS.is_enabled());
  context.set_login_shell(is_login_shell);
  context.set_custom_rcfile(FLAG_RCFILE.is_set());
  /* The startup files source with strictness off, since they read unset
     variables such as $BASH_VERSION on the /etc/profile path. The session
     strictness is applied at the seam below once the config has loaded. */
  context.set_mood(session_mood);
  /* The CLI -u is the user's own ask, so the -W downgrade leaves it fatal and
     the mood seam keeps it on. */
  context.set_error_unset(FLAG_NOUNSET.is_enabled());
  if (FLAG_NOUNSET.is_enabled()) context.set_error_unset_explicit(true);
  let const warnings_specified_count = FLAG_WARNINGS.count();
  context.set_warning_level(static_cast<u8>(
      warnings_specified_count > 2 ? 2 : warnings_specified_count));
  context.set_pipefail(false);
  context.set_no_clobber(FLAG_NO_CLOBBER.is_enabled());
  context.set_export_all(FLAG_EXPORT_ALL.is_enabled());
  context.set_no_exec(FLAG_NO_EXEC.is_enabled());
  context.set_shitbox(FLAG_ENABLE_SHITBOX.is_enabled());
  context.set_failglob(false);
  /* Mimicry is mirrored onto the context, since the execution path in Utils
     reads it there rather than the static flag. */
  context.set_mimicry(FLAG_MIMICRY.is_enabled());
  context.set_monitor(should_be_interactive);

  /* BASH names the path used to invoke this shell, the symlink spelling such as
     /usr/local/bin/bash when shit is symlinked to bash. */
  context.set_shell_executable_path(executable_path);
  /* SHELL is owned by login, getty, or the display manager, so an inherited
     value is left untouched. Only a shell that received no SHELL seeds its own
     invocation path. */
  if (!shit::os::get_environment_variable("SHELL").has_value())
    context.set_shell_variable("SHELL", executable_path);
  context.set_shell_variable("PWD", shit::Path::current_directory().text());
  context.set_shell_variable("SHIT", executable_path);
  context.set_shell_variable("SHIT_VERSION", SHIT_VERSION_STRING);
  context.set_shell_variable("SHIT_COMMIT", SHIT_COMMIT_HASH);
  context.set_shell_variable("SHIT_BUILD_MODE", SHIT_BUILD_MODE);
  context.set_shell_variable("SHIT_OS", SHIT_OS_INFO);

  /* A bash session or a bash flavor in the init list advertises BASH_VERSION so
     a bash rc detects it. */
  bool should_seed_bash_identity = session_mood == shit::mimic_mood::Bash;
  for (let listed : init_moods)
    if (listed == shit::mimic_mood::Bash) should_seed_bash_identity = true;
  context.seed_shell_identity_variables(should_seed_bash_identity);

  /* SHLVL counts shell nesting, incremented and exported so a child shell
     continues the count. */
  i64 shell_level = 0;
  if (shit::Maybe<shit::String> inherited =
          shit::os::get_environment_variable("SHLVL");
      inherited.has_value())
  {
    if (shit::ErrorOr<i64> parsed_level = inherited->view().to<i64>();
        !parsed_level.is_error() && parsed_level.value() > 0)
      shell_level = parsed_level.value();
  }
  /* An inherited level past the cap is reset so the increment cannot overflow,
     the way bash bounds SHLVL. */
  constexpr i64 MAX_SHLVL = 999;
  if (shell_level > MAX_SHLVL) shell_level = 0;
  shit::os::set_environment_variable(
      "SHLVL", shit::String::from(shell_level + 1, shit::heap_allocator()));
  /* The exported set must know SHLVL even on a first shell that did not inherit
     one. */
  context.mark_exported("SHLVL");

  /* The default prompt lives in PS1 unless the environment already supplies
     one. */
  if (!shit::os::get_environment_variable("PS1").has_value())
    context.set_shell_variable("PS1", toiletline::default_prompt_template());

  /* PS3 is left unset, since the select loop falls back to its own default. */
  if (!shit::os::get_environment_variable("PS2").has_value())
    context.set_shell_variable("PS2", "> ");
  if (!shit::os::get_environment_variable("PS4").has_value())
    context.set_shell_variable("PS4", "+ ");

  /* COLUMNS and LINES carry the terminal size so a config that divides by
     COLUMNS, such as ble.sh, sees a non-zero width. They are seeded once and
     not tracked across a later resize. */
  if (should_be_interactive) {
    u32 columns = 0, rows = 0;
    if (shit::os::terminal_size(columns, rows)) {
      context.set_shell_variable(
          "COLUMNS", shit::String::from(columns, shit::heap_allocator()));
      context.set_shell_variable(
          "LINES", shit::String::from(rows, shit::heap_allocator()));
    }
  }

  bool should_quit = FLAG_ONE_COMMAND.is_enabled();
  i32 exit_code = EXIT_SUCCESS;

  /* The path map is reset rather than seeded here, since the eager scan pays
     off only in interactive mode. */
  shit::utils::clear_path_map();
  shit::os::set_default_signal_handlers(should_be_interactive);
  LOG(Info, "installed the default signal handlers");

  /* The parse arena holds the AST and its tokens for one command, reset between
     commands. */
  let ast_arena = shit::BumpArena{};
  shit::AST_ARENA = &ast_arena;

  /* Function bodies outlive the command that defined them, so the function
     arena is never reset during the run. */
  let function_arena = shit::BumpArena{};
  shit::FUNCTION_ARENA = &function_arena;

  /* A privileged shell sources nothing, and --clean skips the startup files
     too. */
  if (is_privileged || is_rescue_mode || FLAG_CLEAN.is_enabled()) {
    LOG(Info, "skipping every startup config file in %s mode",
        is_rescue_mode            ? "rescue"
        : FLAG_CLEAN.is_enabled() ? "clean"
                                  : "privileged");
  } else {
    /* --no-init-diagnostics turns diagnostics and warnings off while the init
       files source, so a -W shell loads a lax bash config quietly, then
       restores them for the session. */
    let const saved_diagnostics_disabled = context.diagnostics_disabled();
    let const saved_warning_level = context.warning_level();
    if (FLAG_SUPPRESS_INIT_DIAGNOSTICS.is_enabled()) {
      context.set_diagnostics_disabled(true);
      context.set_warning_level(0);
    }
    shit::source_init_moods(context, ast_arena, init_moods, is_login_shell,
                            should_be_interactive);
    if (FLAG_SUPPRESS_INIT_DIAGNOSTICS.is_enabled()) {
      context.set_diagnostics_disabled(saved_diagnostics_disabled);
      context.set_warning_level(saved_warning_level);
    }
  }

  /* An rc that unsets PS1 to fall back on the built-in prompt would otherwise
     leave a PROMPT_COMMAND wrapper such as the VTE or the WezTerm integration to
     wrap an empty value into a blank prompt. The default is re-seeded when the
     config left PS1 unset, so the wrapper wraps a real prompt. */
  if (should_be_interactive && !context.get_variable_value("PS1").has_value())
    context.set_shell_variable("PS1", toiletline::default_prompt_template());

  context.set_startup_finished();

  /* The session mood takes over and seeds its strictness once the config has
     loaded, unless the rc picked one with set --mood, which wins the way a
     command-line --mood would. */
  if (!context.mood_set_explicitly()) context.set_mood(session_mood);
  context.apply_strictness_for_mood();

  /* The rc files retained a heap copy of their text and tree until the next
     top-level command clears them, dropped now rather than carried through the
     idle prompt. */
  context.clear_retained_sources();

  /* A plain return must not be used past this point, since toiletline needs its
     own cleanup that utils::quit() runs. */
  loop
  {
    ASSERT(!shit::os::is_child_process());

    let script_contents = shit::String{shit::heap_allocator()};
    /* The named script file flows into the diagnostics so an error reads
       path:line:col. A -c or interactive line carries no path. */
    shit::Maybe<shit::StringView> source_filename = shit::None;

    try {
      if (should_read_files || should_read_stdin) {
        /* -s or a "-" operand reads standard input, otherwise the named file is
           read through the descriptor layer. */
        if (should_read_stdin || file_names[0] == "-") {
          LOG(Info, "reading the whole standard input");
          script_contents = shit::utils::read_entire_standard_input();
        } else {
          const shit::String &file_name = file_names[0];
          LOG(Info, "reading the script file '%s'", file_name.c_str());
          shit::Maybe<shit::String> contents =
              shit::Path{file_name.view()}.read_entire_file();
          if (!contents) {
            /* The caret points at the operand in the joined invocation. */
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
          /* A script-file run bottoms FUNCNAME out at "main", while -c and
             stdin runs leave it off. */
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
          /* The set -b wake hook registers even under -T, since job reporting
             is not completion. */
          toiletline::enable_job_notifications(context);
          if (!FLAG_NO_COMPLETION.is_enabled())
            toiletline::enable_completion(context);

          let const should_highlight =
              !FLAG_NO_COMPLETION.is_enabled() &&
              !FLAG_NO_SYNTAX_HIGHLIGHTING.is_enabled();
          toiletline::set_highlight_enabled(should_highlight);
          toiletline::set_ghost_enabled(should_highlight);
          shit::show_message(
              session_mood == shit::mimic_mood::Posix  ? "POSIX me harder!"
              : session_mood == shit::mimic_mood::Bash ? "Bash me harder!"
                                                       : "Welcome :3");
        } else {
          toiletline::enter_raw_mode();
        }

        context.notify_done_jobs();

        /* The PROMPT_COMMAND hook runs before the template is expanded, so a
           framework that assigns PS1 inside it is in place by then. */
        run_prompt_command(context, ast_arena);

        /* A command whose output did not end in a newline leaves the cursor off
           the first column. A marker, spaces to the line width, and a carriage
           return push the prompt to a fresh line, and on a clean line the
           prompt overwrites the marker so nothing shows. */
        if (should_be_interactive) {
          u32 marker_columns = 0, marker_rows = 0;
          if (shit::os::terminal_size(marker_columns, marker_rows) &&
              marker_columns > 0)
          {
            shit::String eol_marker{shit::heap_allocator()};
            /* One allocation holds the glyph, the fill spaces, and the controls
               so the fill loop never regrows the buffer. */
            eol_marker.reserve(marker_columns + 12);
            if (shit::colors::stdout_wants_color()) {
              eol_marker += shit::colors::ansi::INVERSE;
              eol_marker += "\\n";
              eol_marker += shit::colors::ansi::RESET;
            } else {
              eol_marker += "\\n";
            }
            /* The marker is the two-column \n glyph, so the fill starts at
               column two. */
            for (u32 column = 2; column < marker_columns; column++)
              eol_marker.push(' ');
            eol_marker.push('\r');
            shit::print(eol_marker);
            shit::flush();
          }
        }

        shit::String prompt = toiletline::build_prompt(context);

        toiletline::set_edit_mode(context.vi_mode());

        loop
        {
          let[code, input] = toiletline::get_input(prompt);

          switch (code) {
          case TL_PRESSED_TAB:
            /* This fires only when there was nothing to complete, so the line
               is re-fed rather than inserting a literal tab. */
            toiletline::set_input(input);
            continue;
          case TL_PRESSED_EOF:
            /* EOF logs out only on an empty line. On a non-empty line it is
               ignored so the user can finish the command. */
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
          case TL_PRESSED_QUIT:
            toiletline::emit_newlines(input);
            shit::utils::quit(exit_code, true);
            break;
          case TL_PRESSED_INTERRUPT:
            shit::print("^C");
            shit::flush();
            break;
          case TL_PRESSED_SUSPEND:
            shit::print("^Z");
            shit::flush();
            break;
          default:;
          }

          toiletline::emit_newlines(input);

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

    /* A Ctrl-C used to clear the input line must not abort the command about to
       run, so a pending interrupt is dropped here. */
    shit::os::INTERRUPT_REQUESTED = 0;

    /* On the final chunk a terminal external command may replace the shell
       process rather than fork, exec, and wait, the way dash execs the last
       command under EV_EXIT. An interactive prompt, an EXIT trap, or a pending
       trailer keeps the fork to regain control. */
    const bool should_print_post_run_trailer =
        context.show_exit_code() || context.stats_enabled();
    context.set_terminal_exec_allowed(
        should_quit && !context.shell_is_interactive() &&
        !context.has_exit_trap() && !should_print_post_run_trailer);

    if (context.shell_is_interactive() && !script_contents.is_empty()) {
      shit::String ps0 = toiletline::render_ps0(context);
      if (!ps0.is_empty()) {
        shit::print(ps0);
        shit::flush();
      }
    }

    exit_code = run_script_contents(script_contents, context, ast_arena,
                                    source_filename);

    /* A child process reaches here when its exec() failed and printed the error
       itself. */
    if (should_quit || shit::os::is_child_process() ||
        (FLAG_ERROR_EXIT.is_enabled() && exit_code != 0))
    {
#if !defined NDEBUG
      /* The completion test driver runs after the staged chunks, so a -c that
         registered specs is visible to the engine. */
      if (FLAG_DEBUG_COMPLETE_AT.is_set() && !shit::os::is_child_process()) {
        exit_code = shit::run_debug_completion_driver(
            FLAG_DEBUG_COMPLETE_AT.value(), context);
      }
      if (FLAG_DEBUG_HIGHLIGHT_AT.is_set() && !shit::os::is_child_process()) {
        exit_code = shit::run_debug_highlight_driver(
            FLAG_DEBUG_HIGHLIGHT_AT.value(), context);
      }
#endif
      LOG(Info, "exiting after the final chunk with code %d", exit_code);
      if (!shit::os::is_child_process()) context.run_exit_trap();
      shit::utils::quit(exit_code, FLAG_ERROR_EXIT.is_enabled());
    }
  }

  unreachable();
}
