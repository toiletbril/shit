#include "common.hpp"
#include "expr.hpp"
#include "lex.hpp"
#include "parse.hpp"
#include "trace.hpp"
#include "types.hpp"

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

  /* Merge CLI args. */
  std::string input = argv[1];
  for (int i = 2; i < argc; ++i) {
    input += ' ';
    input += argv[i];
  }
  
  try {
    Parser *p = new Parser{new Lexer{input}};
    Expression *ast = p->construct_ast();
    std::cout << "AST:\n" << ast->to_ast_string() << std::endl;
    std::cout << "Result:\n" <<ast->evaluate() << std::endl;
    delete p;
    delete ast;
  } catch (Error &e) {
    std::cout << e.msg() << std::endl;
  }

  return 0;
}
