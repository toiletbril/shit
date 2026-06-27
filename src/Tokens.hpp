#pragma once

#include "Common.hpp"
#include "Containers.hpp"
#include "Eval.hpp"
#include "Maybe.hpp"

namespace shit {

class Expression;

/* The name and right hand side of an assignment word. The struct is defined
   below Word, since its value field holds a Word by value and a Word is not yet
   complete at this point. */
struct word_assignment_split;

/* One lexed token of an arithmetic expression. The expression text never
   changes, so a $((...)) segment lexes its tokens once and re-evaluates from
   them rather than re-scanning the bytes on every expansion in a loop body. */
struct arith_token
{
  enum class kind : u8
  {
    number,
    name,
    op,
    subscript,
  };
  kind k;
  i64 value{0};
  StringView text{};
};

class WordSegment
{
public:
  /* The kind records how the evaluator may expand this segment. UnquotedText
     expands a leading tilde, splits on IFS after variable expansion, and globs.
     DoubleQuotedText expands variables but never splits or globs.
     VariableReference holds a variable name resolved at run time. */
  enum class Kind : u8
  {
    LiteralText,
    UnquotedText,
    DoubleQuotedText,
    VariableReference,
    CommandSubstitution,
    ArithmeticExpansion,
    /* The text is a direction byte, < or >, then the source inside <(...) or
       >(...). */
    ProcessSubstitution,
    /* The bash 5.3 funsub runs in the current shell, so its assignments and cd
       persist. */
    FunctionSubstitution,
  };

  Kind kind;
  String text;
  bool is_in_double_quotes{false};

  /* The constant decimal result of an ArithmeticExpansion segment whose source
     holds no parameter and no command substitution, computed once at analyze
     time. The evaluator reads it instead of re-parsing the arithmetic on every
     expansion. None on any non-constant segment. */
  mutable Maybe<i64> folded_arithmetic_result{};

  /* The parsed inner command of a CommandSubstitution segment, reused on every
     later expansion. The tree lives in AST_ARENA, which resets between
     top-level commands, yet a function-body segment lives in FUNCTION_ARENA and
     outlives that reset, so the cache records the arena generation it was
     filled in and a hit from an earlier generation is treated as stale and
     reparsed. */
  mutable const Expression *cached_substitution_ast{nullptr};
  mutable usize cached_substitution_generation{0};

  /* The lexed tokens of an ArithmeticExpansion segment, filled once and reused.
     The expression text is immutable, so the tokens stay valid for the
     segment's life and need no generation guard, unlike the substitution tree.
   */
  mutable ArrayList<arith_token> cached_arith_tokens{heap_allocator()};
  mutable bool arith_tokenized{false};
  /* Whether the cached tokens hold a simple expression the token evaluator can
     run. A complex expression falls back to the char parser. */
  mutable bool arith_simple{false};

  pure fn is_split_eligible() const wontthrow -> bool;
  pure fn has_live_glob_chars() const wontthrow -> bool;
  pure fn is_tilde_candidate() const wontthrow -> bool;

  /* True when the segment text holds an unquoted glob metacharacter, one of
     '*',
     '?', or '['. The plain-literal fast path consults this to decide whether a
     word may skip pathname expansion. */
  pure fn has_glob_metacharacter() const wontthrow -> bool;
};

/* A lexed word carries its quoting structure as ordered segments, expanded in
   place rather than against a source-position escape map, so the byte offsets
   never drift apart from the produced text. */
class Word
{
public:
  ArrayList<WordSegment> segments{heap_allocator()};

  pure fn is_empty() const wontthrow -> bool;
  fn to_literal_string() const throws -> String;
  fn to_pretty_string() const throws -> String;

  /* True when the literal text of the word is a non-empty run of ASCII digits,
     the shape a descriptor prefix such as the 2 in 2>file takes. The answer
     comes from the segments directly and allocates no literal String. */
  pure fn is_all_ascii_digits() const wontthrow -> bool;

  /* True when a segment of the word runs a command or a function substitution.
     The empty-command status logic and the assignment value reset both ask this
     to decide whether to reset the exit status. */
  pure fn runs_substitution() const wontthrow -> bool;

  /* A word is an assignment when its first segment is unquoted text holding an
     unescaped NAME= prefix. The returned word is the right hand side. */
  fn get_assignment_split() const throws -> Maybe<word_assignment_split>;

  /* How a word may take the evaluator's plain-literal fast path. NotPlain words
     run the full expansion machine. PlainNoSplit words concatenate their
     segments into one field with no expansion, splitting, or globbing.
     PlainUnquotedOneSegment is a single unquoted segment free of glob
     metacharacters whose only remaining question is whether it holds an IFS
     byte. */
  enum class PlainLiteral : u8
  {
    NotPlain,
    PlainNoSplit,
    PlainUnquotedOneSegment,
  };

  pure fn plain_literal_kind() const wontthrow -> PlainLiteral;

  /* The constant value of a PlainNoSplit word, the concatenation of its segment
     texts. It is built once and reused, since the segments never change after
     the parse, so a loop body that names the same literal pays the
     concatenation once rather than once per turn. */
  fn constant_value() const throws -> StringView;

private:
  /* The plain-literal class and the constant value are pure functions of the
     fixed segments, so they are memoized on the word and a tight loop reads the
     cache rather than rescanning the segments every evaluation. */
  mutable PlainLiteral m_cached_plain_kind{PlainLiteral::NotPlain};
  mutable bool m_has_cached_plain_kind{false};
  mutable String m_constant_value{};
  mutable bool m_has_constant_value{false};
};

struct word_assignment_split
{
  String name;
  Word value;
  /* The word had the form NAME+=VALUE, so evaluation appends to the current
     value of NAME instead of replacing it. */
  bool is_append;
};

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
    SemicolonAmpersand,
    DoubleSemicolonAmpersand,
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
    AmpersandGreater,
    AmpersandDoubleGreater,
    PipeAmpersand,
    Greater,
    DoubleGreater,
    GreaterEquals,
    Less,
    DoubleLess,
    TripleLess,
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
  static fn operator delete(opaque *pointer) wontthrow->void;

protected:
  Token(SourceLocation location);

private:
  SourceLocation m_location;
};

inline constexpr StaticStringMap<Token::Kind>::entry KEYWORD_ENTRIES[] = {
    {SSK("if"),       Token::Kind::If      },
    {SSK("then"),     Token::Kind::Then    },
    {SSK("else"),     Token::Kind::Else    },
    {SSK("elif"),     Token::Kind::Elif    },
    {SSK("fi"),       Token::Kind::Fi      },
    {SSK("when"),     Token::Kind::When    },
    {SSK("case"),     Token::Kind::Case    },
    {SSK("esac"),     Token::Kind::Esac    },
    {SSK("while"),    Token::Kind::While   },
    {SSK("for"),      Token::Kind::For     },
    {SSK("done"),     Token::Kind::Done    },
    {SSK("until"),    Token::Kind::Until   },
    {SSK("time"),     Token::Kind::Time    },
    {SSK("do"),       Token::Kind::Do      },
    {SSK("function"), Token::Kind::Function},
};

inline constexpr StaticStringMap<Token::Kind> KEYWORDS{
    KEYWORD_ENTRIES, countof(KEYWORD_ENTRIES)};

/* clang-format off */
/* The location goes through the lexer's here() so the keyword token carries the
   stamped filename, and a warning anchored at a for or case keyword names its
   file. */
#define KW_CASE(k)                                                             \
  case Token::Kind::k:                                                         \
    t = m_arena->create<tokens::k>(here(actual_cursor_position, byte_count));  \
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
TOKEN_STRUCT(SemicolonAmpersand);
TOKEN_STRUCT(DoubleSemicolonAmpersand);
TOKEN_STRUCT(AmpersandGreater);
TOKEN_STRUCT(AmpersandDoubleGreater);
TOKEN_STRUCT(PipeAmpersand);
TOKEN_STRUCT(TripleLess);

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

  /* The source spelled NAME+=VALUE, so the evaluator appends instead of
     replacing. */
  pure fn is_append() const wontthrow -> bool;

protected:
  String m_key;
  Word m_value;
  bool m_is_append;
};

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

} // namespace tokens

} // namespace shit
