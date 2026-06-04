#pragma once

#include "Expressions.hpp"
#include "Lexer.hpp"

#include <initializer_list>

namespace shit {

using namespace expressions;

struct Parser
{
  Parser(Lexer &&lexer);
  ~Parser();

  std::unique_ptr<Expression> construct_ast();
  const std::vector<Word> &debug_words() const;

private:
  static constexpr usize MAX_RECURSION_DEPTH = 64;

  Lexer m_lexer;

  usize m_recursion_depth{0};
  usize m_if_condition_depth{0};
  usize m_parentheses_depth{0};

  [[nodiscard]] std::unique_ptr<Command> parse_simple_command();

  /* Build a command list until a terminator keyword is peeked, leaving it for
     the caller. The control-structure parsers call this for their inner lists.
   */
  [[nodiscard]] std::unique_ptr<Expression>
  parse_command_list(std::initializer_list<Token::Kind> terminators);

  [[nodiscard]] std::unique_ptr<Command> parse_if();
  [[nodiscard]] std::unique_ptr<Command> parse_while_or_until(bool is_until);
  [[nodiscard]] std::unique_ptr<Command> parse_for();
  [[nodiscard]] std::unique_ptr<Command> parse_case();
  [[nodiscard]] std::unique_ptr<Command> parse_brace_group();
  [[nodiscard]] std::unique_ptr<Command> parse_subshell();
  [[nodiscard]] std::unique_ptr<Command>
  parse_function_definition(std::unique_ptr<Token> name_token);

  [[nodiscard]] std::unique_ptr<Expression>
  parse_expression(u8 min_precedence = 0);
};

} /* namespace shit */
