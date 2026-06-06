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

  /* Build one file or descriptor-duplication redirection for descriptor fd. The
     operator is already consumed and op_location is its position. Shared by the
     simple command parser and the trailing redirect parser. */
  fn build_file_or_dup_redirection(
      i32 fd, Token::Kind op_kind, SourceLocation op_location,
      Maybe<SourceLocation> &first_location,
      ArrayList<expressions::Redirection> &out) throws -> void;

  /* Build one heredoc redirection. The << operator is already consumed and
     op_location is its position. */
  fn build_heredoc_redirection(SourceLocation op_location,
                               Maybe<SourceLocation> &first_location,
                               ArrayList<expressions::Redirection> &out) throws
      -> void;

  /* Peek the next token and, when it begins a redirection, consume the whole
     redirection and append it to out. Returns true when one was parsed, false
     when the next token does not begin a redirection. Used to attach trailing
     redirects to a compound command. */
  mustuse fn try_parse_trailing_redirection(
      ArrayList<expressions::Redirection> &out) throws -> bool;

  /* Parse any trailing redirects that follow a compound command and, when there
     are any, wrap it in a RedirectedCommand. Returns the command unchanged when
     no redirect follows. */
  mustuse fn attach_trailing_redirections(Command *compound) throws
      -> Command *;

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

  /* A bash array assignment NAME=(...) or NAME+=(...) is unsupported in this
     POSIX shell, so the balanced parenthesis group is consumed and the whole
     assignment evaluates as a no-op rather than aborting the file. */
  fn consume_bash_array_assignment() throws -> void;

  mustuse fn parse_expression(u8 min_precedence = 0) throws -> Expression *;
};

} /* namespace shit */
