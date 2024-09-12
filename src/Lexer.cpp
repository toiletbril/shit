#include "Lexer.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"

#include <string>

namespace shit {

namespace lexer {

static constexpr char CEOF = static_cast<char>(EOF);

bool
is_whitespace(char ch)
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
is_number(char ch)
{
  return ch >= '0' && ch <= '9';
}

bool
is_expression_sentinel(char ch)
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

/* TODO: Separate redirections from here. */
bool
is_shell_sentinel(char ch)
{
  switch (ch) {
  case '\n':
  case '|':
  case '{':
  case '}':
  case '&':
  case ';':
  case '<':
  case '>': return true;
  default: return false;
  };
}

bool
is_part_of_identifier(char ch)
{
  return !is_shell_sentinel(ch) && !is_whitespace(ch) && ch != CEOF;
}

bool
is_string_quote(char ch)
{
  switch (ch) {
  case '"':
  case '\'':
  case '`': return true;
  default: return false;
  }
}

bool
is_ascii_char(char ch)
{
  return (ch >= 'A' && ch <= 'A') || (ch >= 'a' && ch <= 'z');
}

bool
is_expandable_char(char ch)
{
  switch (ch) {
  case '[':
  case '?':
  case '*': return true;
  default: return false;
  }
}

} /* namespace lexer */

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

EscapeMap &
Lexer::escape_map()
{
  return m_escape_map;
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
  if (char ch = chop_character(); ch != lexer::CEOF) {
    if (lexer::is_number(ch)) {
      return lex_number();
    } else if (lexer::is_expression_sentinel(ch)) {
      return lex_sentinel();
    } else if (lexer::is_part_of_identifier(ch)) {
      return lex_identifier();
    } else {
      throw ErrorWithLocation{
          {m_cursor_position, 1},
          "Unexpected character"
      };
    }
  }

  return new tokens::EndOfFile{
      {m_cursor_position, 1}
  };
}

Token *
Lexer::lex_shell_token()
{
  if (char ch = chop_character(); ch != lexer::CEOF) {
    if (lexer::is_shell_sentinel(ch)) {
      return lex_sentinel();
    } else if (lexer::is_part_of_identifier(ch)) {
      return lex_identifier();
    } else {
      throw ErrorWithLocation{
          {m_cursor_position, 1},
          "unexpected character"
      };
    }
  }

  return new tokens::EndOfFile{
      {m_cursor_position, 1}
  };
}

void
Lexer::skip_whitespace()
{
  usize i = 0;
  while (lexer::is_whitespace(chop_character(i))) {
    i++;
  }
  advance_forward(i);
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

  return lexer::CEOF;
}

Token *
Lexer::lex_number()
{
  char        ch;
  std::string n{};
  usize       length = 0;

  while (lexer::is_number((ch = chop_character(length)))) {
    n += ch;
    length++;
  }

  Token *num = new tokens::Number{
      {m_cursor_position, length},
      n
  };
  m_cached_offset = length;

  return num;
}

const static std::unordered_map<std::string, Token::Kind> KEYWORDS = {
    {"if",       Token::Kind::If      },
    {"then",     Token::Kind::Then    },
    {"else",     Token::Kind::Else    },
    {"elif",     Token::Kind::Elif    },
    {"fi",       Token::Kind::Fi      },
    {"when",     Token::Kind::When    },
    {"case",     Token::Kind::Case    },
    {"esac",     Token::Kind::Esac    },
    {"for",      Token::Kind::For     },
    {"done",     Token::Kind::Done    },
    {"until",    Token::Kind::Until   },
    {"time",     Token::Kind::Time    },
    {"do",       Token::Kind::Do      },
    {"function", Token::Kind::Function},
};

/* TODO: Escaping looks terrible here, but I can't think of a better way. */
Token *
Lexer::lex_identifier()
{
  char        ch;
  std::string ident_string{};

  usize byte_count = 0;

  bool should_escape = false;
  bool should_append = false;

  std::optional<char> quote_char{};
  usize               last_quote_char_pos = m_cursor_position;

  /* Handle quote escapes and strings. */
  while (lexer::is_part_of_identifier((ch = chop_character(byte_count))) ||
         ((quote_char || should_escape) && ch != lexer::CEOF))
  {
    should_append = true;

    bool is_escape = (ch == '\\');
    bool is_dollar = (ch == '$');

    bool is_in_single_quotes = (quote_char == '\'');

    if (lexer::is_expandable_char(ch)) {
      /* Escape all expandable chars inside quotes. */
      if (quote_char) {
        m_escape_map.add_escape(m_cursor_position + byte_count);
        should_append = false;
      }
    } else if ((is_escape || is_dollar) && is_in_single_quotes) {
      /* Single quotes ignore escapes/variables. */
      m_escape_map.add_escape(m_cursor_position + byte_count);
      should_append = false;
    } else if (is_escape) {
      m_escape_map.add_escape(m_cursor_position + byte_count);
      should_append = false;
    }

    byte_count++;

    if (!should_escape) {
      if (quote_char == ch) {
        quote_char = std::nullopt;
        continue;
      } else if (!quote_char && lexer::is_string_quote(ch)) {
        quote_char = ch;
        last_quote_char_pos = m_cursor_position + byte_count - 1;
        continue;
      }
    }

    if (should_append) {
      ident_string += ch;
    }

    should_escape = is_escape && !is_in_single_quotes;
  }

  if (quote_char) {
    throw ErrorWithLocationAndDetails{
        {last_quote_char_pos, byte_count},
        "Unterminated string literal",
        {m_cursor_position + byte_count, 1},
        "expected a " + std::string{*quote_char}
        + " here"
    };
  }

  Token *t{};

  usize length = toiletline::utf8_strlen(ident_string.c_str());

  /* An identifier may be a keyword. */
  if (auto kw = KEYWORDS.find(shit::utils::lowercase_string(ident_string));
      kw != KEYWORDS.end())
  {
/* clang-format off */
#define KW_CASE(k)                                                             \
  case Token::Kind::k:                                                         \
    t = new tokens::k{                                                         \
        {m_cursor_position, length}                                            \
    };                                                                         \
    break
/* clang-format on */
    switch (kw->second) {
      KW_CASE(Case);
      KW_CASE(Esac);
      KW_CASE(For);
      KW_CASE(Done);
      KW_CASE(Until);
      KW_CASE(Time);
      KW_CASE(Do);
      KW_CASE(Function);
      KW_CASE(If);
      KW_CASE(Then);
      KW_CASE(Else);
      KW_CASE(Fi);
      KW_CASE(When);
    default:
      SHIT_UNREACHABLE("unhandled keyword of type %d", SHIT_ENUM(kw->second));
    }
#undef KW_CASE
  } else {
    t = new tokens::Identifier{
        {m_cursor_position, length},
        ident_string
    };
  }

  m_cached_offset = byte_count;

  return t;
}

/* Only single-character operators are defined here. Further parsing is done in
 * related routines. */
/* clang-format off */
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
/* clang-format on */

Token *
Lexer::lex_sentinel()
{
  char  ch = chop_character();
  usize extra_length = 0;

  Token *t{};

  if (auto op = OPERATORS.find(ch); op != OPERATORS.end()) {
    switch (op->second) {
    case Token::Kind::RightParen:
      t = new tokens::RightParen{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::LeftParen:
      t = new tokens::LeftParen{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::RightBracket:
      t = new tokens::RightBracket{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::LeftBracket:
      t = new tokens::LeftBracket{
          {m_cursor_position, 1}
      };
      break;

    case Token::Kind::Semicolon:
      t = new tokens::Semicolon{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Dot:
      t = new tokens::Dot{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Newline:
      t = new tokens::Newline{
          {m_cursor_position, 1}
      };
      break;

    case Token::Kind::Plus:
      t = new tokens::Plus{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Minus:
      t = new tokens::Minus{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Asterisk:
      t = new tokens::Asterisk{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Slash:
      t = new tokens::Slash{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Percent:
      t = new tokens::Percent{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Tilde:
      t = new tokens::Tilde{
          {m_cursor_position, 1}
      };
      break;
    case Token::Kind::Cap:
      t = new tokens::Cap{
          {m_cursor_position, 1}
      };
      break;

    case Token::Kind::RightSquareBracket: {
      if (chop_character(1) == ']') {
        t = new tokens::DoubleRightSquareBracket{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::RightSquareBracket{
            {m_cursor_position, 1}
        };
      }
    } break;

    case Token::Kind::LeftSquareBracket: {
      if (chop_character(1) == '[') {
        t = new tokens::DoubleLeftSquareBracket{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::LeftSquareBracket{
            {m_cursor_position, 1}
        };
      }
    } break;

    case Token::Kind::ExclamationMark: {
      if (chop_character(1) == '=') {
        t = new tokens::ExclamationEquals{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::ExclamationMark{
            {m_cursor_position, 1}
        };
      }
    } break;

    case Token::Kind::Ampersand: {
      if (chop_character(1) == '&') {
        t = new tokens::DoubleAmpersand{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::Ampersand{
            {m_cursor_position, 1}
        };
      }
    } break;

    case Token::Kind::Greater: {
      if (chop_character(1) == '>') {
        t = new tokens::DoubleGreater{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else if (chop_character(1) == '=') {
        t = new tokens::GreaterEquals{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::Greater{
            {m_cursor_position, 1}
        };
      }
    } break;

    case Token::Kind::Less: {
      if (chop_character(1) == '<') {
        t = new tokens::DoubleLess{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else if (chop_character(1) == '=') {
        t = new tokens::LessEquals{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::Less{
            {m_cursor_position, 1}
        };
      }
    } break;

    case Token::Kind::Pipe: {
      if (chop_character(1) == '|') {
        t = new tokens::DoublePipe{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::Pipe{
            {m_cursor_position, 1}
        };
      }
    } break;

    case Token::Kind::Equals: {
      if (chop_character(1) == '=') {
        t = new tokens::DoubleEquals{
            {m_cursor_position, 2}
        };
        extra_length++;
      } else {
        t = new tokens::Equals{
            {m_cursor_position, 1}
        };
      }
    } break;

    default:
      SHIT_UNREACHABLE("unhandled operator of type %d", SHIT_ENUM(op->second));
    }
  } else {
    std::string s{};
    s += "unknown operator '";
    s += ch;
    s += "'";
    throw ErrorWithLocation{
        {m_cursor_position, extra_length},
        s
    };
  }

  m_cached_offset = 1 + extra_length;

  return t;
}

} /* namespace shit */
