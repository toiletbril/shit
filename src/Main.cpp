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

/* Total bullcrap. */
FLAG(IGNORED1, Bool, 'h', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", "Ignored, left for compatibility.");
FLAG(IGNORED3, Bool, 'u', "\0", "Ignored, left for compatibility.");

FLAG(LOGIN, Bool, 'l', "login", "Act as a login shell.");
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

int
main(int argc, char **argv)
{
  bool                     is_login_shell = false;
  std::vector<std::string> file_names{};

  try {
    file_names = shit::parse_flags(FLAG_LIST, argc, argv);
  } catch (shit::Error &e) {
    shit::show_message(e.to_string());
    return EXIT_SUCCESS;
  }

  /* Program path is the first argument. Pull it out and get rid of it. */
  std::string program_path{};

  if (file_names.size() > 0) {
    program_path = file_names[0];
    file_names.erase(file_names.begin());
    if (program_path == "-") is_login_shell = true;
  } else {
    program_path = "<unknown path>";
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

  if (FLAG_LOGIN.is_enabled() || strcmp(argv[0], "-") == 0)
    is_login_shell = true;

  if (FLAG_STDIN.is_enabled() && FLAG_INTERACTIVE.is_enabled()) {
    shit::show_message(
        "Both '-s' and '-i' options are specified. Falling back to '-i'.");
    FLAG_STDIN.toggle();
  }

  /* Figure out what to do. Note that "-c" can be specified multiple times.
   * Option precedence should behave as follows: "-s", then files (arguments),
   * then "-i" (or no arguments). */
  bool should_read_files = !file_names.empty();
  bool should_execute_commands = !FLAG_COMMAND.is_empty();
  bool should_be_interactive =
      (!should_execute_commands && FLAG_INTERACTIVE.is_enabled()) ||
      (!should_read_files && shit::os::is_stdin_a_tty());
  bool should_read_stdin =
      (!should_be_interactive && !should_read_files) || FLAG_STDIN.is_enabled();

  if (FLAG_EXPORT_ALL.is_enabled() || FLAG_NO_CLOBBER.is_enabled()) {
    shit::show_message("One or more unimplemented options were ignored.");
  }

  /* Main loop state. */
  shit::EvalContext context{FLAG_DISABLE_EXPANSION.is_enabled(),
                            FLAG_VERBOSE.is_enabled(),
                            FLAG_EXPAND_VERBOSE.is_enabled()};

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
        toiletline::set_title("shit % " + pwd);

        if (pwd.length() > PWD_LENGTH)
          pwd = "..." + pwd.substr(pwd.length() - PWD_LENGTH + 3);

        /* shit % ...wd1/pwd2/pwd3/pwd4/pwd5 $ command */
        std::string prompt{};
        prompt += "shit % ";
        prompt += pwd;
        prompt += " $ ";

        static constexpr usize TOILETLINE_BUFFER_SIZE = 2048;

        /* Ask for input until we get one. */
        for (;;) {
          auto [code, input] =
              toiletline::readline(TOILETLINE_BUFFER_SIZE, prompt);

          if (code == TL_PRESSED_EOF) {
            /* Exit on CTRL-D. */
            std::cout << "^D" << std::flush;
            toiletline::emit_newlines(input);
            shit::utils::quit(exit_code, true);
          } else if (code == TL_PRESSED_INTERRUPT) {
            /* Ignore Ctrl-C. */
            std::cout << "^C" << std::flush;
          } else if (code == TL_PRESSED_SUSPEND) {
            /* Ignore Ctrl-Z. */
            std::cout << "^Z" << std::flush;
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
    } catch (shit::Error &e) {
      shit::show_message(e.to_string());
      shit::utils::quit(EXIT_FAILURE);
    } catch (std::exception &e) {
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
    } catch (shit::ErrorWithLocationAndDetails &e) {
      shit::show_message(e.to_string(script_contents));
      shit::show_message(e.details_to_string(script_contents));
    } catch (shit::ErrorWithLocation &e) {
      shit::show_message(e.to_string(script_contents));
    } catch (shit::Error &e) {
      shit::show_message(e.to_string());
    } catch (std::exception &e) {
      shit::show_message("Uncaught std::exception while executing the AST.");
      shit::show_message("what(): " + std::string{e.what()});
    } catch (...) {
      shit::show_message(
          "Unexpected system explosion while executing the AST.");
      shit::show_message("Last system message: " +
                         shit::os::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

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
