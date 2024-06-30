#include "Tokens.hpp"

#include "Debug.hpp"
#include "Errors.hpp"

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
  return raw_string();
}

namespace tokens {

/**
 * class: If
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
If::raw_string() const
{
  return "If";
}

/**
 * class: Else
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
Else::raw_string() const
{
  return "Else";
}

/**
 * class: Then
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
Then::raw_string() const
{
  return "Then";
}

/**
 * class: Fi
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
Fi::raw_string() const
{
  return "Fi";
}

/**
 * class: EndOfFile
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
EndOfFile::raw_string() const
{
  return "EOF";
}

/**
 * class: Semicolon
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
Semicolon::raw_string() const
{
  return ";";
}

/**
 * class: Dot
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
Dot::raw_string() const
{
  return ".";
}

/**
 * class: Dollar
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
Dollar::raw_string() const
{
  return "$";
}

/**
 * class: Value
 */
Value::Value(usize location, std::string_view sv) : Token(location), m_value(sv)
{}

std::string
Value::raw_string() const
{
  return m_value;
}

/**
 * class: Number
 */
Number::Number(usize location, std::string_view sv) : Value(location, sv) {}

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
 * class: String
 */
String::String(usize location, char quote_char, std::string_view sv)
    : Value(location, sv), m_quote_char(quote_char)
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

char
String::quote_char() const
{
  return m_quote_char;
}

/**
 * class: ExpandableString
 */
Expandable::Expandable(usize location, std::string_view sv)
    : Value(location, sv)
{}

Token::Kind
Expandable::kind() const
{
  return Token::Kind::Expandable;
}

Token::Flags
Expandable::flags() const
{
  return Token::Flag::Value | Token::Flag::Expandable;
}

/**
 * class: Identifier
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
 * class: Plus
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
Plus::raw_string() const
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
  return std::make_unique<expressions::Add>(source_location(), lhs, rhs);
}

std::unique_ptr<Expression>
Plus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<expressions::Unnegate>(source_location(), rhs);
}

/**
 * class: Minus
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
Minus::raw_string() const
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
  return std::make_unique<expressions::Subtract>(source_location(), lhs, rhs);
}

std::unique_ptr<Expression>
Minus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<expressions::Negate>(source_location(), rhs);
}

/**
 * class: Slash
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
Slash::raw_string() const
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
  return std::make_unique<expressions::Divide>(source_location(), lhs, rhs);
}

/**
 * class: Asterisk
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
Asterisk::raw_string() const
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
  return std::make_unique<expressions::Multiply>(source_location(), lhs, rhs);
}

/**
 * class: Percent
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
Percent::raw_string() const
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
  return std::make_unique<expressions::Module>(source_location(), lhs, rhs);
}

/**
 * class: LeftParen
 */
LeftParen::LeftParen(usize location) : Token(location) {}

Token::Kind
LeftParen::kind() const
{
  return Token::Kind::LeftParen;
}

std::string
LeftParen::raw_string() const
{
  return "(";
}

Token::Flags
LeftParen::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: RightParen
 */
RightParen::RightParen(usize location) : Token(location) {}

Token::Kind
RightParen::kind() const
{
  return Token::Kind::RightParen;
}

std::string
RightParen::raw_string() const
{
  return ")";
}

Token::Flags
RightParen::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: LeftSquareBracket
 */
LeftSquareBracket::LeftSquareBracket(usize location) : Token(location) {}

Token::Kind
LeftSquareBracket::kind() const
{
  return Token::Kind::LeftSquareBracket;
}

std::string
LeftSquareBracket::raw_string() const
{
  return "[";
}

Token::Flags
LeftSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: RightSquareBracket
 */
RightSquareBracket::RightSquareBracket(usize location) : Token(location) {}

Token::Kind
RightSquareBracket::kind() const
{
  return Token::Kind::RightSquareBracket;
}

std::string
RightSquareBracket::raw_string() const
{
  return "]";
}

Token::Flags
RightSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: DoubleLeftSquareBracket
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
DoubleLeftSquareBracket::raw_string() const
{
  return "[[";
}

Token::Flags
DoubleLeftSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: DoubleRightSquareBracket
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
DoubleRightSquareBracket::raw_string() const
{
  return "]]";
}

Token::Flags
DoubleRightSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: LeftBracket
 */
LeftBracket::LeftBracket(usize location) : Token(location) {}

Token::Kind
LeftBracket::kind() const
{
  return Token::Kind::LeftBracket;
}

std::string
LeftBracket::raw_string() const
{
  return "[";
}

Token::Flags
LeftBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: RightBracket
 */
RightBracket::RightBracket(usize location) : Token(location) {}

Token::Kind
RightBracket::kind() const
{
  return Token::Kind::RightBracket;
}

std::string
RightBracket::raw_string() const
{
  return "]";
}

Token::Flags
RightBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: ExclamationMark
 */
ExclamationMark::ExclamationMark(usize location) : Operator(location) {}

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
ExclamationMark::raw_string() const
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
  return std::make_unique<expressions::LogicalNot>(source_location(), rhs);
}

/**
 * class: Tilde
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
Tilde::raw_string() const
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
  return std::make_unique<expressions::BinaryComplement>(source_location(),
                                                         rhs);
}

/**
 * class: Ampersand
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
Ampersand::raw_string() const
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
  return std::make_unique<expressions::BinaryAnd>(source_location(), lhs, rhs);
}

/**
 * class: DoubleAmpersand
 */
DoubleAmpersand::DoubleAmpersand(usize location) : Operator(location) {}

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
DoubleAmpersand::raw_string() const
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
  return std::make_unique<expressions::LogicalAnd>(source_location(), lhs, rhs);
}

/**
 * class: Greater
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
Greater::raw_string() const
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
  return std::make_unique<expressions::GreaterThan>(source_location(), lhs,
                                                    rhs);
}

/**
 * class: DoubleGreater
 */
DoubleGreater::DoubleGreater(usize location) : Operator(location) {}

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
DoubleGreater::raw_string() const
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
  return std::make_unique<expressions::RightShift>(source_location(), lhs, rhs);
}

/**
 * class: GreaterEquals
 */
GreaterEquals::GreaterEquals(usize location) : Operator(location) {}

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
GreaterEquals::raw_string() const
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
  return std::make_unique<expressions::GreaterOrEqual>(source_location(), lhs,
                                                       rhs);
}

/**
 * class: Less
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
Less::raw_string() const
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
  return std::make_unique<expressions::LessThan>(source_location(), lhs, rhs);
}

/**
 * class: DoubleLess
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
DoubleLess::raw_string() const
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
  return std::make_unique<expressions::LeftShift>(source_location(), lhs, rhs);
}

/**
 * class: LessEquals
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
LessEquals::raw_string() const
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
  return std::make_unique<expressions::LessOrEqual>(source_location(), lhs,
                                                    rhs);
}

/**
 * class: Pipe
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
Pipe::raw_string() const
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
  return std::make_unique<expressions::BinaryOr>(source_location(), lhs, rhs);
}

/**
 * class: DoublePipe
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
DoublePipe::raw_string() const
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
  return std::make_unique<expressions::LogicalOr>(source_location(), lhs, rhs);
}

/**
 * class: Cap
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
Cap::raw_string() const
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
  return std::make_unique<expressions::Xor>(source_location(), lhs, rhs);
}

/**
 * class: Equals
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
Equals::raw_string() const
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
 * class: DoubleEquals
 */
DoubleEquals::DoubleEquals(usize location) : Operator(location) {}

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
DoubleEquals::raw_string() const
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
  return std::make_unique<expressions::Equal>(source_location(), lhs, rhs);
}

/**
 * class: ExclamationEquals
 */
ExclamationEquals::ExclamationEquals(usize location) : Operator(location) {}

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
ExclamationEquals::raw_string() const
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
  return std::make_unique<expressions::NotEqual>(source_location(), lhs, rhs);
}

} /* namespace tokens */

} /* namespace shit */
