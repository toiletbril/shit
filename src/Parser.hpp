#pragma once

#include "Containers.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"

#include <initializer_list>

namespace shit {

using namespace expressions;

struct Parser
{
  Parser(Lexer &&lexer);
  ~Parser();

  fn construct_ast() -> std::unique_ptr<Expression>;
  fn debug_words() const -> const ArrayList<Word> &;

private:
  static constexpr usize MAX_RECURSION_DEPTH = 64;

  Lexer m_lexer;

  usize m_recursion_depth{0};
  usize m_if_condition_depth{0};
  usize m_parentheses_depth{0};

  [[nodiscard]] fn parse_simple_command() -> std::unique_ptr<Command>;

  /* Build a command list until a terminator keyword is peeked, leaving it for
     the caller. The control-structure parsers call this for their inner lists.
   */
  [[nodiscard]] fn
  parse_command_list(std::initializer_list<Token::Kind> terminators)
      -> std::unique_ptr<Expression>;

  [[nodiscard]] fn parse_if() -> std::unique_ptr<Command>;
  [[nodiscard]] fn parse_while_or_until(bool is_until)
      -> std::unique_ptr<Command>;
  [[nodiscard]] fn parse_for() -> std::unique_ptr<Command>;
  [[nodiscard]] fn parse_case() -> std::unique_ptr<Command>;
  [[nodiscard]] fn parse_brace_group() -> std::unique_ptr<Command>;
  [[nodiscard]] fn parse_subshell() -> std::unique_ptr<Command>;
  [[nodiscard]] fn parse_function_definition(std::unique_ptr<Token> name_token)
      -> std::unique_ptr<Command>;

  [[nodiscard]] fn parse_expression(u8 min_precedence = 0)
      -> std::unique_ptr<Expression>;
};

} /* namespace shit */
