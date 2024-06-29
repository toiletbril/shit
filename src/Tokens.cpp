#include "Tokens.hpp"

#include "Debug.hpp"

namespace shit {

/**
 * class: Token
 */
Token::Token(usize location) : m_location(location) {}

usize
Token::source_location() const
{
  return m_location;
}

std::string
Token::to_ast_string() const
{
  return value();
}

namespace tokens {

/**
 * class: TokenIf
 */
If::If(usize location) : Token(location) {}

Token::Kind
If::kind() const
{
  return Token::Kind::If;
}

Token::Flags
If::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
If::value() const
{
  return "If";
}

/**
 * class: TokenElse
 */
Else::Else(usize location) : Token(location) {}

Token::Kind
Else::kind() const
{
  return Token::Kind::Else;
}

Token::Flags
Else::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
Else::value() const
{
  return "Else";
}

/**
 * class: TokenThen
 */
Then::Then(usize location) : Token(location) {}

Token::Kind
Then::kind() const
{
  return Token::Kind::Then;
}

Token::Flags
Then::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
Then::value() const
{
  return "Then";
}

/**
 * class: TokenFi
 */
Fi::Fi(usize location) : Token(location) {}

Token::Kind
Fi::kind() const
{
  return Token::Kind::Fi;
}

Token::Flags
Fi::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
Fi::value() const
{
  return "Fi";
}

/**
 * class: TokenEndOfFile
 */
EndOfFile::EndOfFile(usize location) : Token(location) {}

Token::Kind
EndOfFile::kind() const
{
  return Token::Kind::EndOfFile;
}

Token::Flags
EndOfFile::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
EndOfFile::value() const
{
  return "EOF";
}

/**
 * class: TokenSemicolon
 */
Semicolon::Semicolon(usize location) : Token(location) {}

Token::Kind
Semicolon::kind() const
{
  return Token::Kind::Semicolon;
}

Token::Flags
Semicolon::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
Semicolon::value() const
{
  return ";";
}

/**
 * class: TokenDot
 */
Dot::Dot(usize location) : Token(location) {}

Token::Kind
Dot::kind() const
{
  return Token::Kind::Dot;
}

Token::Flags
Dot::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
Dot::value() const
{
  return ".";
}

/**
 * class: TokenDollar
 */
Dollar::Dollar(usize location) : Token(location) {}

Token::Kind
Dollar::kind() const
{
  return Token::Kind::Dollar;
}

Token::Flags
Dollar::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
Dollar::value() const
{
  return "$";
}

/**
 * class: Value
 */
Value::Value(usize location, std::string_view sv)
    : Token(location), m_value(sv)
{}

std::string
Value::value() const
{
  return m_value;
}

/**
 * class: TokenNumber
 */
Number::Number(usize location, std::string_view sv)
    : Value(location, sv)
{}

Token::Kind
Number::kind() const
{
  return Token::Kind::Number;
}

Token::Flags
Number::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenString
 */
String::String(usize location, std::string_view sv)
    : Value(location, sv)
{}

Token::Kind
String::kind() const
{
  return Token::Kind::String;
}

Token::Flags
String::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenIdentifier
 */
Identifier::Identifier(usize location, std::string_view sv)
    : Value(location, sv)
{}

Token::Kind
Identifier::kind() const
{
  return Token::Kind::Identifier;
}

Token::Flags
Identifier::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: Operator
 */
Operator::Operator(usize location) : Token(location) {}

u8
Operator::left_precedence() const
{
  return 0;
}

u8
Operator::unary_precedence() const
{
  return 0;
}

bool
Operator::binary_left_associative() const
{
  return true;
}

std::unique_ptr<Expression>
Operator::construct_binary_expression(const Expression *lhs,
                                           const Expression *rhs) const
{
  SHIT_UNUSED(lhs);
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid binary operator construction of type %d",
                   E(kind()));
}

std::unique_ptr<Expression>
Operator::construct_unary_expression(const Expression *rhs) const
{
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid unary operator construction of type %d", E(kind()));
}

/**
 * class: TokenPlus
 */
Plus::Plus(usize location) : Operator(location) {}

Token::Kind
Plus::kind() const
{
  return Token::Kind::Plus;
}

Token::Flags
Plus::flags() const
{
  return Token::Flag::BinaryOperator | Token::Flag::UnaryOperator;
}

std::string
Plus::value() const
{
  return "+";
}

u8
Plus::left_precedence() const
{
  return 11;
}

u8
Plus::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
Plus::construct_binary_expression(const Expression *lhs,
                                       const Expression *rhs) const
{
  return std::make_unique<Add>(source_location(), lhs, rhs);
}

std::unique_ptr<Expression>
Plus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<Unnegate>(source_location(), rhs);
}

/**
 * class: TokenMinus
 */
Minus::Minus(usize location) : Operator(location) {}

Token::Kind
Minus::kind() const
{
  return Token::Kind::Minus;
}

Token::Flags
Minus::flags() const
{
  return Token::Flag::BinaryOperator | Token::Flag::UnaryOperator;
}

std::string
Minus::value() const
{
  return "-";
}

u8
Minus::left_precedence() const
{
  return 11;
}

u8
Minus::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
Minus::construct_binary_expression(const Expression *lhs,
                                        const Expression *rhs) const
{
  return std::make_unique<Subtract>(source_location(), lhs, rhs);
}

std::unique_ptr<Expression>
Minus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<Negate>(source_location(), rhs);
}

/**
 * class: TokenSlash
 */
Slash::Slash(usize location) : Operator(location) {}

Token::Kind
Slash::kind() const
{
  return Token::Kind::Slash;
}

Token::Flags
Slash::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Slash::value() const
{
  return "/";
}

u8
Slash::left_precedence() const
{
  return 12;
}

std::unique_ptr<Expression>
Slash::construct_binary_expression(const Expression *lhs,
                                        const Expression *rhs) const
{
  return std::make_unique<Divide>(source_location(), lhs, rhs);
}

/**
 * class: TokenAsterisk
 */
Asterisk::Asterisk(usize location) : Operator(location) {}

Token::Kind
Asterisk::kind() const
{
  return Token::Kind::Asterisk;
}

Token::Flags
Asterisk::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Asterisk::value() const
{
  return "*";
}

u8
Asterisk::left_precedence() const
{
  return 12;
}

std::unique_ptr<Expression>
Asterisk::construct_binary_expression(const Expression *lhs,
                                           const Expression *rhs) const
{
  return std::make_unique<Multiply>(source_location(), lhs, rhs);
}

/**
 * class: TokenPercent
 */
Percent::Percent(usize location) : Operator(location) {}

Token::Kind
Percent::kind() const
{
  return Token::Kind::Percent;
}

Token::Flags
Percent::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Percent::value() const
{
  return "%";
}

u8
Percent::left_precedence() const
{
  return 12;
}

std::unique_ptr<Expression>
Percent::construct_binary_expression(const Expression *lhs,
                                          const Expression *rhs) const
{
  return std::make_unique<Module>(source_location(), lhs, rhs);
}

/**
 * class: TokenLeftParen
 */
LeftParen::LeftParen(usize location) : Token(location) {}

Token::Kind
LeftParen::kind() const
{
  return Token::Kind::LeftParen;
}

std::string
LeftParen::value() const
{
  return "(";
}

Token::Flags
LeftParen::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenRightParen
 */
RightParen::RightParen(usize location) : Token(location) {}

Token::Kind
RightParen::kind() const
{
  return Token::Kind::RightParen;
}

std::string
RightParen::value() const
{
  return ")";
}

Token::Flags
RightParen::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenLeftSquareBracket
 */
LeftSquareBracket::LeftSquareBracket(usize location) : Token(location)
{}

Token::Kind
LeftSquareBracket::kind() const
{
  return Token::Kind::LeftSquareBracket;
}

std::string
LeftSquareBracket::value() const
{
  return "[";
}

Token::Flags
LeftSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenRightSquareBracket
 */
RightSquareBracket::RightSquareBracket(usize location)
    : Token(location)
{}

Token::Kind
RightSquareBracket::kind() const
{
  return Token::Kind::RightSquareBracket;
}

std::string
RightSquareBracket::value() const
{
  return "]";
}

Token::Flags
RightSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenDoubleLeftSquareBracket
 */
DoubleLeftSquareBracket::DoubleLeftSquareBracket(usize location)
    : Token(location)
{}

Token::Kind
DoubleLeftSquareBracket::kind() const
{
  return Token::Kind::DoubleLeftSquareBracket;
}

std::string
DoubleLeftSquareBracket::value() const
{
  return "[[";
}

Token::Flags
DoubleLeftSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenDoubleRightSquareBracket
 */
DoubleRightSquareBracket::DoubleRightSquareBracket(usize location)
    : Token(location)
{}

Token::Kind
DoubleRightSquareBracket::kind() const
{
  return Token::Kind::DoubleRightSquareBracket;
}

std::string
DoubleRightSquareBracket::value() const
{
  return "]]";
}

Token::Flags
DoubleRightSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenLeftBracket
 */
LeftBracket::LeftBracket(usize location) : Token(location) {}

Token::Kind
LeftBracket::kind() const
{
  return Token::Kind::LeftBracket;
}

std::string
LeftBracket::value() const
{
  return "[";
}

Token::Flags
LeftBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenRightBracket
 */
RightBracket::RightBracket(usize location) : Token(location) {}

Token::Kind
RightBracket::kind() const
{
  return Token::Kind::RightBracket;
}

std::string
RightBracket::value() const
{
  return "]";
}

Token::Flags
RightBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenExclamationMark
 */
ExclamationMark::ExclamationMark(usize location)
    : Operator(location)
{}

Token::Kind
ExclamationMark::kind() const
{
  return Token::Kind::ExclamationMark;
}

Token::Flags
ExclamationMark::flags() const
{
  return Token::Flag::UnaryOperator;
}

std::string
ExclamationMark::value() const
{
  return "!";
}

u8
ExclamationMark::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
ExclamationMark::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<LogicalNot>(source_location(), rhs);
}

/**
 * class: TokenTilde
 */
Tilde::Tilde(usize location) : Operator(location) {}

Token::Kind
Tilde::kind() const
{
  return Token::Kind::Tilde;
}

Token::Flags
Tilde::flags() const
{
  return Token::Flag::UnaryOperator;
}

std::string
Tilde::value() const
{
  return "~";
}

u8
Tilde::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
Tilde::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<BinaryComplement>(source_location(), rhs);
}

/**
 * class: TokenAmpersand
 */
Ampersand::Ampersand(usize location) : Operator(location) {}

Token::Kind
Ampersand::kind() const
{
  return Token::Kind::Ampersand;
}

Token::Flags
Ampersand::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Ampersand::value() const
{
  return "&";
}

u8
Ampersand::left_precedence() const
{
  return 7;
}

std::unique_ptr<Expression>
Ampersand::construct_binary_expression(const Expression *lhs,
                                            const Expression *rhs) const
{
  return std::make_unique<BinaryAnd>(source_location(), lhs, rhs);
}

/**
 * class: TokenDoubleAmpersand
 */
DoubleAmpersand::DoubleAmpersand(usize location)
    : Operator(location)
{}

Token::Kind
DoubleAmpersand::kind() const
{
  return Token::Kind::DoubleAmpersand;
}

Token::Flags
DoubleAmpersand::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
DoubleAmpersand::value() const
{
  return "&&";
}

u8
DoubleAmpersand::left_precedence() const
{
  return 4;
}

std::unique_ptr<Expression>
DoubleAmpersand::construct_binary_expression(const Expression *lhs,
                                                  const Expression *rhs) const
{
  return std::make_unique<LogicalAnd>(source_location(), lhs, rhs);
}

/**
 * class: TokenGreater
 */
Greater::Greater(usize location) : Operator(location) {}

Token::Kind
Greater::kind() const
{
  return Token::Kind::Greater;
}

Token::Flags
Greater::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Greater::value() const
{
  return ">";
}

u8
Greater::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
Greater::construct_binary_expression(const Expression *lhs,
                                          const Expression *rhs) const
{
  return std::make_unique<GreaterThan>(source_location(), lhs, rhs);
}

/**
 * class: TokenDoubleGreater
 */
DoubleGreater::DoubleGreater(usize location) : Operator(location)
{}

Token::Kind
DoubleGreater::kind() const
{
  return Token::Kind::DoubleGreater;
}

Token::Flags
DoubleGreater::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
DoubleGreater::value() const
{
  return ">>";
}

u8
DoubleGreater::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
DoubleGreater::construct_binary_expression(const Expression *lhs,
                                                const Expression *rhs) const
{
  return std::make_unique<RightShift>(source_location(), lhs, rhs);
}

/**
 * class: TokenGreaterEquals
 */
GreaterEquals::GreaterEquals(usize location) : Operator(location)
{}

Token::Kind
GreaterEquals::kind() const
{
  return Token::Kind::GreaterEquals;
}

Token::Flags
GreaterEquals::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
GreaterEquals::value() const
{
  return ">=";
}

u8
GreaterEquals::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
GreaterEquals::construct_binary_expression(const Expression *lhs,
                                                const Expression *rhs) const
{
  return std::make_unique<GreaterOrEqual>(source_location(), lhs, rhs);
}

/**
 * class: TokenLess
 */
Less::Less(usize location) : Operator(location) {}

Token::Kind
Less::kind() const
{
  return Token::Kind::Less;
}

Token::Flags
Less::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Less::value() const
{
  return "<";
}

u8
Less::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
Less::construct_binary_expression(const Expression *lhs,
                                       const Expression *rhs) const
{
  return std::make_unique<LessThan>(source_location(), lhs, rhs);
}

/**
 * class: TokenDoubleLess
 */
DoubleLess::DoubleLess(usize location) : Operator(location) {}

Token::Kind
DoubleLess::kind() const
{
  return Token::Kind::DoubleLess;
}

Token::Flags
DoubleLess::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
DoubleLess::value() const
{
  return "<<";
}

u8
DoubleLess::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
DoubleLess::construct_binary_expression(const Expression *lhs,
                                             const Expression *rhs) const
{
  return std::make_unique<LeftShift>(source_location(), lhs, rhs);
}

/**
 * class: TokenLessEquals
 */
LessEquals::LessEquals(usize location) : Operator(location) {}

Token::Kind
LessEquals::kind() const
{
  return Token::Kind::LessEquals;
}

Token::Flags
LessEquals::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
LessEquals::value() const
{
  return "<=";
}

u8
LessEquals::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
LessEquals::construct_binary_expression(const Expression *lhs,
                                             const Expression *rhs) const
{
  return std::make_unique<LessOrEqual>(source_location(), lhs, rhs);
}

/**
 * class: TokenPipe
 */
Pipe::Pipe(usize location) : Operator(location) {}

Token::Kind
Pipe::kind() const
{
  return Token::Kind::Pipe;
}

Token::Flags
Pipe::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Pipe::value() const
{
  return "|";
}

u8
Pipe::left_precedence() const
{
  return 5;
}

std::unique_ptr<Expression>
Pipe::construct_binary_expression(const Expression *lhs,
                                       const Expression *rhs) const
{
  return std::make_unique<BinaryOr>(source_location(), lhs, rhs);
}

/**
 * class: TokenDoublePipe
 */
DoublePipe::DoublePipe(usize location) : Operator(location) {}

Token::Kind
DoublePipe::kind() const
{
  return Token::Kind::DoublePipe;
}

Token::Flags
DoublePipe::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
DoublePipe::value() const
{
  return "||";
}

u8
DoublePipe::left_precedence() const
{
  return 4;
}

std::unique_ptr<Expression>
DoublePipe::construct_binary_expression(const Expression *lhs,
                                             const Expression *rhs) const
{
  return std::make_unique<LogicalOr>(source_location(), lhs, rhs);
}

/**
 * class: TokenCap
 */
Cap::Cap(usize location) : Operator(location) {}

Token::Kind
Cap::kind() const
{
  return Token::Kind::Cap;
}

Token::Flags
Cap::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Cap::value() const
{
  return "^";
}

u8
Cap::left_precedence() const
{
  return 9;
}

std::unique_ptr<Expression>
Cap::construct_binary_expression(const Expression *lhs,
                                      const Expression *rhs) const
{
  return std::make_unique<Xor>(source_location(), lhs, rhs);
}

/**
 * class: TokenEquals
 */
Equals::Equals(usize location) : Operator(location) {}

Token::Kind
Equals::kind() const
{
  return Token::Kind::Equals;
}

Token::Flags
Equals::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
Equals::value() const
{
  return "=";
}

u8
Equals::left_precedence() const
{
  return 3;
}

std::unique_ptr<Expression>
Equals::construct_binary_expression(const Expression *lhs,
                                         const Expression *rhs) const
{
  SHIT_UNUSED(lhs);
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid call");
}

/**
 * class: TokenDoubleEquals
 */
DoubleEquals::DoubleEquals(usize location) : Operator(location)
{}

Token::Kind
DoubleEquals::kind() const
{
  return Token::Kind::DoubleEquals;
}

Token::Flags
DoubleEquals::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
DoubleEquals::value() const
{
  return "==";
}

u8
DoubleEquals::left_precedence() const
{
  return 3;
}

std::unique_ptr<Expression>
DoubleEquals::construct_binary_expression(const Expression *lhs,
                                               const Expression *rhs) const
{
  return std::make_unique<Equal>(source_location(), lhs, rhs);
}

/**
 * class: TokenExclamationEquals
 */
ExclamationEquals::ExclamationEquals(usize location)
    : Operator(location)
{}

Token::Kind
ExclamationEquals::kind() const
{
  return Token::Kind::DoubleEquals;
}

Token::Flags
ExclamationEquals::flags() const
{
  return Token::Flag::BinaryOperator;
}

std::string
ExclamationEquals::value() const
{
  return "!=";
}

u8
ExclamationEquals::left_precedence() const
{
  return 3;
}

std::unique_ptr<Expression>
ExclamationEquals::construct_binary_expression(const Expression *lhs,
                                                    const Expression *rhs) const
{
  return std::make_unique<NotEqual>(source_location(), lhs, rhs);
}

} /* namespace tokens */

} /* namespace shit */
