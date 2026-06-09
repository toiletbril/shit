#pragma once

#include "Containers.hpp"
#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"

#include <initializer_list>

namespace shit {

using namespace expressions;

class Parser
{
public:
  Parser(Lexer &&lexer, bool bash_compatible = false);
  ~Parser();

  fn construct_ast() throws -> Expression *;

  /* Parse the whole input, recovering from each syntax error by resynchronizing
     to the next statement boundary and continuing, so one pass collects every
     error rather than stopping at the first. Each error is appended as a fully
     rendered message, the primary line and any detail note, so a detail hint is
     not lost the way storing the base ErrorWithLocation by value would slice it
     off. The returned tree is meant to run only when errors stays empty. */
  fn construct_ast(ArrayList<String> &errors) throws -> Expression *;

  pure fn debug_words() const wontthrow -> const ArrayList<Word> &;

private:
  static constexpr usize MAX_RECURSION_DEPTH = 64;

  /* The compound-command nesting limit guards the native stack against a
     pathologically nested source such as thousands of open parentheses. It is
     looser than the arithmetic limit because a legitimate script nests far
     fewer compound commands than an arithmetic expression nests operators. */
  static constexpr usize MAX_COMMAND_DEPTH = 512;

  Lexer m_lexer;

  /* Whether bash-compatible parsing is active, the (( )) arithmetic command and
     the C-style for. Handed in at construction from the EvalContext mode rather
     than read from a global. */
  bool m_bash_compatible;

  usize m_command_depth{0};
  usize m_recursion_depth{0};
  usize m_if_condition_depth{0};
  usize m_parentheses_depth{0};

  mustuse fn parse_simple_command() throws -> Command *;

  /* Skip tokens until the next statement boundary or the end of input, so a
     recovering parse can resume after a syntax error. Always consumes at least
     one token to guarantee forward progress. */
  fn recover_to_next_statement() throws -> void;

  /* Consume newlines that follow a pipe operator, so a pipeline written across
     several lines with a trailing '|' continues onto the next command. */
  fn skip_newlines_after_pipe() throws -> void;

  /* Build one file or descriptor-duplication redirection for descriptor fd. The
     operator is already consumed and op_location is its position. Shared by the
     simple command parser and the trailing redirect parser. */
  fn build_file_or_dup_redirection(
      i32 fd, Token::Kind op_kind, SourceLocation op_location,
      Maybe<SourceLocation> &first_location,
      ArrayList<expressions::Redirection> &out) throws -> void;

  /* Build one heredoc redirection on descriptor fd. The << operator is already
     consumed and op_location is its position. A digit prefix such as the 3 in
     3<<EOF supplies a non-zero fd. */
  fn build_heredoc_redirection(i32 fd, SourceLocation op_location,
                               Maybe<SourceLocation> &first_location,
                               ArrayList<expressions::Redirection> &out) throws
      -> void;

  /* The digit word is already peeked and word_location is its position. Consume
     it, and when the next token is a redirect operator touching the digit run,
     parse the descriptor and append the redirection to out, then return true.
     Return false with the digit consumed when no adjacent operator follows, so
     the caller decides what the bare number means. Shared by the simple command
     parser and the trailing redirect parser. */
  mustuse fn try_parse_descriptor_prefixed_redirection(
      const tokens::WordToken *word_token, SourceLocation word_location,
      Maybe<SourceLocation> &first_location,
      ArrayList<expressions::Redirection> &out) throws -> bool;

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
  mustuse fn parse_paren_command() throws -> Command *;
  mustuse fn parse_subshell(Token *open) throws -> Command *;
  mustuse fn capture_double_paren_body(Token *open) throws -> StringView;
  mustuse fn parse_arithmetic_command(Token *open) throws -> Command *;
  mustuse fn parse_c_style_for(SourceLocation location, Token *open) throws
      -> Command *;
  mustuse fn parse_conditional_command() throws -> Command *;
  mustuse fn parse_function_definition(Token *name_token) throws -> Command *;

  /* Consume a bash array assignment group NAME=(...) or NAME+=(...) and return
     its element tokens. Bash mode expands them into the array, POSIX mode
     discards the list and evaluates the assignment as a no-op. */
  fn consume_bash_array_assignment() throws -> ArrayList<const Token *>;

  mustuse fn parse_expression(u8 min_precedence = 0) throws -> Expression *;
};

} /* namespace shit */
