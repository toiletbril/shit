#pragma once

#include "Expressions.hpp"
#include "Lexer.hpp"

namespace shit {

struct Parser
{
  Parser(Lexer &&lexer);
  ~Parser();

  std::unique_ptr<Expression> construct_ast();
  EscapeMap                  &escape_map();

private:
  static constexpr usize MAX_RECURSION_DEPTH = 64;

  Lexer m_lexer;

  usize m_recursion_depth{0};
  usize m_if_condition_depth{0};
  usize m_parentheses_depth{0};

  [[nodiscard]] std::unique_ptr<expressions::SimpleCommand>
                                        parse_simple_command();
  std::unique_ptr<expressions::Command> parse_compound_command(const Token *t);
  [[nodiscard]] std::unique_ptr<Expression> parse();
  [[nodiscard]] std::unique_ptr<Expression>
  parse_expression(u8 min_precedence = 0);
};

} /* namespace shit */
