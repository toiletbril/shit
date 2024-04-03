#include "Common.hpp"
#include "Expressions.hpp"
#include "Flags.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

#define TOILETLINE_IMPLEMENTATION
#include "toiletline/toiletline.h"

#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

static FlagBool flag_help{'\0', "help", "Display help message."};
static FlagBool flag_dump_ast{'A', "dump-ast",
                              "Dump AST for debugging purposes."};

static FlagString flag_command{'c', "command",
                               "Execute specified command and exit."};

static std::vector<Flag *> flags = {
    &flag_command,
    &flag_dump_ast,
    &flag_help,
};

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
  s += "Shit, command-line interpreter or shell.";
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

int
main(int argc, char **argv)
{
  std::vector<std::string> file_names;

  try {
    file_names = flag_parse(flags, argc, argv);
  } catch (Error &e) {
    std::cout << "shit: " << e.to_string() << std::endl;
    return 1;
  }

  std::string program_path = file_names[0];
  file_names.erase(file_names.begin());

  if (flag_help.enabled()) {
    show_help(program_path);
    return 1;
  }

  std::string input;

  bool first_arg = true;
  for (std::string_view s : file_names) {
    if (!first_arg)
      input += ' ';
    input += s;
    first_arg = false;
  }

  bool should_break = false;
  bool error_happened = false;
  bool tl_initialized = false;

  usize arg_index = 0;
  for (;;) {
    std::string contents;

    if (file_names.empty() && flag_command.contents().empty()) {
      /* should use the shell? */
      if (!tl_initialized) {
        if (tl_init() != TL_SUCCESS) {
          std::cout
              << "shit: Could initialize toiletline. If you meant use stdin, "
                 "provide '-' as an argument."
              << std::endl;
          error_happened = true;
          break;
        }
        tl_initialized = true;
      }

      char buffer[1024];
      int  code = tl_readline(buffer, sizeof(buffer), "tl> ");

      if (code == TL_PRESSED_EOF || code == TL_PRESSED_INTERRUPT) {
        tl_exit();
        std::cout << "exit" << std::endl;
        break;
      }

      contents = buffer;
      std::cout << "\n";
      if (contents.empty())
        continue;
    } else if (!flag_command.contents().empty()) {
      /* should use the -c flag? */
      contents = flag_command.contents();
      should_break = true;
    } else {
      if (arg_index + 1 == file_names.size())
        should_break = true;

      /* use stdin if file is - */
      if (file_names[arg_index] == "-") {
        for (;;) {
          uchar ch = std::cin.get();
          if (std::cin.eof())
            break;
          contents += ch;
        }
      } else {
        /* or we were given actual file names */
        std::fstream f{file_names[arg_index], f.in | f.binary};
        if (!f.is_open()) {
          std::cout << "shit: Could not open '" + file_names[arg_index] + "'"
                    << std::endl;
          return 1;
        }

        for (;;) {
          uchar ch = f.get();
          if (f.eof())
            break;
          contents += ch;
        }
      }
      arg_index++;
    }

    try {
      std::unique_ptr<Parser> p = std::make_unique<Parser>(new Lexer{contents});
      std::unique_ptr<Expression> ast = p->construct_ast();
      if (flag_dump_ast.enabled())
        std::cout << ast->to_ast_string() << std::endl;
      std::cout << ast->evaluate() << std::endl;
      error_happened = false;
    } catch (ErrorWithLocation &e) {
      std::cout << "shit: " << e.to_string(contents) << std::endl;
      error_happened = true;
    }

    if (should_break)
      break;
  }

  return error_happened;
}
