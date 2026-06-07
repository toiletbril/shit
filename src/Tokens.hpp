#pragma once

#include "Common.hpp"
#include "Containers.hpp"
#include "Eval.hpp"
#include "Maybe.hpp"

#include <memory>
#include <utility>

namespace shit {

class Expression;

/* The name and right hand side of an assignment word, the named result of
   Word::get_assignment_split. The struct is forward-declared here and defined
   below Word, since its value field holds a Word by value and a Word is not yet
   complete at this point. */
struct word_assignment_split;

class WordSegment
{
public:
  /* The kind records how the evaluator may expand this segment. LiteralText is
     final and the evaluator leaves it alone. UnquotedText expands a leading
     tilde, splits on IFS after variable expansion, and globs. DoubleQuotedText
     expands variables but never splits or globs. VariableReference holds a
     variable name that the evaluator resolves at run time. */
  enum class Kind : u8
  {
    LiteralText,
    UnquotedText,
    DoubleQuotedText,
    VariableReference,
    /* The text holds the source inside $(...). The evaluator runs it and
       splices the captured output. */
    CommandSubstitution,
    /* The text holds the source inside $((...)). The evaluator computes it and
       splices the decimal result. */
    ArithmeticExpansion,
  };

  Kind kind;
  String text;
  bool is_in_double_quotes{false};

  /* The constant decimal result of an ArithmeticExpansion segment whose source
     holds no parameter and no command substitution, computed once at analyze
     time. The evaluator reads it instead of re-parsing the arithmetic on every
     expansion, which matters inside a loop body. None on any non-constant
     segment. */
  mutable Maybe<i64> folded_arithmetic_result{};

  /* The parsed inner command of a CommandSubstitution segment, lexed and parsed
     once and reused on every later expansion. The outer AST of a loop body is
     re-evaluated without re-parsing, so a $(...) in the body would otherwise
     re-run the lexer and parser each iteration. The tree lives in AST_ARENA
     alongside the outer command and is reclaimed when that arena resets, so the
     cache never outlives the segment it hangs on. The parsed flag separates a
     cached null from an unparsed segment. */
  mutable const Expression *cached_substitution_ast{nullptr};
  mutable bool is_substitution_parsed{false};

  pure fn is_split_eligible() const wontthrow -> bool;
  pure fn has_live_glob_chars() const wontthrow -> bool;
  pure fn is_tilde_candidate() const wontthrow -> bool;

  /* True when the segment text holds an unquoted glob metacharacter, one of '*',
     '?', or '['. The plain-literal fast path in the evaluator consults this to
     decide whether a word may skip pathname expansion. */
  pure fn has_glob_metacharacter() const wontthrow -> bool;
};

/* A lexed word carries its quoting structure as ordered segments. The evaluator
   expands the segments instead of consulting a source-position escape map, so
   the byte offsets never drift apart from the produced text. */
class Word
{
public:
  ArrayList<WordSegment> segments{heap_allocator()};

  pure fn is_empty() const wontthrow -> bool;
  fn to_literal_string() const throws -> String;
  fn to_pretty_string() const throws -> String;

  /* True when the literal text of the word is a non-empty run of ASCII digits,
     the shape a descriptor prefix such as the 2 in 2>file takes. The parser
     checks this in command position for every word, so it answers from the
     segments directly and allocates no literal String. */
  pure fn is_all_ascii_digits() const wontthrow -> bool;

  /* A word is an assignment when its first segment is unquoted text holding an
     unescaped NAME= prefix. The returned word is the right hand side. */
  fn get_assignment_split() const throws -> Maybe<word_assignment_split>;

  /* How a word may take the evaluator's plain-literal fast path. NotPlain words
     run the full expansion machine. PlainNoSplit words concatenate their
     segments into one field with no expansion, splitting, or globbing.
     PlainUnquotedOneSegment is a single unquoted segment free of glob
     metacharacters whose only remaining question is whether it holds an IFS
     byte, which the evaluator answers against the live separators. */
  enum class PlainLiteral : u8
  {
    NotPlain,
    PlainNoSplit,
    PlainUnquotedOneSegment,
  };

  pure fn plain_literal_kind() const wontthrow -> PlainLiteral;
};

struct word_assignment_split
{
  String name;
  Word value;
  /* The word had the form NAME+=VALUE rather than NAME=VALUE, so evaluation
     appends the value to the current value of NAME instead of replacing it. */
  bool is_append;
};

/**
 * Simple tokens
 */
class Token
{
public:
  enum class Kind : u8
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
    DoubleSemicolon,
    Dot,
    Dollar,

    /* Values */
    Number,
    Word,
    Identifier,
    Redirection,
    Assignment,

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
    CompoundList   = 1 << 5,
    /* clang-format on */
  };

  Token() = delete;
  virtual ~Token() = default;

  /* Each token should provide it's own way to copy it. */
  Token(const Token &) = delete;
  Token(Token &&) noexcept = delete;
  Token &operator=(const Token &) = delete;
  Token &operator=(Token &&) noexcept = delete;

  virtual fn kind() const wontthrow -> Kind = 0;
  virtual fn flags() const wontthrow -> Flags = 0;
  virtual fn raw_string() const throws -> String = 0;

  virtual fn to_ast_string() const throws -> String;

  pure fn source_location() const wontthrow -> SourceLocation;

  /* A token lives in the parse arena, so its storage is reclaimed in bulk. This
     no-ops for arena storage and frees an ordinary heap token otherwise. The
     destructor still runs through the normal delete. */
  static fn operator delete(void *pointer) wontthrow->void;

protected:
  Token(SourceLocation location);

private:
  SourceLocation m_location;
};

inline constexpr StaticStringMap<Token::Kind>::entry KEYWORD_ENTRIES[] = {
    {PackedStringKey::from_literal("if"),       Token::Kind::If      },
    {PackedStringKey::from_literal("then"),     Token::Kind::Then    },
    {PackedStringKey::from_literal("else"),     Token::Kind::Else    },
    {PackedStringKey::from_literal("elif"),     Token::Kind::Elif    },
    {PackedStringKey::from_literal("fi"),       Token::Kind::Fi      },
    {PackedStringKey::from_literal("when"),     Token::Kind::When    },
    {PackedStringKey::from_literal("case"),     Token::Kind::Case    },
    {PackedStringKey::from_literal("esac"),     Token::Kind::Esac    },
    {PackedStringKey::from_literal("while"),    Token::Kind::While   },
    {PackedStringKey::from_literal("for"),      Token::Kind::For     },
    {PackedStringKey::from_literal("done"),     Token::Kind::Done    },
    {PackedStringKey::from_literal("until"),    Token::Kind::Until   },
    {PackedStringKey::from_literal("time"),     Token::Kind::Time    },
    {PackedStringKey::from_literal("do"),       Token::Kind::Do      },
    {PackedStringKey::from_literal("function"), Token::Kind::Function},
};

inline constexpr StaticStringMap<Token::Kind> KEYWORDS{
    KEYWORD_ENTRIES, sizeof(KEYWORD_ENTRIES) / sizeof(KEYWORD_ENTRIES[0])};

/* clang-format off */
#define KW_CASE(k)                                                             \
  case Token::Kind::k:                                                         \
    t = m_arena->create<tokens::k>(                                            \
        SourceLocation{actual_cursor_position, byte_count});                   \
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
  class t : public Token                                                       \
  {                                                                            \
  public:                                                                      \
    t(SourceLocation location);                                                \
                                                                               \
    Kind kind() const wontthrow override;                                      \
    Flags flags() const wontthrow override;                                    \
    String raw_string() const throws override;                                 \
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
TOKEN_STRUCT(DoubleSemicolon);

TOKEN_STRUCT(Dot);
TOKEN_STRUCT(LeftParen);
TOKEN_STRUCT(RightParen);
TOKEN_STRUCT(LeftSquareBracket);
TOKEN_STRUCT(RightSquareBracket);
TOKEN_STRUCT(LeftBracket);
TOKEN_STRUCT(RightBracket);
TOKEN_STRUCT(DoubleLeftSquareBracket);
TOKEN_STRUCT(DoubleRightSquareBracket);

class Redirection : public Token
{
public:
  Redirection(SourceLocation location, StringView what_fd, StringView to_file);

  fn kind() const wontthrow -> Kind override;
  fn flags() const wontthrow -> Flags override;

  pure fn from_fd() const wontthrow -> const String &;
  pure fn to_file() const wontthrow -> const String &;

protected:
  String m_from_fd{};
  String m_to_file{};
};

class Assignment : public Token
{
public:
  Assignment(SourceLocation location, StringView key, Word value,
             bool is_append);

  fn kind() const wontthrow -> Kind override;
  fn flags() const wontthrow -> Flags override;

  fn raw_string() const throws -> String override;

  pure fn key() const wontthrow -> const String &;
  pure fn value_word() const wontthrow -> const Word &;

  /* The source spelled NAME+=VALUE, so the evaluator appends the value to the
     current value of NAME instead of replacing it. */
  pure fn is_append() const wontthrow -> bool;

protected:
  String m_key;
  Word m_value;
  bool m_is_append;
};

/* Tokens with values. */
class Value : public Token
{
public:
  Value(SourceLocation location, StringView sv);

  fn raw_string() const throws -> String override;

protected:
  String m_value;
};

#define VALUE_TOKEN_STRUCT(t)                                                  \
  class t : public Value                                                       \
  {                                                                            \
  public:                                                                      \
    t(SourceLocation location, StringView sv);                                 \
                                                                               \
    Kind kind() const wontthrow override;                                      \
    Flags flags() const wontthrow override;                                    \
  }

VALUE_TOKEN_STRUCT(Number);
VALUE_TOKEN_STRUCT(Identifier);

class WordToken : public Value
{
public:
  WordToken(SourceLocation location, Word word);

  fn kind() const wontthrow -> Kind override;
  fn flags() const wontthrow -> Flags override;

  pure fn word() const wontthrow -> const Word &;

protected:
  Word m_word;
};

class Operator : public Token
{
public:
  Operator(SourceLocation location);

  virtual fn binary_left_associative() const wontthrow -> bool;

  virtual fn left_precedence() const wontthrow -> u8;
  virtual fn construct_binary_expression(const Expression *lhs,
                                         const Expression *rhs) const throws
      -> Expression *;

  virtual fn unary_precedence() const wontthrow -> u8;
  virtual fn construct_unary_expression(const Expression *rhs) const throws
      -> Expression *;
};

#define UNARY_BINARY_OPERATOR_TOKEN_STRUCT(t)                                  \
  class t : public Operator                                                    \
  {                                                                            \
  public:                                                                      \
    t(SourceLocation location);                                                \
                                                                               \
    Kind kind() const wontthrow override;                                      \
    Flags flags() const wontthrow override;                                    \
    String raw_string() const throws override;                                 \
                                                                               \
    u8 left_precedence() const wontthrow override;                             \
    Expression *                                                               \
    construct_binary_expression(const Expression *lhs,                         \
                                const Expression *rhs) const throws override;  \
                                                                               \
    u8 unary_precedence() const wontthrow override;                            \
    Expression *                                                               \
    construct_unary_expression(const Expression *rhs) const throws override;   \
  }

UNARY_BINARY_OPERATOR_TOKEN_STRUCT(Plus);
UNARY_BINARY_OPERATOR_TOKEN_STRUCT(Minus);

#define UNARY_OPERATOR_TOKEN_STRUCT(t)                                         \
  class t : public Operator                                                    \
  {                                                                            \
  public:                                                                      \
    t(SourceLocation location);                                                \
                                                                               \
    Kind kind() const wontthrow override;                                      \
    Flags flags() const wontthrow override;                                    \
    String raw_string() const throws override;                                 \
                                                                               \
    u8 unary_precedence() const wontthrow override;                            \
    Expression *                                                               \
    construct_unary_expression(const Expression *rhs) const throws override;   \
  }

UNARY_OPERATOR_TOKEN_STRUCT(Tilde);
UNARY_OPERATOR_TOKEN_STRUCT(ExclamationMark);

#define BINARY_OPERATOR_TOKEN_STRUCT(t)                                        \
  class t : public Operator                                                    \
  {                                                                            \
  public:                                                                      \
    t(SourceLocation location);                                                \
                                                                               \
    Kind kind() const wontthrow override;                                      \
    Flags flags() const wontthrow override;                                    \
    String raw_string() const throws override;                                 \
                                                                               \
    u8 left_precedence() const wontthrow override;                             \
    Expression *                                                               \
    construct_binary_expression(const Expression *lhs,                         \
                                const Expression *rhs) const throws override;  \
  }

BINARY_OPERATOR_TOKEN_STRUCT(Ampersand);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleAmpersand);
BINARY_OPERATOR_TOKEN_STRUCT(DoublePipe);

BINARY_OPERATOR_TOKEN_STRUCT(Slash);
BINARY_OPERATOR_TOKEN_STRUCT(Percent);
BINARY_OPERATOR_TOKEN_STRUCT(Asterisk);
BINARY_OPERATOR_TOKEN_STRUCT(Greater);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleGreater);
BINARY_OPERATOR_TOKEN_STRUCT(GreaterEquals);
BINARY_OPERATOR_TOKEN_STRUCT(Less);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleLess);
BINARY_OPERATOR_TOKEN_STRUCT(LessEquals);
BINARY_OPERATOR_TOKEN_STRUCT(Pipe);
BINARY_OPERATOR_TOKEN_STRUCT(Cap);
BINARY_OPERATOR_TOKEN_STRUCT(Equals);
BINARY_OPERATOR_TOKEN_STRUCT(DoubleEquals);
BINARY_OPERATOR_TOKEN_STRUCT(ExclamationEquals);

} /* namespace tokens */

} /* namespace shit */
