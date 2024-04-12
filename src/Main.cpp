#include "Common.hpp"
#include "Expressions.hpp"
#include "Flags.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Utils.hpp"

#define TL_ASSERT INSIST
#define TOILETLINE_IMPLEMENTATION
#include "toiletline/toiletline.h"

#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

static FlagBool flag_help{'\0', "help", "Display help message."};
static FlagBool flag_version{'\0', "version", "Display program version."};
static FlagBool flag_dump_ast{'A', "dump-ast",
                              "Dump AST for debugging purposes."};
static FlagBool flag_exit_code{'e', "exit-code",
                               "Print exit code after each expression."};

static FlagString flag_command{'c', "command",
                               "Execute specified command and exit."};

static std::vector<Flag *> flags = {
    &flag_command, &flag_dump_ast, &flag_exit_code, &flag_help, &flag_version,
};

static void
show_version()
{
  std::cout
      << "Shit " << SHIT_VER_MAJOR << '.' << SHIT_VER_MINOR << '.'
      << SHIT_VER_PATCH << "\n"
      << "(c) toiletbril <https://github.com/toiletbril>\n\n"
         "License GPLv3: GNU GPL version 3.\n"
         "This is free software: you are free to change and redistribute it.\n"
         "There is NO WARRANTY, to the extent permitted by law."
      << std::endl;
}

static void
show_help(std::string_view program_name)
{
  std::string s;

  s += "Usage:\n";
  s += "  ";
  s += program_name;
  s += " [-options]";
  s += " [file1, ...]\n";
  s += "  ";
  s += "Command-line interpreter or shell.";
  s += "\n\n";

  s += "Options:";
  for (const Flag *f : flags) {
    s += "\n";
    bool has_short = false;
    bool long_is_string = false;
    if (f->short_name() != '\0') {
      s += "  -";
      s += f->short_name();
      has_short = true;
    }
    if (!f->long_name().empty()) {
      if (has_short)
        s += ", ";
      else
        s += "      ";
      s += "--";
      s += f->long_name();
      if (f->type() == FlagType::String) {
        s += "=<...>";
        long_is_string = true;
      }
    }
    usize padding = 24 - f->long_name().length() - (long_is_string ? 6 : 0);
    for (usize i = 0; i < padding; i++)
      s += ' ';
    s += f->description();
  }
  std::cout << s << std::endl;
}

static void
show_error(std::string_view err)
{
  std::cout << "shit: " << err << std::endl;
}

int
main(int argc, char **argv)
{
  std::vector<std::string> file_names;

  try {
    file_names = flag_parse(flags, argc, argv);
  } catch (Error &e) {
    show_error(e.to_string());
    return 1;
  }

  /* Program path is the first argument. Pull it out and get rid of it. */
  std::string program_path = file_names[0];
  file_names.erase(file_names.begin());

  if (flag_help.enabled()) {
    show_help(program_path);
    return 1;
  } else if (flag_version.enabled()) {
    show_version();
    return 0;
  }

  bool should_break = false;
  bool error_happened = false;
  bool toiletline_initialized = false;

  usize arg_index = 0;
  for (;;) {
    std::string contents;

    /* If we weren't given any arguments or -c=..., fire up the toiletline. */
    if (file_names.empty() && flag_command.contents().empty()) {
      if (!toiletline_initialized) {
        if (tl_init() != TL_SUCCESS) {
          show_error("Could not initialize toiletline. If you meant use stdin, "
                     "provide '-' as an argument.");
          error_happened = true;
          break;
        }
        toiletline_initialized = true;
      }

      static constexpr usize PWD_LENGTH = 24;

      std::string prompt;
      std::string pwd = shit_current_directory();
      if (pwd.length() > PWD_LENGTH) {
        pwd = "..." + pwd.substr(pwd.length() - PWD_LENGTH + 3);
      }
      prompt += pwd;
      prompt += " shit> ";

      char buffer[2048];
      int  code = tl_readline(buffer, sizeof(buffer), prompt.c_str());
      INSIST(!shit_process_is_child());

      if (code == TL_PRESSED_EOF || code == TL_PRESSED_INTERRUPT) {
        tl_exit();
        std::cout << "exit" << std::endl;
        break;
      }

      contents = buffer;
      std::cout << "\n";
      if (contents.empty())
        continue;
    } else if (!flag_command.contents().empty()) { /* Were we given -c flag? */
      contents = flag_command.contents();
      should_break = true;
    } else {
      if (arg_index + 1 == file_names.size())
        should_break = true;

      std::fstream  f{};
      std::istream *file{};

      /* When file name is "-", use stdin. */
      if (file_names[arg_index] == "-") {
        file = &std::cin;
      } else { /* Otherwise, process the actual file name. */
        f = std::fstream{file_names[arg_index], f.in | f.binary};
        if (!f.is_open()) {
          show_error("Could not open '" + file_names[arg_index] + "'");
          return 1;
        }
        file = &f;
      }

      for (;;) {
        uchar ch = file->get();
        if (file->eof())
          break;
        contents += ch;
      }

      arg_index++;
    }

    error_happened = true;
    try {
      std::unique_ptr<Parser> p = std::make_unique<Parser>(new Lexer{contents});
      std::unique_ptr<Expression> ast = p->construct_ast();

      if (flag_dump_ast.enabled())
        std::cout << ast->to_ast_string() << std::endl;

      i32 exit_code = ast->evaluate();
      if (flag_exit_code.enabled())
        std::cout << exit_code << std::endl;

      error_happened = false;
    } catch (ErrorWithLocationAndDetails &e) {
      show_error(e.to_string(contents));
      show_error(e.details_to_string(contents));
    } catch (ErrorWithLocation &e) {
      show_error(e.to_string(contents));
    }

    /* We can get here from child process if they didn't platform_exec()
     * properly to print error. */
    if (should_break || shit_process_is_child())
      break;
  }

  return error_happened;
}
