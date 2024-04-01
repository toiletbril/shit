#include "Debug.hpp"
#include "Tokens.hpp"

/* Implementations for specific token types */

/* class: Token */
Token::Token(usize location) : m_location(location) {}

usize
Token::location() const
{
  return m_location;
}

std::string
Token::to_ast_string() const
{
  return value();
}

/* class: EndOfFile */
EndOfFile::EndOfFile(usize location) : Token(location) {}

TokenType
EndOfFile::type() const
{
  return TokenType::EndOfFile;
}

OperatorFlags
EndOfFile::operator_flags() const
{
  return OperatorFlag::NotAnOperator;
}

std::string
EndOfFile::value() const
{
  return "EOF";
}

/* class: Number */
Number::Number(usize location, std::string_view sv)
    : Token(location), m_value(sv)
{
}

TokenType
Number::type() const
{
  return TokenType::Number;
}

OperatorFlags
Number::operator_flags() const
{
  return OperatorFlag::NotAnOperator;
}

std::string
Number::value() const
{
  return m_value;
}

/* class: TokenOperator */
TokenOperator::TokenOperator(usize location) : Token(location) {}

u8
TokenOperator::left_precedence() const
{
  return 0;
}

u8
TokenOperator::unary_precedence() const
{
  return 0;
}

bool
TokenOperator::binary_left_associative() const
{
  return true;
}

std::unique_ptr<Expression>
TokenOperator::construct_binary_expression(const Expression *lhs,
                                           const Expression *rhs) const
{
  UNUSED(lhs);
  UNUSED(rhs);
  UNREACHABLE();
}

std::unique_ptr<Expression>
TokenOperator::construct_unary_expression(const Expression *rhs) const
{
  UNUSED(rhs);
  UNREACHABLE();
}

/* class: Plus */
Plus::Plus(usize location) : TokenOperator(location) {}

TokenType
Plus::type() const
{
  return TokenType::Plus;
}

OperatorFlags
Plus::operator_flags() const
{
  return OperatorFlag::Binary | OperatorFlag::Unary;
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
  return std::make_unique<Add>(lhs, rhs);
}

std::unique_ptr<Expression>
Plus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<Unnegate>(rhs);
}

/* class: Minus */
Minus::Minus(usize location) : TokenOperator(location) {}

TokenType
Minus::type() const
{
  return TokenType::Minus;
}

OperatorFlags
Minus::operator_flags() const
{
  return OperatorFlag::Binary | OperatorFlag::Unary;
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
  return std::make_unique<Subtract>(lhs, rhs);
}

std::unique_ptr<Expression>
Minus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<Negate>(rhs);
}

/* class: Slash */
Slash::Slash(usize location) : TokenOperator(location) {}

TokenType
Slash::type() const
{
  return TokenType::Slash;
}

OperatorFlags
Slash::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<Divide>(lhs, rhs);
}

/* class: Asterisk */
Asterisk::Asterisk(usize location) : TokenOperator(location) {}

TokenType
Asterisk::type() const
{
  return TokenType::Asterisk;
}

OperatorFlags
Asterisk::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<Multiply>(lhs, rhs);
}

/* class: Percent */
Percent::Percent(usize location) : TokenOperator(location) {}

TokenType
Percent::type() const
{
  return TokenType::Percent;
}

OperatorFlags
Percent::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<Module>(lhs, rhs);
}

/* class: LeftParen */
LeftParen::LeftParen(usize location) : Token(location) {}

TokenType
LeftParen::type() const
{
  return TokenType::LeftParen;
}

std::string
LeftParen::value() const
{
  return "(";
}

OperatorFlags
LeftParen::operator_flags() const
{
  return OperatorFlag::NotAnOperator;
}

/* class: RightParen */
RightParen::RightParen(usize location) : Token(location) {}

TokenType
RightParen::type() const
{
  return TokenType::RightParen;
}

std::string
RightParen::value() const
{
  return ")";
}

OperatorFlags
RightParen::operator_flags() const
{
  return OperatorFlag::NotAnOperator;
}

/* class: Tilde */
Tilde::Tilde(usize location) : TokenOperator(location) {}

TokenType
Tilde::type() const
{
  return TokenType::Tilde;
}

OperatorFlags
Tilde::operator_flags() const
{
  return OperatorFlag::Unary;
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
  return std::make_unique<BinaryComplement>(rhs);
}

/* class: Ampersand */
Ampersand::Ampersand(usize location) : TokenOperator(location) {}

TokenType
Ampersand::type() const
{
  return TokenType::Ampersand;
}

OperatorFlags
Ampersand::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<BinaryAnd>(lhs, rhs);
}

/* class: DoubleAmpersand */
DoubleAmpersand::DoubleAmpersand(usize location) : TokenOperator(location) {}

TokenType
DoubleAmpersand::type() const
{
  return TokenType::DoubleAmpersand;
}

OperatorFlags
DoubleAmpersand::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<LogicalAnd>(lhs, rhs);
}

/* class: Greater */
Greater::Greater(usize location) : TokenOperator(location) {}

TokenType
Greater::type() const
{
  return TokenType::Greater;
}

OperatorFlags
Greater::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<GreaterThan>(lhs, rhs);
}

/* class: DoubleGreater */
DoubleGreater::DoubleGreater(usize location) : TokenOperator(location) {}

TokenType
DoubleGreater::type() const
{
  return TokenType::DoubleGreater;
}

OperatorFlags
DoubleGreater::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<RightShift>(lhs, rhs);
}

/* class: GreaterEquals */
GreaterEquals::GreaterEquals(usize location) : TokenOperator(location) {}

TokenType
GreaterEquals::type() const
{
  return TokenType::GreaterEquals;
}

OperatorFlags
GreaterEquals::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<GreaterOrEqual>(lhs, rhs);
}

/* class: Less */
Less::Less(usize location) : TokenOperator(location) {}

TokenType
Less::type() const
{
  return TokenType::Less;
}

OperatorFlags
Less::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<LessThan>(lhs, rhs);
}

/* class: DoubleLess */
DoubleLess::DoubleLess(usize location) : TokenOperator(location) {}

TokenType
DoubleLess::type() const
{
  return TokenType::DoubleLess;
}

OperatorFlags
DoubleLess::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<LeftShift>(lhs, rhs);
}

/* class: LessEquals */
LessEquals::LessEquals(usize location) : TokenOperator(location) {}

TokenType
LessEquals::type() const
{
  return TokenType::LessEquals;
}

OperatorFlags
LessEquals::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<LessOrEqual>(lhs, rhs);
}

/* class: Pipe */
Pipe::Pipe(usize location) : TokenOperator(location) {}

TokenType
Pipe::type() const
{
  return TokenType::Pipe;
}

OperatorFlags
Pipe::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<BinaryOr>(lhs, rhs);
}

/* class: DoublePipe */
DoublePipe::DoublePipe(usize location) : TokenOperator(location) {}

TokenType
DoublePipe::type() const
{
  return TokenType::DoublePipe;
}

OperatorFlags
DoublePipe::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<LogicalOr>(lhs, rhs);
}

/* class: Cap */
Cap::Cap(usize location) : TokenOperator(location) {}

TokenType
Cap::type() const
{
  return TokenType::Cap;
}

OperatorFlags
Cap::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<Xor>(lhs, rhs);
}

/* class: Equals */
Equals::Equals(usize location) : TokenOperator(location) {}

TokenType
Equals::type() const
{
  return TokenType::Equals;
}

OperatorFlags
Equals::operator_flags() const
{
  return OperatorFlag::Binary;
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
  UNUSED(lhs);
  UNUSED(rhs);
  INSIST(false, "todo");
}

/* class: DoubleEquals */
DoubleEquals::DoubleEquals(usize location) : TokenOperator(location) {}

TokenType
DoubleEquals::type() const
{
  return TokenType::DoubleEquals;
}

OperatorFlags
DoubleEquals::operator_flags() const
{
  return OperatorFlag::Binary;
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
  return std::make_unique<Equality>(lhs, rhs);
}
