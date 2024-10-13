#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
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

/* Total bullcrap. */
FLAG(IGNORED1, Bool, 'h', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED3, Bool, 'u', "\0", "Ignored, left for compatibility.");

FLAG(EXIT_CODE, Bool, 'E', "exit-code", "Print exit code after each command.");
FLAG(ESCAPE_MAP, Bool, 'M', "escape-map",
     "Print escape map after each command parsed.");
FLAG(STATS, Bool, 'S', "stats",
     "Print statistics after each command executed.");
FLAG(DUMP_AST, Bool, 'A', "dump-ast",
     "Dump AST before executing each command.");

FLAG(VERSION, Bool, '\0', "version", "Display program version and notices.");
FLAG(SHORT_VERSION, Bool, 'V', "short-version",
     "Display version in a short form.");
FLAG(HELP, Bool, '\0', "help", "Display help message.");

#if SHIT_USING_COSMO
FLAG(COSMO_FTRACE, Bool, '\0', "ftrace", "Cosmopolitan: Trace functions.");
FLAG(COSMO_STRACE, Bool, '\0', "trace", "Cosmopolitan: Trace system calls.");
#endif

int
main(int argc, char **argv)
{
#if SHIT_USING_COSMO
  ShowCrashReports();
  SHIT_UNUSED(FLAG_COSMO_FTRACE);
  SHIT_UNUSED(FLAG_COSMO_STRACE);
#endif

  bool                     is_login_shell = false;
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

  /* Figure out what to do. Note that "-c" can be specified multiple times.
   * Option precedence should behave as follows: "-s", then "-c", then files
   * (arguments), then "-i" (or no arguments). */
  bool should_read_stdin =
      FLAG_STDIN.is_enabled() || !shit::os::is_stdin_a_tty();
  bool should_execute_commands = !should_read_stdin && !FLAG_COMMAND.is_empty();
  bool should_read_files = !file_names.empty() && !should_execute_commands;
  bool should_be_interactive =
      !should_read_files &&
      (FLAG_INTERACTIVE.is_enabled() || shit::os::is_stdin_a_tty());

  if (FLAG_STDIN.is_enabled() &&
      (!FLAG_COMMAND.is_empty() || !file_names.empty() ||
       FLAG_INTERACTIVE.is_enabled()))
  {
    shit::show_message("Incompatible options or arguments were specified along "
                       "with '-s' option. "
                       "Falling back to '-s'.");
  } else if (!FLAG_COMMAND.is_empty() &&
             (!file_names.empty() || FLAG_INTERACTIVE.is_enabled()))
  {
    shit::show_message("Incompatible options or arguments were specified along "
                       "with '-c' options. "
                       "Falling back to '-c'.");
  } else if (!file_names.empty() && FLAG_INTERACTIVE.is_enabled()) {
    shit::show_message("Both file argument and '-i' option were given. "
                       "Falling back to reading files.");
  }

  if (FLAG_EXPORT_ALL.is_enabled() || FLAG_NO_CLOBBER.is_enabled())
    shit::show_message("One or more unimplemented options were ignored.");

  /* Main loop state. */
  shit::EvalContext context{
      FLAG_DISABLE_EXPANSION.is_enabled(), FLAG_VERBOSE.is_enabled(),
      FLAG_EXPAND_VERBOSE.is_enabled(), should_be_interactive};

  usize arg_index = 0;
  bool  should_quit = FLAG_ONE_COMMAND.is_enabled() ? true : false;
  int   exit_code = EXIT_SUCCESS;

  /* Clear and set up cache. Don't prematurely initialize the whole path map,
   * since it's only really noticeable in interactive mode. This way,
   * subsequent calls to the same program will still be cached in any mode, but
   * we won't waste any milliseconds traversing directories for very simple
   * scripts! */
  shit::utils::clear_path_map();
  shit::os::set_default_signal_handlers();

  if (is_login_shell) {
    /* TODO: We can't really execute complex scripts yet. From 'man dash':
     * A login shell first reads commands from the files /etc/profile and
     * .profile if they exist.  If the environment variable ENV is set on entry
     * to an interactive shell, or is set in the .profile of a login shell, the
     * shell next reads commands from the file named in ENV. */
    shit::show_message("Acting as a login shell is not supported yet. "
                       "Please bear with me!");
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
        std::fstream  f{};
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

        std::string pwd = shit::utils::get_current_directory().string();
        toiletline::set_title("shit @ " + pwd);

        if (pwd.length() > PWD_LENGTH)
          pwd = "..." + pwd.substr(pwd.length() - PWD_LENGTH + 3);

        std::string u = shit::os::get_current_user().value_or("???");

        /* shit % ...wd1/pwd2/pwd3/pwd4/pwd5 $ command */
        std::string prompt{};
        prompt += u;
        prompt += ' ';
        prompt += pwd;
        prompt += (u == "root") ? " # " : " $ ";

        static constexpr usize TOILETLINE_BUFFER_SIZE = 2048;

        /* Ask for input until we get one. */
        for (;;) {
          auto [code, input] =
              toiletline::readline(TOILETLINE_BUFFER_SIZE, prompt);

          switch (code) {
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
      shit::show_message("Uncaught std::exception while getting the input.");
      shit::show_message("what(): " + std::string{e.what()});
      shit::utils::quit(EXIT_FAILURE);
    } catch (...) {
      shit::show_message(
          "Unexpected system explosion while getting the input.");
      shit::show_message("Last system message: " +
                         shit::os::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

    exit_code = EXIT_FAILURE;
    /* Shut up the compiler. */
    SHIT_UNUSED(exit_code);

    /* Execute the contents. */
    try {
      shit::Parser p{shit::Lexer{script_contents}};

      std::unique_ptr<shit::Expression> ast = p.construct_ast();

      if (FLAG_DUMP_AST.is_enabled()) {
        std::cout << ast->to_ast_string() << std::endl;
      }

      if (FLAG_ESCAPE_MAP.is_enabled()) {
        std::cout << "[Escape Map\n  " << p.escape_map().to_string() << "\n]"
                  << std::endl;
      }

      context.steal_escape_map(std::move(p.escape_map()));
      exit_code = ast->evaluate(context);

      if (FLAG_EXIT_CODE.is_enabled())
        std::cout << "[Code " << exit_code << ']' << std::endl;

      if (FLAG_STATS.is_enabled())
        std::cout << context.make_stats_string() << std::endl;

      context.end_command();
    } catch (const shit::ErrorWithLocationAndDetails &e) {
      shit::show_message(e.to_string(script_contents));
      shit::show_message(e.details_to_string(script_contents));
    } catch (const shit::ErrorWithLocation &e) {
      shit::show_message(e.to_string(script_contents));
    } catch (const shit::Error &e) {
      shit::show_message(e.to_string());
    } catch (const std::exception &e) {
      shit::show_message("Uncaught std::exception while executing the AST.");
      shit::show_message("what(): " + std::string{e.what()});
    } catch (...) {
      shit::show_message(
          "Unexpected system explosion while executing the AST.");
      shit::show_message("Last system message: " +
                         shit::os::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

    /* TODO: Make ExecutionErrorWithLocation to distinguish execution errors?
     * Or statically check commands before they are executed? */

    /* We can get here from child process if they didn't exec()
     * properly to print error. */
    if (should_quit || shit::os::is_child_process() ||
        (FLAG_ERROR_EXIT.is_enabled() && exit_code != 0))
    {
      shit::utils::quit(exit_code);
    }
  }

  SHIT_UNREACHABLE();
}
