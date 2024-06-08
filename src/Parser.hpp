#pragma once

#include "Expressions.hpp"
#include "Lexer.hpp"

#include <optional>

namespace shit {

struct Parser
{
  Parser(Lexer *lexer);
  ~Parser();

  std::unique_ptr<Expression> construct_ast();

  static constexpr usize MAX_RECURSION_DEPTH = 64;

private:
  Lexer *m_lexer;

  usize m_recursion_depth{0};
  usize m_if_condition_depth{0};
  usize m_parentheses_depth{0};

  [[nodiscard]] std::unique_ptr<Expression>          parse_command();
  [[nodiscard]] std::optional<std::unique_ptr<Exec>> parse_exec();
  [[nodiscard]] std::unique_ptr<Expression>
  parse_expression(u8 min_precedence = 0);
};

} /* namespace shit */
