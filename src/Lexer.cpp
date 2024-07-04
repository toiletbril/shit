#include "Lexer.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <string>

namespace shit {

static constexpr char CEOF = static_cast<char>(EOF);

bool
Lexer::is_whitespace(char ch)
{
  switch (ch) {
  case ' ':
  case '\v':
  case '\r':
  case '\t': return true;
  default: return false;
  }
}

bool
Lexer::is_number(char ch)
{
  return ch >= '0' && ch <= '9';
}

bool
Lexer::is_expression_sentinel(char ch)
{
  switch (ch) {
  case '\n':
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case ')':
  case '(':
  case ';':
  case '~':
  case '&':
  case '|':
  case '>':
  case '<':
  case '^':
  case '=':
  case '.':
  case '!': return true;
  default: return false;
  };
}

bool
Lexer::is_shell_sentinel(char ch)
{
  switch (ch) {
  case '\n':
  case '|':
  case '{':
  case '}':
  case '&':
  case ';': return true;
  default: return false;
  };
}

bool
Lexer::is_part_of_identifier(char ch)
{
  return !is_shell_sentinel(ch) && !is_whitespace(ch) && ch != CEOF;
}

bool
Lexer::is_string_quote(char ch)
{
  switch (ch) {
  case '"':
  case '\'':
  case '`': return true;
  default: return false;
  }
}

bool
Lexer::is_ascii_char(char ch)
{
  return (ch >= 'A' && ch <= 'A') || (ch >= 'a' && ch <= 'z');
}

bool
Lexer::is_expandable_char(char ch)
{
  switch (ch) {
  case '[':
  case '?':
  case '*': return true;
  default: return false;
  }
}

bool
Lexer::is_redirect_char(char ch)
{
  switch (ch) {
  case '>':
  case '<': return true;
  default: return false;
  }
}

Lexer::Lexer(std::string source) : m_source(std::move(source)) {}

Lexer::~Lexer() = default;

Token *
Lexer::peek_expression_token()
{
  skip_whitespace();
  return lex_expression_token();
}

Token *
Lexer::peek_shell_token()
{
  skip_whitespace();
  return lex_shell_token();
}

Token *
Lexer::next_expression_token()
{
  skip_whitespace();
  Token *t = lex_expression_token();
  advance_past_last_peek();
  return t;
}

Token *
Lexer::next_shell_token()
{
  skip_whitespace();
  Token *t = lex_shell_token();
  advance_past_last_peek();
  return t;
}

std::string_view
Lexer::source() const
{
  return m_source;
}

usize
Lexer::advance_past_last_peek()
{
  usize r = advance_forward(m_cached_offset);
  m_cached_offset = 0;
  return r;
}

Token *
Lexer::lex_expression_token()
{
  if (char ch = chop_character(); ch != CEOF) {
    if (is_number(ch)) {
      return lex_number();
    } else if (is_expression_sentinel(ch)) {
      return lex_sentinel();
    } else if (is_part_of_identifier(ch)) {
      return lex_argument();
    } else {
      throw ErrorWithLocation{m_cursor_position, "Unexpected character"};
    }
  }

  return new tokens::EndOfFile{m_cursor_position};
}

Token *
Lexer::lex_shell_token()
{
  if (char ch = chop_character(); ch != CEOF) {
    if (is_shell_sentinel(ch)) {
      return lex_sentinel();
    } else if (is_part_of_identifier(ch)) {
      return lex_argument();
    } else {
      throw ErrorWithLocation{m_cursor_position, "Unexpected character"};
    }
  }

  return new tokens::EndOfFile{m_cursor_position};
}

void
Lexer::skip_whitespace()
{
  usize i = 0;
  while (is_whitespace(chop_character(i++))) {
    /* Chop chop... */
  }
  advance_forward((i > 0) ? i - 1 : 0);
}

usize
Lexer::advance_forward(usize offset)
{
  SHIT_ASSERT(m_cursor_position + offset <= m_source.length());
  m_cursor_position += offset;
  return offset;
}

char
Lexer::chop_character(usize offset)
{
  if (m_cursor_position + offset < m_source.length()) {
    return m_source[m_cursor_position + offset];
  }

  return CEOF;
}

Token *
Lexer::lex_number()
{
  char        ch;
  std::string n{};
  usize       length = 0;

  while (is_number((ch = chop_character(length)))) {
    n += ch;
    length++;
  }

  Token *num = new tokens::Number{m_cursor_position, n};
  m_cached_offset = length;

  return num;
}

/* TODO: Escaping looks terrible here, but I can't think of a better way. */
Token *
Lexer::lex_argument()
{
  char        ch;
  std::string id{};

  usize length = 0;
  bool  should_escape = false;

  std::optional<char> quote_char{};

  /* Handle quote escapes and strings. */
  while (is_part_of_identifier((ch = chop_character(length))) ||
         ((quote_char || should_escape) && ch != CEOF))
  {
    length++;
    id += ch;
  }

  Token *t{};

  /* An identifier may be a keyword. */
  if (auto kw = KEYWORDS.find(shit::utils::lowercase_string(id));
      kw != KEYWORDS.end())
  {
    switch (kw->second) {
      /* clang-format off */
    case Token::Kind::If:   t = new tokens::If{m_cursor_position}; break;
    case Token::Kind::Then: t = new tokens::Then{m_cursor_position}; break;
    case Token::Kind::Else: t = new tokens::Else{m_cursor_position}; break;
    case Token::Kind::Fi:   t = new tokens::Fi{m_cursor_position}; break;
      /* clang-format on */
    default: SHIT_UNREACHABLE("Unhandled keyword of type %d", E(kw->second));
    }
  } else {
    t = new tokens::Identifier{m_cursor_position, id};
  }

  m_cached_offset = length;

  return t;
}

/* Only single-character operators are defined here. Further parsing is done in
 * related routines. */
static const std::unordered_map<char, Token::Kind> OPERATORS = {
    /* Sentinels */
    {')',  Token::Kind::RightParen        },
    {'(',  Token::Kind::LeftParen         },
    {']',  Token::Kind::RightSquareBracket},
    {'[',  Token::Kind::LeftSquareBracket },
    {'}',  Token::Kind::RightBracket      },
    {'{',  Token::Kind::LeftBracket       },

    {';',  Token::Kind::Semicolon         },
    {'.',  Token::Kind::Dot               },
    {'\n', Token::Kind::Newline           },

    /* Operators */
    {'+',  Token::Kind::Plus              },
    {'-',  Token::Kind::Minus             },
    {'*',  Token::Kind::Asterisk          },
    {'/',  Token::Kind::Slash             },
    {'%',  Token::Kind::Percent           },
    {'~',  Token::Kind::Tilde             },
    {'^',  Token::Kind::Cap               },
    {'!',  Token::Kind::ExclamationMark   },
    {'&',  Token::Kind::Ampersand         },
    {'>',  Token::Kind::Greater           },
    {'<',  Token::Kind::Less              },
    {'|',  Token::Kind::Pipe              },
    {'=',  Token::Kind::Equals            },
};

Token *
Lexer::lex_sentinel()
{
  char  ch = chop_character();
  usize extra_length = 0;

  Token *t{};

  if (auto op = OPERATORS.find(ch); op != OPERATORS.end()) {
    switch (op->second) {
      /* clang-format off */
    case Token::Kind::RightParen:   t = new tokens::RightParen{m_cursor_position}; break;
    case Token::Kind::LeftParen:    t = new tokens::LeftParen{m_cursor_position}; break;
    case Token::Kind::RightBracket: t = new tokens::RightBracket{m_cursor_position}; break;
    case Token::Kind::LeftBracket:  t = new tokens::LeftBracket{m_cursor_position}; break;

    case Token::Kind::Semicolon:    t = new tokens::Semicolon{m_cursor_position}; break;
    case Token::Kind::Dot:          t = new tokens::Dot{m_cursor_position}; break;
    case Token::Kind::Newline:      t = new tokens::Newline{m_cursor_position}; break;

    case Token::Kind::Plus:         t = new tokens::Plus{m_cursor_position}; break;
    case Token::Kind::Minus:        t = new tokens::Minus{m_cursor_position}; break;
    case Token::Kind::Asterisk:     t = new tokens::Asterisk{m_cursor_position}; break;
    case Token::Kind::Slash:        t = new tokens::Slash{m_cursor_position}; break;
    case Token::Kind::Percent:      t = new tokens::Percent{m_cursor_position}; break;
    case Token::Kind::Tilde:        t = new tokens::Tilde{m_cursor_position}; break;
    case Token::Kind::Cap:          t = new tokens::Cap{m_cursor_position}; break;
      /* clang-format on */

    case Token::Kind::RightSquareBracket: {
      if (chop_character(1) == ']') {
        t = new tokens::DoubleRightSquareBracket{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::RightSquareBracket{m_cursor_position};
      }
    } break;

    case Token::Kind::LeftSquareBracket: {
      if (chop_character(1) == '[') {
        t = new tokens::DoubleLeftSquareBracket{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::LeftSquareBracket{m_cursor_position};
      }
    } break;

    case Token::Kind::ExclamationMark: {
      if (chop_character(1) == '=') {
        t = new tokens::ExclamationEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::ExclamationMark{m_cursor_position};
      }
    } break;

    case Token::Kind::Ampersand: {
      if (chop_character(1) == '&') {
        t = new tokens::DoubleAmpersand{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::Ampersand{m_cursor_position};
      }
    } break;

    case Token::Kind::Greater: {
      if (chop_character(1) == '>') {
        t = new tokens::DoubleGreater{m_cursor_position};
        extra_length++;
      } else if (chop_character(1) == '=') {
        t = new tokens::GreaterEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::Greater{m_cursor_position};
      }
    } break;

    case Token::Kind::Less: {
      if (chop_character(1) == '<') {
        t = new tokens::DoubleLess{m_cursor_position};
        extra_length++;
      } else if (chop_character(1) == '=') {
        t = new tokens::LessEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::Less{m_cursor_position};
      }
    } break;

    case Token::Kind::Pipe: {
      if (chop_character(1) == '|') {
        t = new tokens::DoublePipe{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::Pipe{m_cursor_position};
      }
    } break;

    case Token::Kind::Equals: {
      if (chop_character(1) == '=') {
        t = new tokens::DoubleEquals{m_cursor_position};
        extra_length++;
      } else {
        t = new tokens::Equals{m_cursor_position};
      }
    } break;

    default: SHIT_UNREACHABLE("Unhandled operator of type %d", E(op->second));
    }
  } else {
    std::string s{};
    s += "Unknown operator '";
    s += ch;
    s += "'";
    throw ErrorWithLocation{m_cursor_position, s};
  }

  m_cached_offset = 1 + extra_length;

  return t;
}

} /* namespace shit */
