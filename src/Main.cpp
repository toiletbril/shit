#include "Common.hpp"
#include "Expressions.hpp"
#include "Flags.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

FlagBool   flag_help{'\0', "help", "Display help message."};
FlagBool   flag_dump_ast{'A', "dump-ast", "Dump AST."};
FlagString flag_command{'c', "command", "Execute specified command string."};

std::vector<Flag *> flags = {
    &flag_dump_ast,
    &flag_command,
    &flag_help,
};

void
show_help(std::string_view program_name)
{
  std::string s;

  s += "Usage:\n";
  s += "  ";
  s += program_name;
  s += " [-options]";
  s += " [args...]\n";
  s += "Shit, command-line interpreter or shell";
  s += "\n\n";

  s += "Options:";
  for (const Flag *f : flags) {
    s += "\n";
    bool has_short = false;
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
    }
    for (usize i = 0; i < 16 - f->long_name().length(); i++)
      s += ' ';
    s += f->description();
  }
  std::cout << s << std::endl;
  std::exit(1);
}

int
main(int argc, char **argv)
{
  std::vector<std::string> file_names;

  try {
    file_names = flag_parse(flags, argc, argv);
  } catch (Error &e) {
    std::cout << e.to_string() << std::endl;
    return 1;
  }

  std::string program_path = file_names[0];
  file_names.erase(file_names.begin());

  if (file_names.size() == 0 && flag_command.get().empty()) {
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

  usize arg_index = 0;
  bool  should_break = false;

  for (;;) {
    std::string contents;

    if (flag_command.get().empty()) {
      if (arg_index + 1 == file_names.size())
        should_break = true;

      if (file_names[arg_index] != "-") {
        std::fstream f{file_names[arg_index], f.in | f.binary};
        if (!f.is_open()) {
          std::cout << "Error: could not open '" + file_names[arg_index] + "'"
                    << std::endl;
          return 1;
        }

        for (;;) {
          uchar ch = f.get();
          if (f.eof())
            break;
          contents += ch;
        }
      } else {
        for (;;) {
          uchar ch = std::cin.get();
          if (std::cin.eof())
            break;
          contents += ch;
        }
      }
      arg_index++;
    } else {
      contents = flag_command.get();
      should_break = true;
    }

    try {
      std::unique_ptr<Parser> p = std::make_unique<Parser>(new Lexer{contents});
      std::unique_ptr<Expression> ast = p->construct_ast();
      if (flag_dump_ast.get())
        std::cout << ast->to_ast_string() << std::endl;
      std::cout << ast->evaluate() << std::endl;
    } catch (ErrorWithLocation &e) {
      std::cout << e.to_string(contents) << std::endl;
      return 1;
    }

    if (should_break)
      break;
  }

  return 0;
}
