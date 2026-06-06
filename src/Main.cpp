#include "Arena.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Os.hpp"
#include "Parser.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-OPTIONS] [--] <file1> [file2, ...]", "[-OPTIONS] [-]",
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
FLAG(NO_EXEC, Bool, 'n', "no-exec", "Read and parse commands but do not run them.");
FLAG(NOUNSET, Bool, 'u', "nounset", "Treat an unset variable as an error.");
FLAG(LOGIN, Bool, 'l', "login", "Act as a login shell and source the profiles.");

FLAG(IGNORED1, Bool, 'h', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", "Ignored, left for compatibility.");

FLAG(AST, Bool, 'A', "ast", "Print AST before executing each command.");
FLAG(ESCAPE_MAP, Bool, 'M', "escape-bitmap",
     "Print escape bitmap after each parsed command.");
FLAG(EXIT_CODE, Bool, 'E', "exit-code",
     "Print exit code after each executed command.");
FLAG(STATS, Bool, 'S', "stats",
     "Print statistics after each executed command.");

FLAG(VERSION, Bool, '\0', "version", "Display program version and notices.");
FLAG(SHORT_VERSION, Bool, 'V', "short-version",
     "Display version in a short form.");
FLAG(HELP, Bool, '\0', "help", "Display help message.");

#if SHIT_PLATFORM_IS COSMO
FLAG(COSMO_FTRACE, Bool, '\0', "ftrace", "Cosmopolitan: Trace functions.");
FLAG(COSMO_STRACE, Bool, '\0', "strace", "Cosmopolitan: Trace system calls.");
#endif

/* Report a break, continue, or return that reached the top with no loop,
   function, or sourced script to consume it. The jump carries the source and
   the origin it was made in, so the caret points at the exact builtin and the
   note names where it ran. */
static void
report_escaped_control_flow(shit::EvalContext &context,
                            const std::string &fallback_source)
{
  if (!context.has_pending_control_flow()) return;

  const shit::ControlFlow &control = context.pending_control_flow();
  std::string what;
  switch (control.kind) {
  case shit::ControlFlow::Kind::Break:
    what = "'break' used outside of a loop";
    break;
  case shit::ControlFlow::Kind::Continue:
    what = "'continue' used outside of a loop";
    break;
  case shit::ControlFlow::Kind::Return:
    what = "'return' used outside of a function or a sourced script";
    break;
  case shit::ControlFlow::Kind::Exit:
  case shit::ControlFlow::Kind::Normal:
    context.clear_control_flow();
    return;
  }

  const std::string *source =
      control.source != nullptr ? control.source : &fallback_source;
  shit::ErrorWithLocation located{control.location, what};
  shit::show_message(located.to_string(*source));
  if (!control.origin.empty())
    shit::show_message(
        shit::Note{"this jump was reached while running " + control.origin}
            .to_string());

  context.clear_control_flow();
}

/* Lex, parse, validate, and evaluate one chunk of shell source in the given
   context. The main loop and source_file share this so a sourced file runs the
   same pipeline as an interactive line. Returns the resulting exit code. */
static int
run_script_contents(const std::string &script_contents,
                    shit::EvalContext &context, shit::BumpArena &ast_arena)
{
  int exit_code = EXIT_FAILURE;

  try {
    SHIT_DEFER { context.end_command(); };

    /* Reclaim the previous command's arena storage before the next parse, and
       destroy the eval and dot ASTs that point into it. Function bodies live in
       the separate function arena, so they survive this reset and a function
       defined on one command stays callable on the next. */
    context.clear_retained_sources();
    ast_arena.reset();
    context.reset_scratch_arena();

    shit::Parser p{
        shit::Lexer{script_contents, ast_arena, FLAG_ESCAPE_MAP.is_enabled()}
    };
    std::unique_ptr<shit::Expression> ast = p.construct_ast();

    if (FLAG_AST.is_enabled()) {
      shit::print_to_standard_output(ast->to_ast_string());
      shit::print_to_standard_output("\n");
    }

    if (FLAG_ESCAPE_MAP.is_enabled()) {
      for (const auto &word : p.debug_words()) {
        shit::print_to_standard_output(word.to_pretty_string());
        shit::print_to_standard_output("\n");
      }
    }

    /* Validate the whole tree before running anything. An unconditional
       problem stops execution, a conditional one only warns. */
    if (!shit::analyze_ast(ast.get(), script_contents,
                           context.function_names(), context.alias_names()))
    {
      exit_code = EXIT_FAILURE;
    } else if (context.no_exec()) {
      /* Under -n the tree is parsed and validated but never run. */
      exit_code = EXIT_SUCCESS;
    } else {
      context.set_current_source(&script_contents, "the script");
      exit_code = static_cast<int>(ast->evaluate(context));
      report_escaped_control_flow(context, script_contents);
      /* script_contents is local to this call, so drop the frame before it goes
         out of scope and leaves a dangling pointer behind. */
      context.set_current_source(nullptr, "");
    }
    context.set_last_exit_status(static_cast<i32>(exit_code));

    if (FLAG_EXIT_CODE.is_enabled())
      shit::print_to_standard_output("[Code " + std::to_string(exit_code) +
                                     "]\n");

    if (FLAG_STATS.is_enabled())
    {
      shit::print_to_standard_output(context.make_stats_string());
      shit::print_to_standard_output("\n");
    }
  } catch (const shit::ErrorWithLocationAndDetails &e) {
    shit::show_message(e.to_string(script_contents));
    shit::show_message(e.details_to_string(script_contents));
  } catch (const shit::ErrorWithLocation &e) {
    shit::show_message(e.to_string(script_contents));
  } catch (const shit::Error &e) {
    shit::show_message(e.to_string());
  } catch (const std::exception &e) {
    shit::show_message(
        "Uncaught exception while executing the AST. Aborting the command.");
    shit::show_message("Last system message: '" +
                       shit::os::last_system_error_message() + "'.");
    shit::show_message("Context: '" + std::string{e.what()} + "'.");
  } catch (...) {
    shit::show_message(
        "Unexpected system explosion while executing the AST. Exiting.");
    shit::show_message("Last system message: " +
                       shit::os::last_system_error_message());
    shit::utils::quit(EXIT_FAILURE);
  }

  return exit_code;
}

/* Read a whole file and run it in the given context. A missing file is not an
   error, since a login shell sources profiles that may not exist. */
static void
source_file(const std::filesystem::path &path, shit::EvalContext &context,
            shit::BumpArena &ast_arena)
{
  shit::Maybe<std::string> contents =
      shit::utils::read_entire_file(path.string());
  if (!contents) return;

  run_script_contents(*contents, context, ast_arena);
}

/* Expand the common prompt escapes in PS1 and PS2. */
static std::string
expand_prompt_escapes(const std::string &prompt, const std::string &user,
                      const std::string &working_directory)
{
  std::string out{};
  for (usize i = 0; i < prompt.length(); i++) {
    if (prompt[i] != '\\' || i + 1 >= prompt.length()) {
      out += prompt[i];
      continue;
    }
    char escaped = prompt[++i];
    switch (escaped) {
    case 'u': out += user; break;
    case 'h':
      out +=
          shit::os::get_environment_variable("HOSTNAME").value_or("localhost");
      break;
    case 'w': {
      std::string shown = working_directory;
      shit::Maybe<std::filesystem::path> home = shit::os::get_home_directory();
      if (home && shown.rfind(home->string(), 0) == 0)
        shown = "~" + shown.substr(home->string().length());
      out += shown;
    } break;
    case 'W':
      out += std::filesystem::path{working_directory}.filename().string();
      break;
    case '$': out += (user == "root") ? '#' : '$'; break;
    case 'n': out += '\n'; break;
    case 't': out += '\t'; break;
    case '\\': out += '\\'; break;
    default:
      out += '\\';
      out += escaped;
      break;
    }
  }
  return out;
}

int
main(int argc, char **argv)
{
#if SHIT_PLATFORM_IS COSMO
  ShowCrashReports();
  SHIT_UNUSED(FLAG_COSMO_FTRACE);
  SHIT_UNUSED(FLAG_COSMO_STRACE);
#endif

  bool is_login_shell = false;
  std::vector<std::string> file_names{};

  try {
    file_names = shit::parse_flags(FLAG_LIST, argc, argv);
  } catch (const shit::Error &e) {
    shit::show_message(e.to_string());
    return EXIT_SUCCESS;
  }

  /* Program path is the first argument. Pull it out and get rid of it. */
  std::string program_path{};

  if (file_names.size() > 0) {
    program_path = file_names[0];
    file_names.erase(file_names.begin());
  } else {
    program_path = "<unknown>";
  }

  if (FLAG_HELP.is_enabled()) {
    std::string h{};
    h += shit::make_synopsis(program_path, HELP_SYNOPSIS);
    h += '\n';
    h += shit::make_flag_help(FLAG_LIST);
    h += '\n';
    shit::print_to_standard_error(h);
    return EXIT_SUCCESS;
  } else if (FLAG_VERSION.is_enabled()) {
    shit::show_version();
    return EXIT_SUCCESS;
  } else if (FLAG_SHORT_VERSION.is_enabled()) {
    shit::show_short_version();
    return EXIT_SUCCESS;
  }

  if (FLAG_LOGIN.is_enabled() || program_path == "-") is_login_shell = true;

  /* Both stdin and interactive flags are enabled, but there will be only the
   * last man standing. */
  if (FLAG_STDIN.is_enabled() && FLAG_INTERACTIVE.is_enabled()) {
    bool is_tty = shit::os::is_stdin_a_tty();

    std::string s{};
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
    if (!FLAG_COMMAND.is_empty() || !file_names.empty() ||
        FLAG_INTERACTIVE.is_enabled())
    {
      shit::show_message(
          "Incompatible options or arguments were specified along "
          "with '-s' option. "
          "Falling back to '-s'.");
    }
    should_read_stdin = true;
  } else if (!FLAG_COMMAND.is_empty()) {
    if (!file_names.empty() || FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message(
          "Incompatible options or arguments were specified along "
          "with '-c' options. "
          "Falling back to '-c'.");
    }
    should_execute_commands = true;
  } else if (!file_names.empty()) {
    if (FLAG_INTERACTIVE.is_enabled()) {
      shit::show_message("Both file argument and '-i' option were given. "
                         "Falling back to reading files.");
    }
    should_read_files = true;
  } else {
    should_be_interactive = true;
  }

  /* Main loop state. The program name is $0 and the remaining arguments are the
     positional parameters $1 upward. */
  shit::EvalContext context{FLAG_DISABLE_EXPANSION.is_enabled(),
                            FLAG_VERBOSE.is_enabled(),
                            FLAG_EXPAND_VERBOSE.is_enabled(),
                            should_be_interactive,
                            FLAG_ERROR_EXIT.is_enabled(),
                            program_path,
                            file_names};

  /* Apply the remaining option flags that the constructor does not take. */
  context.set_error_unset(FLAG_NOUNSET.is_enabled());
  context.set_no_clobber(FLAG_NO_CLOBBER.is_enabled());
  context.set_export_all(FLAG_EXPORT_ALL.is_enabled());
  context.set_no_exec(FLAG_NO_EXEC.is_enabled());
  /* Monitor mode is on by default in an interactive shell, the way job control
     is enabled at a prompt. */
  context.set_monitor(should_be_interactive);

  /* Seed the standard and shell-specific variables a script may read. The
     version and runtime values come from the build. */
  context.set_shell_variable("SHELL", program_path);
  context.set_shell_variable("PWD",
                             shit::utils::get_current_directory().string());
  context.set_shell_variable(
      "SHIT_VERSION", std::to_string(SHIT_VER_MAJOR) + "." +
                          std::to_string(SHIT_VER_MINOR) + "." +
                          std::to_string(SHIT_VER_PATCH) + "-" SHIT_VER_EXTRA);
  context.set_shell_variable("SHIT_COMMIT", SHIT_COMMIT_HASH);
  context.set_shell_variable("SHIT_BUILD_MODE", SHIT_BUILD_MODE);
  context.set_shell_variable("SHIT_OS", SHIT_OS_INFO);

  usize arg_index = 0;
  bool should_quit = FLAG_ONE_COMMAND.is_enabled() ? true : false;
  int exit_code = EXIT_SUCCESS;

  /* Clear and set up cache. Don't prematurely initialize the whole path map,
   * since it's only really noticeable in interactive mode. This way,
   * subsequent calls to the same program will still be cached in any mode,
   * but we won't waste any milliseconds traversing directories for very
   * simple scripts! */
  shit::utils::clear_path_map();
  shit::os::set_default_signal_handlers();

  /* The parse arena holds the AST and its tokens for one command, and is reset
     between commands. It outlives each tree it builds. */
  shit::BumpArena ast_arena{};
  shit::g_ast_arena = &ast_arena;

  /* The function arena holds function bodies, which outlive the command that
     defined them, so it is never reset during the run. */
  shit::BumpArena function_arena{};
  shit::g_function_arena = &function_arena;

  /* A login shell reads /etc/profile and ~/.profile if they exist, then the
     file named by ENV when that is set. A missing file is silently skipped. */
  if (is_login_shell) {
    source_file("/etc/profile", context, ast_arena);
    if (shit::Maybe<std::filesystem::path> home =
            shit::os::get_home_directory();
        home.has_value())
    {
      source_file(*home / ".profile", context, ast_arena);
    }
    if (shit::Maybe<std::string> env = context.get_variable_value("ENV");
        env.has_value() && !env->empty())
    {
      source_file(*env, context, ast_arena);
    }
  }

  /* A simple return cannot be used after this point, since we need a special
   * cleanup for toiletline. utils::quit() should be used instead. */
  for (;;) {
    SHIT_ASSERT(!shit::os::is_child_process());

    std::string script_contents{};

    /* Figure out what to do and retrieve the code. */
    try {
      if (should_read_files || should_read_stdin) {
        /* If "-s" is used, or when the file name is "-", read standard input,
           otherwise read the named file, both through the descriptor layer so
           no iostream file stream is pulled in. */
        if (should_read_stdin || file_names[arg_index] == "-") {
          should_quit = should_quit || should_read_stdin;
          script_contents = shit::utils::read_entire_standard_input();
        } else {
          shit::Maybe<std::string> contents =
              shit::utils::read_entire_file(file_names[arg_index]);
          if (!contents) {
            throw shit::Error{"Could not open '" + file_names[arg_index] +
                              "': " + shit::os::last_system_error_message()};
          }
          script_contents = *contents;
        }

        if ((arg_index += 1) == file_names.size()) {
          should_quit = true;
        }
      } else if (should_execute_commands) {
        script_contents = FLAG_COMMAND.next();
        if (FLAG_COMMAND.at_end()) should_quit = true;
      } else if (should_be_interactive) {
        if (!toiletline::is_active()) {
          shit::utils::initialize_path_map();
          toiletline::initialize();
          shit::show_message("Welcome :3");
        } else {
          /* NOTE: avoid this branch if exit_raw_mode() wasn't called
           * previosly! */
          toiletline::enter_raw_mode();
        }

        static constexpr usize PWD_LENGTH = 24;

        std::string full_pwd = shit::utils::get_current_directory().string();
        toiletline::set_title("shit @ " + full_pwd);

        std::string pwd = full_pwd;
        if (pwd.length() > PWD_LENGTH)
          pwd = "..." + pwd.substr(pwd.length() - PWD_LENGTH + 3);

        std::string u = shit::os::get_current_user().value_or("???");

        /* shit % ...wd1/pwd2/pwd3/pwd4/pwd5 $ command */
        std::string prompt{};
        if (shit::Maybe<std::string> ps1 = context.get_variable_value("PS1");
            ps1.has_value() && !ps1->empty())
        {
          /* A user-set PS1 expands its escape sequences, \u \h \w \W \$ and
             the like. */
          prompt = expand_prompt_escapes(*ps1, u, full_pwd);
        } else {
          prompt += u;
          prompt += ' ';
          prompt += pwd;
          prompt += (u == "root") ? " # " : " $ ";
        }

        /* Ask for input until we get one. */
        for (;;) {
          auto [code, input] = toiletline::get_input(prompt);

          switch (code) {
          case TL_PRESSED_TAB:
            /* TODO. */
            shit::print_to_standard_output("^I");
            shit::flush_standard_output();
            toiletline::set_input(input);
            break;
          case TL_PRESSED_EOF:
            /* Exit on CTRL-D. */
            shit::print_to_standard_output("^D");
            shit::flush_standard_output();
            toiletline::emit_newlines(input);
            shit::utils::quit(exit_code, true);
            break;
          case TL_PRESSED_INTERRUPT:
            /* Ignore Ctrl-C. */
            shit::print_to_standard_output("^C");
            shit::flush_standard_output();
            break;
          case TL_PRESSED_SUSPEND:
            /* Ignore Ctrl-Z. */
            shit::print_to_standard_output("^Z");
            shit::flush_standard_output();
            break;
          }

          toiletline::emit_newlines(input);

          /* Execute the command without raw mode. */
          if (code == TL_PRESSED_ENTER && !input.empty()) {
            script_contents = input;
            break;
          }
        }

        toiletline::exit_raw_mode();
      } else {
        SHIT_UNREACHABLE();
      }
    } catch (const shit::Error &e) {
      shit::show_message(e.to_string());
      shit::utils::quit(EXIT_FAILURE);
    } catch (const std::exception &e) {
      shit::show_message(
          "Uncaught exception while getting the input. Exiting.");
      shit::show_message("Context: '" + std::string{e.what()} + "'.");
      shit::utils::quit(EXIT_FAILURE);
    } catch (...) {
      shit::show_message(
          "Unexpected system explosion while getting the input. Exiting.");
      shit::show_message("Last system message: " +
                         shit::os::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

    /* Execute the contents through the shared pipeline. */
    exit_code = run_script_contents(script_contents, context, ast_arena);

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

  SHIT_UNREACHABLE();
}
