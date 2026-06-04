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
#include <fstream>
#include <iostream>
#include <iterator>
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

/* TODO: */
FLAG(EXPORT_ALL, Bool, 'a', "export-all",
     "UNIMPLEMENTED: Export all variables assigned to.");
FLAG(NO_CLOBBER, Bool, 'C', "no-clobber",
     "UNIMPLEMENTED: Don't overwrite existing files with '>'.");
FLAG(LOGIN, Bool, 'l', "login", "UNIMPLEMENTED: Act as a login shell.");

FLAG(IGNORED1, Bool, 'h', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED3, Bool, 'u', "\0", "Ignored, left for compatibility.");

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

    shit::Parser p{
        shit::Lexer{script_contents, ast_arena, FLAG_ESCAPE_MAP.is_enabled()}
    };
    std::unique_ptr<shit::Expression> ast = p.construct_ast();

    if (FLAG_AST.is_enabled()) std::cout << ast->to_ast_string() << std::endl;

    if (FLAG_ESCAPE_MAP.is_enabled()) {
      for (const auto &word : p.debug_words())
        std::cout << word.to_pretty_string() << std::endl;
    }

    /* Validate the whole tree before running anything. An unconditional
       problem stops execution, a conditional one only warns. */
    if (!shit::analyze_ast(ast.get(), script_contents,
                           context.function_names())) {
      exit_code = EXIT_FAILURE;
    } else {
      exit_code = static_cast<int>(ast->evaluate(context));
    }
    context.set_last_exit_status(static_cast<i32>(exit_code));

    if (FLAG_EXIT_CODE.is_enabled())
      std::cout << "[Code " << exit_code << ']' << std::endl;

    if (FLAG_STATS.is_enabled())
      std::cout << context.make_stats_string() << std::endl;
  } catch (const shit::LoopControl &) {
    /* A break or continue that escaped every loop. The Error formats the label
       and show_message adds the shit prefix, matching every other error. */
    shit::show_message(
        shit::Error{"'break' or 'continue' used outside of a loop"}
            .to_string());
  } catch (const shit::FunctionReturn &) {
    /* A return that escaped every function. */
    shit::show_message(
        shit::Error{"'return' used outside of a function"}.to_string());
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
  std::fstream f{path, std::fstream::in | std::fstream::binary};
  if (!f.is_open()) return;

  std::string contents{std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>()};
  run_script_contents(contents, context, ast_arena);
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
      std::optional<std::filesystem::path> home = shit::os::get_home_directory();
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
    std::cerr << h << std::endl;
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

  if (FLAG_EXPORT_ALL.is_enabled() || FLAG_NO_CLOBBER.is_enabled())
    shit::show_message("One or more unimplemented options were ignored.");

  /* Main loop state. The program name is $0 and the remaining arguments are the
     positional parameters $1 upward. */
  shit::EvalContext context{FLAG_DISABLE_EXPANSION.is_enabled(),
                            FLAG_VERBOSE.is_enabled(),
                            FLAG_EXPAND_VERBOSE.is_enabled(),
                            should_be_interactive,
                            FLAG_ERROR_EXIT.is_enabled(),
                            program_path,
                            file_names};

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
    if (std::optional<std::filesystem::path> home =
            shit::os::get_home_directory();
        home.has_value())
    {
      source_file(*home / ".profile", context, ast_arena);
    }
    if (std::optional<std::string> env = context.get_variable_value("ENV");
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
        /* Were we given a list of files or "-s" flag? */
        std::fstream f{};
        std::istream *file{};

        /* If "-s" is used, or when file name is "-", use stdin. */
        if (should_read_stdin || file_names[arg_index] == "-") {
          /* Exit if "-s" is present. */
          should_quit = should_quit || should_read_stdin;
          file = &std::cin;
        } else {
          /* Otherwise, process the actual file name. */
          f = std::fstream{file_names[arg_index],
                           std::fstream::in | std::fstream::binary};

          if (!f.is_open())
            throw shit::Error{"Could not open '" + file_names[arg_index] +
                              "': " + shit::os::last_system_error_message()};

          file = &f;
        }

        for (;;) {
          char ch = file->get();
          if (file->bad()) {
            throw shit::Error{"Could not read '" + file_names[arg_index] +
                              "': " + shit::os::last_system_error_message()};
          } else if (file->eof()) {
            break;
          }
          script_contents += ch;
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
        if (std::optional<std::string> ps1 = context.get_variable_value("PS1");
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
            std::cout << "^I" << std::flush;
            toiletline::set_input(input);
            break;
          case TL_PRESSED_EOF:
            /* Exit on CTRL-D. */
            std::cout << "^D" << std::flush;
            toiletline::emit_newlines(input);
            shit::utils::quit(exit_code, true);
            break;
          case TL_PRESSED_INTERRUPT:
            /* Ignore Ctrl-C. */
            std::cout << "^C" << std::flush;
            break;
          case TL_PRESSED_SUSPEND:
            /* Ignore Ctrl-Z. */
            std::cout << "^Z" << std::flush;
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
      shit::utils::quit(exit_code, FLAG_ERROR_EXIT.is_enabled());
    }
  }

  SHIT_UNREACHABLE();
}
