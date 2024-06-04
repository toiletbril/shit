#include "Cli.hpp"
#include "Common.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

static std::vector<shit::Flag *> FLAG_LIST{};

#define FLAG(...) ADD_FLAG(FLAG_LIST, __VA_ARGS__)

FLAG(DUMP_AST, Bool, 'A', "dump-ast", "Dump AST for debugging purposes.");
FLAG(EXIT_CODE, Bool, 'e', "exit-code", "Print exit code after each command.");
FLAG(COMMAND, String, 'c', "command", "Execute specified command and exit.");
FLAG(HELP, Bool, '\0', "help", "Display help message.");
FLAG(VERSION, Bool, '\0', "version", "Display program version.");

int
main(int argc, char **argv)
{
  std::vector<std::string> file_names;

  try {
    file_names = shit::parse_flags(FLAG_LIST, argc, argv);
  } catch (shit::Error &e) {
    shit::show_error(e.to_string());
    return EXIT_SUCCESS;
  }

  /* Program path is the first argument. Pull it out and get rid of it. */
  std::string program_path = file_names[0];
  file_names.erase(file_names.begin());

  if (FLAG_HELP.enabled()) {
    shit::show_help(program_path, FLAG_LIST);
    return EXIT_SUCCESS;
  } else if (FLAG_VERSION.enabled()) {
    shit::show_version();
    return EXIT_SUCCESS;
  }

  bool should_quit = false;

  usize arg_index = 0;
  int   exit_code = EXIT_SUCCESS;

  /* Clear and set up cache. Don't prematurely initialize the whole path map,
   * since it's only really noticeable in interactive mode. This way,
   * subsequent calls to the same program will still be cached in any mode. */
  shit::utils::clear_path_map();

  /* A simple return cannot be used after this point, since we need a special
   * cleanup for toiletline. utils::quit() should be used instead. */
  for (;;) {
    SHIT_ASSERT(!shit::utils::is_child_process());

    std::string contents;

    /* Figure out what to do and retrieve the code. */
    try {
      /* If we weren't given any arguments or -c=..., fire up the toiletline. */
      if (file_names.empty() && FLAG_COMMAND.contents().empty()) {
        if (!toiletline::is_active()) {
          shit::utils::set_default_signal_handlers();
          shit::utils::initialize_path_map();
          toiletline::initialize();
        } else {
          /* NOTE: avoid this branch if exit_raw_mode() wasn't called
           * previosly! */
          toiletline::enter_raw_mode();
        }

        static constexpr usize PWD_LENGTH = 24;

        /* shit % ...wd1/pwd2/pwd3/pwd4/pwd5 $ command */
        std::string prompt = "shit % ";
        std::string pwd = shit::utils::get_current_directory().string();
        if (pwd.length() > PWD_LENGTH) {
          pwd = "..." + pwd.substr(pwd.length() - PWD_LENGTH + 3);
        }
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
            shit::utils::quit(exit_code);
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
            contents = input;
            break;
          }
        }

        toiletline::exit_raw_mode();
      } else if (!FLAG_COMMAND.contents().empty()) {
        /* Were we given -c flag? */
        contents = FLAG_COMMAND.contents();
        should_quit = true;
      } else {
        /* Were we given a list of files? */
        if (arg_index + 1 == file_names.size())
          should_quit = true;

        std::fstream  f{};
        std::istream *file{};

        /* When file name is "-", use stdin. */
        if (file_names[arg_index] == "-") {
          file = &std::cin;
        } else { /* Otherwise, process the actual file name. */
          f = std::fstream{file_names[arg_index], f.in | f.binary};
          if (!f.is_open()) {
            throw shit::Error{"Could not open '" + file_names[arg_index] +
                              "': " + shit::utils::last_system_error_message()};
          }
          file = &f;
        }

        for (;;) {
          char ch = file->get();
          if (file->bad()) {
            throw shit::Error{"Could not read '" + file_names[arg_index] +
                              "': " + shit::utils::last_system_error_message()};
          }
          if (file->eof())
            break;
          contents += ch;
        }

        arg_index++;
      }
    } catch (shit::Error &e) {
      shit::show_error(e.to_string());
      shit::utils::quit(EXIT_FAILURE);
    } catch (...) {
      shit::show_error("Could not figure out what to do due to an unexpected "
                       "explosion! Last system message: " +
                       shit::utils::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

    exit_code = EXIT_FAILURE;

    /* Execute the contents. */
    try {
      std::unique_ptr<shit::Parser> p =
          std::make_unique<shit::Parser>(new shit::Lexer{contents});

      std::unique_ptr<shit::Expression> ast = p->construct_ast();
      if (FLAG_DUMP_AST.enabled())
        std::cout << ast->to_ast_string() << std::endl;

      exit_code = ast->evaluate();
      if (FLAG_EXIT_CODE.enabled())
        std::cout << "[Code " << exit_code << "]" << std::endl;
    } catch (shit::ErrorWithLocationAndDetails &e) {
      shit::show_error(e.to_string(contents));
      shit::show_error(e.details_to_string(contents));
    } catch (shit::ErrorWithLocation &e) {
      shit::show_error(e.to_string(contents));
    } catch (...) {
      shit::show_error("Could not execute the code due to an unexpected "
                       "explosion! Last system message: " +
                       shit::utils::last_system_error_message());
      shit::utils::quit(EXIT_FAILURE);
    }

    /* We can get here from child process if they didn't exec()
     * properly to print error. */
    if (should_quit || shit::utils::is_child_process())
      shit::utils::quit(exit_code);
  }

  SHIT_UNREACHABLE();
}
