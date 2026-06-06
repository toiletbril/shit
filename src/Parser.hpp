#pragma once

#include "Containers.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"

#include <initializer_list>

namespace shit {

using namespace expressions;

class Parser
{
public:
  Parser(Lexer &&lexer);
  ~Parser();

  fn construct_ast() throws -> Expression *;
  pure fn debug_words() const wontthrow -> const ArrayList<Word> &;

private:
  static constexpr usize MAX_RECURSION_DEPTH = 64;

  Lexer m_lexer;

  usize m_recursion_depth{0};
  usize m_if_condition_depth{0};
  usize m_parentheses_depth{0};

  mustuse fn parse_simple_command() throws -> Command *;

  /* Build a command list until a terminator keyword is peeked, leaving it for
     the caller. The control-structure parsers call this for their inner lists.
   */
  mustuse fn parse_command_list(
      std::initializer_list<Token::Kind> terminators) throws -> Expression *;

  mustuse fn parse_if() throws -> Command *;
  mustuse fn parse_while_or_until(bool is_until) throws -> Command *;
  mustuse fn parse_for() throws -> Command *;
  mustuse fn parse_case() throws -> Command *;
  mustuse fn parse_brace_group() throws -> Command *;
  mustuse fn parse_subshell() throws -> Command *;
  mustuse fn parse_function_definition(Token *name_token) throws -> Command *;

  mustuse fn parse_expression(u8 min_precedence = 0) throws -> Expression *;
};

} /* namespace shit */
