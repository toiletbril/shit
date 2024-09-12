#pragma once

#include "Common.hpp"
#include "Expressions.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace shit {

/**
 * Simple tokens
 */
struct Token
{
  enum class Kind : uint8_t
  {
    Invalid,

    /* Significant symbols */
    RightParen,
    LeftParen,
    LeftSquareBracket,
    RightSquareBracket,
    DoubleLeftSquareBracket,
    DoubleRightSquareBracket,
    RightBracket,
    LeftBracket,

    EndOfFile,
    Newline,
    Semicolon,
    Dot,
    Dollar,

    /* Values */
    Number,
    String,
    Identifier,
    Redirection,

    /* Operators */
    Plus,
    Minus,
    Asterisk,
    Slash,
    Percent,
    Tilde,
    Ampersand,
    DoubleAmpersand,
    Greater,
    DoubleGreater,
    GreaterEquals,
    Less,
    DoubleLess,
    LessEquals,
    Pipe,
    DoublePipe,
    Cap,
    Equals,
    DoubleEquals,
    ExclamationMark,
    ExclamationEquals,

    /* Keywords */
    If,
    Then,
    Else,
    Fi,
    Echo,
    Exit,
    Elif,
    When,
    While,
    Case,
    For,
    Done,
    Esac,
    Until,
    Time,
    Do,
    Function,
  };

  using Flags = u8;

  enum Flag : uint8_t
  {
    /* clang-format off */
    Sentinel       = 0,
    Value          = 1,
    UnaryOperator  = 1 << 1,
    BinaryOperator = 1 << 2,
    Special        = 1 << 3,
    Keyword        = 1 << 4,
    /* clang-format on */
  };

  Token() = delete;
  virtual ~Token() = default;

  /* Each token should provide it's own way to copy it. */
  Token(const Token &) = delete;
  Token(Token &&) noexcept = delete;
  Token &operator=(const Token &) = delete;
  Token &operator=(Token &&) noexcept = delete;

  virtual Kind        kind() const = 0;
  virtual Flags       flags() const = 0;
  virtual std::string raw_string() const = 0;

  virtual std::string to_ast_string() const;

  SourceLocation source_location() const;

protected:
  Token(SourceLocation location);

private:
  SourceLocation m_location;
};

const std::unordered_map<std::string, Token::Kind> KEYWORDS = {
    {"if",       Token::Kind::If      },
    {"then",     Token::Kind::Then    },
    {"else",     Token::Kind::Else    },
    {"elif",     Token::Kind::Elif    },
    {"fi",       Token::Kind::Fi      },
    {"when",     Token::Kind::When    },
    {"case",     Token::Kind::Case    },
    {"esac",     Token::Kind::Esac    },
    {"while",    Token::Kind::While   },
    {"for",      Token::Kind::For     },
    {"done",     Token::Kind::Done    },
    {"until",    Token::Kind::Until   },
    {"time",     Token::Kind::Time    },
    {"do",       Token::Kind::Do      },
    {"function", Token::Kind::Function},
};

/* clang-format off */
#define KW_CASE(k)                                                             \
  case Token::Kind::k:                                                         \
    t = new tokens::k{                                                         \
        {m_cursor_position, length}                                            \
    };                                                                         \
    break
/* clang-format on */

#define KW_SWITCH_CASES()                                                      \
  KW_CASE(If);                                                                 \
  KW_CASE(Then);                                                               \
  KW_CASE(Else);                                                               \
  KW_CASE(Elif);                                                               \
  KW_CASE(Fi);                                                                 \
  KW_CASE(When);                                                               \
  KW_CASE(Case);                                                               \
  KW_CASE(While);                                                              \
  KW_CASE(Esac);                                                               \
  KW_CASE(For);                                                                \
  KW_CASE(Done);                                                               \
  KW_CASE(Until);                                                              \
  KW_CASE(Time);                                                               \
  KW_CASE(Do);                                                                 \
  KW_CASE(Function);

namespace tokens {

#define TOKEN_STRUCT(t)                                                        \
  struct t : public Token                                                      \
  {                                                                            \
    t(SourceLocation location);                                                \
                                                                               \
    Kind        kind() const override;                                         \
    Flags       flags() const override;                                        \
    std::string raw_string() const override;                                   \
  }

TOKEN_STRUCT(If);
TOKEN_STRUCT(Fi);
TOKEN_STRUCT(Else);
TOKEN_STRUCT(Elif);
TOKEN_STRUCT(Then);
TOKEN_STRUCT(Case);
TOKEN_STRUCT(When);
TOKEN_STRUCT(Esac);
TOKEN_STRUCT(For);
TOKEN_STRUCT(While);
TOKEN_STRUCT(Until);
TOKEN_STRUCT(Do);
TOKEN_STRUCT(Done);
TOKEN_STRUCT(Time);
TOKEN_STRUCT(Function);

TOKEN_STRUCT(EndOfFile);
TOKEN_STRUCT(Newline);
TOKEN_STRUCT(Semicolon);
TOKEN_STRUCT(Dot);
TOKEN_STRUCT(LeftParen);
TOKEN_STRUCT(RightParen);
TOKEN_STRUCT(LeftSquareBracket);
TOKEN_STRUCT(RightSquareBracket);
TOKEN_STRUCT(LeftBracket);
TOKEN_STRUCT(RightBracket);
TOKEN_STRUCT(DoubleLeftSquareBracket);
TOKEN_STRUCT(DoubleRightSquareBracket);

struct Redirection : Token
{
  Redirection(SourceLocation location, std::string_view what_fd,
              std::string_view to_file);

  Kind  kind() const override;
  Flags flags() const override;

  const std::string &from_fd() const;
  const std::string &to_file() const;

protected:
  std::string m_from_fd{};
  std::string m_to_file{};
};

/* Tokens with values. */
struct Value : public Token
{
  Value(SourceLocation location, std::string_view sv);

  std::string raw_string() const override;

protected:
  std::string m_value;
};

#define VALUE_TOKEN_STRUCT(t)                                                  \
  struct t : public Value                                                      \
  {                                                                            \
    t(SourceLocation location, std::string_view sv);                           \
                                                                               \
    Kind  kind() const override;                                               \
    Flags flags() const override;                                              \
  }

VALUE_TOKEN_STRUCT(Number);
VALUE_TOKEN_STRUCT(Identifier);

struct String : public Value
{
  String(SourceLocation location, char quote_char, std::string_view sv);

  Kind  kind() const override;
  Flags flags() const override;

  char quote_char() const;

protected:
  char m_quote_char;
};

struct Operator : public Token
{
  Operator(SourceLocation location);

  virtual bool binary_left_associative() const;

  virtual u8 left_precedence() const;
  virtual std::unique_ptr<Expression>
  construct_binary_expression(const Expression *lhs,
                              const Expression *rhs) const;

  virtual u8 unary_precedence() const;
  virtual std::unique_ptr<Expression>
  construct_unary_expression(const Expression *rhs) const;
};

#define UNARY_BINARY_OPERATOR_TOKEN_STRUCT(t)                                  \
  struct t : Operator                                                          \
  {                                                                            \
    t(SourceLocation location);                                                \
                                                                               \
    Kind        kind() const override;                                         \
    Flags       flags() const override;                                        \
    std::string raw_string() const override;                                   \
                                                                               \
    u8 left_precedence() const override;                                       \
    std::unique_ptr<Expression>                                                \
    construct_binary_expression(const Expression *lhs,                         \
                                const Expression *rhs) const override;         \
                                                                               \
    u8 unary_precedence() const override;                                      \
    std::unique_ptr<Expression>                                                \
    construct_unary_expression(const Expression *rhs) const override;          \
  }

UNARY_BINARY_OPERATOR_TOKEN_STRUCT(Plus);
UNARY_BINARY_OPERATOR_TOKEN_STRUCT(Minus);

#define UNARY_OPERATOR_TOKEN_STRUCT(t)                                         \
  struct t : Operator                                                          \
  {                                                                            \
    t(SourceLocation location);                                                \
                                                                               \
    Kind        kind() const override;                                         \
    Flags       flags() const override;                                        \
    std::string raw_string() const override;                                   \
                                                                               \
    u8 unary_precedence() const override;                                      \
    std::unique_ptr<Expression>                                                \
    construct_unary_expression(const Expression *rhs) const override;          \
  }

UNARY_OPERATOR_TOKEN_STRUCT(Tilde);
UNARY_OPERATOR_TOKEN_STRUCT(ExclamationMark);

#define BINARY_OPERATOR_TOKEN_STRUCT(t)                                        \
  struct t : Operator                                                          \
  {                                                                            \
    t(SourceLocation location);                                                \
                                                                               \
    Kind        kind() const override;                                         \
    Flags       flags() const override;                                        \
    std::string raw_string() const override;                                   \
                                                                               \
    u8 left_precedence() const override;                                       \
    std::unique_ptr<Expression>                                                \
    construct_binary_expression(const Expression *lhs,                         \
                                const Expression *rhs) const override;         \
  }

BINARY_OPERATOR_TOKEN_STRUCT(Slash);
BINARY_OPERATOR_TOKEN_STRUCT(Percent);
BINARY_OPERATOR_TOKEN_STRUCT(Asterisk);
BINARY_OPERATOR_TOKEN_STRUCT(Ampersand);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleAmpersand);
BINARY_OPERATOR_TOKEN_STRUCT(Greater);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleGreater);
BINARY_OPERATOR_TOKEN_STRUCT(GreaterEquals);
BINARY_OPERATOR_TOKEN_STRUCT(Less);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleLess);
BINARY_OPERATOR_TOKEN_STRUCT(LessEquals);
BINARY_OPERATOR_TOKEN_STRUCT(Pipe);
BINARY_OPERATOR_TOKEN_STRUCT(DoublePipe);
BINARY_OPERATOR_TOKEN_STRUCT(Cap);
BINARY_OPERATOR_TOKEN_STRUCT(Equals);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleEquals);
BINARY_OPERATOR_TOKEN_STRUCT(ExclamationEquals);

} /* namespace tokens */

} /* namespace shit */
