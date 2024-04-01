#include "Common.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

#include <iostream>

[[noreturn]] void
show_help()
{
  std::cout << "error: no input" << std::endl;
  std::exit(1);
}

int
main(int argc, char **argv)
{
  if (argc <= 1)
    show_help();

  std::string input = argv[1];
  for (int i = 2; i < argc; ++i) {
    input += ' ';
    input += argv[i];
  }

  try {
    std::unique_ptr<Parser> p = std::make_unique<Parser>(new Lexer{input});
    std::unique_ptr<Expression> ast = p->construct_ast();
    std::cout << ast->to_ast_string() << std::endl;
    std::cout << ast->evaluate() << std::endl;
  } catch (ErrorBase *e) {
    std::cout << e->msg() << std::endl;
    delete e;
  }

  return 0;
}
