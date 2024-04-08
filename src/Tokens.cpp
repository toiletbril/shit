#include "Tokens.hpp"

#include "Debug.hpp"

/* Implementations for specific token types */

/**
 * class: Token
 */
Token::Token(usize location) : m_location(location) {}

Token::~Token() = default;

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

/**
 * class: TokenIf
 */
TokenIf::TokenIf(usize location) : Token(location) {}

TokenType
TokenIf::type() const
{
  return TokenType::If;
}

TokenFlags
TokenIf::flags() const
{
  return TokenFlag::Sentinel;
}

std::string
TokenIf::value() const
{
  return "If";
}

/**
 * class: TokenElse
 */
TokenElse::TokenElse(usize location) : Token(location) {}

TokenType
TokenElse::type() const
{
  return TokenType::Else;
}

TokenFlags
TokenElse::flags() const
{
  return TokenFlag::Sentinel;
}

std::string
TokenElse::value() const
{
  return "Else";
}

/**
 * class: TokenThen
 */
TokenThen::TokenThen(usize location) : Token(location) {}

TokenType
TokenThen::type() const
{
  return TokenType::Then;
}

TokenFlags
TokenThen::flags() const
{
  return TokenFlag::Sentinel;
}

std::string
TokenThen::value() const
{
  return "Then";
}

/**
 * class: TokenFi
 */
TokenFi::TokenFi(usize location) : Token(location) {}

TokenType
TokenFi::type() const
{
  return TokenType::Fi;
}

TokenFlags
TokenFi::flags() const
{
  return TokenFlag::Sentinel;
}

std::string
TokenFi::value() const
{
  return "Fi";
}

/**
 * class: TokenEndOfFile
 */
TokenEndOfFile::TokenEndOfFile(usize location) : Token(location) {}

TokenType
TokenEndOfFile::type() const
{
  return TokenType::EndOfFile;
}

TokenFlags
TokenEndOfFile::flags() const
{
  return TokenFlag::Sentinel;
}

std::string
TokenEndOfFile::value() const
{
  return "EOF";
}

/**
 * class: TokenSemicolon
 */
TokenSemicolon::TokenSemicolon(usize location) : Token(location) {}

TokenType
TokenSemicolon::type() const
{
  return TokenType::Semicolon;
}

TokenFlags
TokenSemicolon::flags() const
{
  return TokenFlag::Sentinel;
}

std::string
TokenSemicolon::value() const
{
  return ";";
}

/**
 * class: TokenDot
 */
TokenDot::TokenDot(usize location) : Token(location) {}

TokenType
TokenDot::type() const
{
  return TokenType::Dot;
}

TokenFlags
TokenDot::flags() const
{
  return TokenFlag::Sentinel;
}

std::string
TokenDot::value() const
{
  return ".";
}

/**
 * class: TokenValue
 */
TokenValue::TokenValue(usize location, std::string_view sv)
    : Token(location), m_value(sv)
{}

std::string
TokenValue::value() const
{
  return m_value;
}

/**
 * class: TokenNumber
 */
TokenNumber::TokenNumber(usize location, std::string_view sv)
    : TokenValue(location, sv)
{}

TokenType
TokenNumber::type() const
{
  return TokenType::Number;
}

TokenFlags
TokenNumber::flags() const
{
  return TokenFlag::Value;
}

/**
 * class: TokenString
 */
TokenString::TokenString(usize location, std::string_view sv)
    : TokenValue(location, sv)
{}

TokenType
TokenString::type() const
{
  return TokenType::String;
}

TokenFlags
TokenString::flags() const
{
  return TokenFlag::Value;
}

/**
 * class: TokenIdentifier
 */
TokenIdentifier::TokenIdentifier(usize location, std::string_view sv)
    : TokenValue(location, sv)
{}

TokenType
TokenIdentifier::type() const
{
  return TokenType::Identifier;
}

TokenFlags
TokenIdentifier::flags() const
{
  return TokenFlag::Value;
}

/**
 * class: TokenTokenOperator
 */
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
  UNREACHABLE("Invalid binary operator construction of type %d", type());
}

std::unique_ptr<Expression>
TokenOperator::construct_unary_expression(const Expression *rhs) const
{
  UNUSED(rhs);
  UNREACHABLE("Invalid unary operator construction of type %d", type());
}

/**
 * class: TokenPlus
 */
TokenPlus::TokenPlus(usize location) : TokenOperator(location) {}

TokenType
TokenPlus::type() const
{
  return TokenType::Plus;
}

TokenFlags
TokenPlus::flags() const
{
  return TokenFlag::BinaryOperator | TokenFlag::UnaryOperator;
}

std::string
TokenPlus::value() const
{
  return "+";
}

u8
TokenPlus::left_precedence() const
{
  return 11;
}

u8
TokenPlus::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
TokenPlus::construct_binary_expression(const Expression *lhs,
                                       const Expression *rhs) const
{
  return std::make_unique<Add>(location(), lhs, rhs);
}

std::unique_ptr<Expression>
TokenPlus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<Unnegate>(location(), rhs);
}

/**
 * class: TokenMinus
 */
TokenMinus::TokenMinus(usize location) : TokenOperator(location) {}

TokenType
TokenMinus::type() const
{
  return TokenType::Minus;
}

TokenFlags
TokenMinus::flags() const
{
  return TokenFlag::BinaryOperator | TokenFlag::UnaryOperator;
}

std::string
TokenMinus::value() const
{
  return "-";
}

u8
TokenMinus::left_precedence() const
{
  return 11;
}

u8
TokenMinus::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
TokenMinus::construct_binary_expression(const Expression *lhs,
                                        const Expression *rhs) const
{
  return std::make_unique<Subtract>(location(), lhs, rhs);
}

std::unique_ptr<Expression>
TokenMinus::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<Negate>(location(), rhs);
}

/**
 * class: TokenSlash
 */
TokenSlash::TokenSlash(usize location) : TokenOperator(location) {}

TokenType
TokenSlash::type() const
{
  return TokenType::Slash;
}

TokenFlags
TokenSlash::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenSlash::value() const
{
  return "/";
}

u8
TokenSlash::left_precedence() const
{
  return 12;
}

std::unique_ptr<Expression>
TokenSlash::construct_binary_expression(const Expression *lhs,
                                        const Expression *rhs) const
{
  return std::make_unique<Divide>(location(), lhs, rhs);
}

/**
 * class: TokenAsterisk
 */
TokenAsterisk::TokenAsterisk(usize location) : TokenOperator(location) {}

TokenType
TokenAsterisk::type() const
{
  return TokenType::Asterisk;
}

TokenFlags
TokenAsterisk::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenAsterisk::value() const
{
  return "*";
}

u8
TokenAsterisk::left_precedence() const
{
  return 12;
}

std::unique_ptr<Expression>
TokenAsterisk::construct_binary_expression(const Expression *lhs,
                                           const Expression *rhs) const
{
  return std::make_unique<Multiply>(location(), lhs, rhs);
}

/**
 * class: TokenPercent
 */
TokenPercent::TokenPercent(usize location) : TokenOperator(location) {}

TokenType
TokenPercent::type() const
{
  return TokenType::Percent;
}

TokenFlags
TokenPercent::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenPercent::value() const
{
  return "%";
}

u8
TokenPercent::left_precedence() const
{
  return 12;
}

std::unique_ptr<Expression>
TokenPercent::construct_binary_expression(const Expression *lhs,
                                          const Expression *rhs) const
{
  return std::make_unique<Module>(location(), lhs, rhs);
}

/**
 * class: TokenLeftParen
 */
TokenLeftParen::TokenLeftParen(usize location) : Token(location) {}

TokenType
TokenLeftParen::type() const
{
  return TokenType::LeftParen;
}

std::string
TokenLeftParen::value() const
{
  return "(";
}

TokenFlags
TokenLeftParen::flags() const
{
  return TokenFlag::Value;
}

/**
 * class: TokenRightParen
 */
TokenRightParen::TokenRightParen(usize location) : Token(location) {}

TokenType
TokenRightParen::type() const
{
  return TokenType::RightParen;
}

std::string
TokenRightParen::value() const
{
  return ")";
}

TokenFlags
TokenRightParen::flags() const
{
  return TokenFlag::Value;
}

/**
 * class: TokenExclamationMark
 */
TokenExclamationMark::TokenExclamationMark(usize location)
    : TokenOperator(location)
{}

TokenType
TokenExclamationMark::type() const
{
  return TokenType::Tilde;
}

TokenFlags
TokenExclamationMark::flags() const
{
  return TokenFlag::UnaryOperator;
}

std::string
TokenExclamationMark::value() const
{
  return "!";
}

u8
TokenExclamationMark::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
TokenExclamationMark::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<LogicalNot>(location(), rhs);
}

/**
 * class: TokenTilde
 */
TokenTilde::TokenTilde(usize location) : TokenOperator(location) {}

TokenType
TokenTilde::type() const
{
  return TokenType::Tilde;
}

TokenFlags
TokenTilde::flags() const
{
  return TokenFlag::UnaryOperator;
}

std::string
TokenTilde::value() const
{
  return "~";
}

u8
TokenTilde::unary_precedence() const
{
  return 13;
}

std::unique_ptr<Expression>
TokenTilde::construct_unary_expression(const Expression *rhs) const
{
  return std::make_unique<BinaryComplement>(location(), rhs);
}

/**
 * class: TokenAmpersand
 */
TokenAmpersand::TokenAmpersand(usize location) : TokenOperator(location) {}

TokenType
TokenAmpersand::type() const
{
  return TokenType::Ampersand;
}

TokenFlags
TokenAmpersand::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenAmpersand::value() const
{
  return "&";
}

u8
TokenAmpersand::left_precedence() const
{
  return 7;
}

std::unique_ptr<Expression>
TokenAmpersand::construct_binary_expression(const Expression *lhs,
                                            const Expression *rhs) const
{
  return std::make_unique<BinaryAnd>(location(), lhs, rhs);
}

/**
 * class: TokenDoubleAmpersand
 */
TokenDoubleAmpersand::TokenDoubleAmpersand(usize location)
    : TokenOperator(location)
{}

TokenType
TokenDoubleAmpersand::type() const
{
  return TokenType::DoubleAmpersand;
}

TokenFlags
TokenDoubleAmpersand::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenDoubleAmpersand::value() const
{
  return "&&";
}

u8
TokenDoubleAmpersand::left_precedence() const
{
  return 4;
}

std::unique_ptr<Expression>
TokenDoubleAmpersand::construct_binary_expression(const Expression *lhs,
                                                  const Expression *rhs) const
{
  return std::make_unique<LogicalAnd>(location(), lhs, rhs);
}

/**
 * class: TokenGreater
 */
TokenGreater::TokenGreater(usize location) : TokenOperator(location) {}

TokenType
TokenGreater::type() const
{
  return TokenType::Greater;
}

TokenFlags
TokenGreater::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenGreater::value() const
{
  return ">";
}

u8
TokenGreater::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
TokenGreater::construct_binary_expression(const Expression *lhs,
                                          const Expression *rhs) const
{
  return std::make_unique<GreaterThan>(location(), lhs, rhs);
}

/**
 * class: TokenDoubleGreater
 */
TokenDoubleGreater::TokenDoubleGreater(usize location) : TokenOperator(location)
{}

TokenType
TokenDoubleGreater::type() const
{
  return TokenType::DoubleGreater;
}

TokenFlags
TokenDoubleGreater::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenDoubleGreater::value() const
{
  return ">>";
}

u8
TokenDoubleGreater::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
TokenDoubleGreater::construct_binary_expression(const Expression *lhs,
                                                const Expression *rhs) const
{
  return std::make_unique<RightShift>(location(), lhs, rhs);
}

/**
 * class: TokenGreaterEquals
 */
TokenGreaterEquals::TokenGreaterEquals(usize location) : TokenOperator(location)
{}

TokenType
TokenGreaterEquals::type() const
{
  return TokenType::GreaterEquals;
}

TokenFlags
TokenGreaterEquals::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenGreaterEquals::value() const
{
  return ">=";
}

u8
TokenGreaterEquals::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
TokenGreaterEquals::construct_binary_expression(const Expression *lhs,
                                                const Expression *rhs) const
{
  return std::make_unique<GreaterOrEqual>(location(), lhs, rhs);
}

/**
 * class: TokenLess
 */
TokenLess::TokenLess(usize location) : TokenOperator(location) {}

TokenType
TokenLess::type() const
{
  return TokenType::Less;
}

TokenFlags
TokenLess::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenLess::value() const
{
  return "<";
}

u8
TokenLess::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
TokenLess::construct_binary_expression(const Expression *lhs,
                                       const Expression *rhs) const
{
  return std::make_unique<LessThan>(location(), lhs, rhs);
}

/**
 * class: TokenDoubleLess
 */
TokenDoubleLess::TokenDoubleLess(usize location) : TokenOperator(location) {}

TokenType
TokenDoubleLess::type() const
{
  return TokenType::DoubleLess;
}

TokenFlags
TokenDoubleLess::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenDoubleLess::value() const
{
  return "<<";
}

u8
TokenDoubleLess::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
TokenDoubleLess::construct_binary_expression(const Expression *lhs,
                                             const Expression *rhs) const
{
  return std::make_unique<LeftShift>(location(), lhs, rhs);
}

/**
 * class: TokenLessEquals
 */
TokenLessEquals::TokenLessEquals(usize location) : TokenOperator(location) {}

TokenType
TokenLessEquals::type() const
{
  return TokenType::LessEquals;
}

TokenFlags
TokenLessEquals::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenLessEquals::value() const
{
  return "<=";
}

u8
TokenLessEquals::left_precedence() const
{
  return 8;
}

std::unique_ptr<Expression>
TokenLessEquals::construct_binary_expression(const Expression *lhs,
                                             const Expression *rhs) const
{
  return std::make_unique<LessOrEqual>(location(), lhs, rhs);
}

/**
 * class: TokenPipe
 */
TokenPipe::TokenPipe(usize location) : TokenOperator(location) {}

TokenType
TokenPipe::type() const
{
  return TokenType::Pipe;
}

TokenFlags
TokenPipe::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenPipe::value() const
{
  return "|";
}

u8
TokenPipe::left_precedence() const
{
  return 5;
}

std::unique_ptr<Expression>
TokenPipe::construct_binary_expression(const Expression *lhs,
                                       const Expression *rhs) const
{
  return std::make_unique<BinaryOr>(location(), lhs, rhs);
}

/**
 * class: TokenDoublePipe
 */
TokenDoublePipe::TokenDoublePipe(usize location) : TokenOperator(location) {}

TokenType
TokenDoublePipe::type() const
{
  return TokenType::DoublePipe;
}

TokenFlags
TokenDoublePipe::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenDoublePipe::value() const
{
  return "||";
}

u8
TokenDoublePipe::left_precedence() const
{
  return 4;
}

std::unique_ptr<Expression>
TokenDoublePipe::construct_binary_expression(const Expression *lhs,
                                             const Expression *rhs) const
{
  return std::make_unique<LogicalOr>(location(), lhs, rhs);
}

/**
 * class: TokenCap
 */
TokenCap::TokenCap(usize location) : TokenOperator(location) {}

TokenType
TokenCap::type() const
{
  return TokenType::Cap;
}

TokenFlags
TokenCap::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenCap::value() const
{
  return "^";
}

u8
TokenCap::left_precedence() const
{
  return 9;
}

std::unique_ptr<Expression>
TokenCap::construct_binary_expression(const Expression *lhs,
                                      const Expression *rhs) const
{
  return std::make_unique<Xor>(location(), lhs, rhs);
}

/**
 * class: TokenEquals
 */
TokenEquals::TokenEquals(usize location) : TokenOperator(location) {}

TokenType
TokenEquals::type() const
{
  return TokenType::Equals;
}

TokenFlags
TokenEquals::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenEquals::value() const
{
  return "=";
}

u8
TokenEquals::left_precedence() const
{
  return 3;
}

std::unique_ptr<Expression>
TokenEquals::construct_binary_expression(const Expression *lhs,
                                         const Expression *rhs) const
{
  UNUSED(lhs);
  UNUSED(rhs);
  INSIST(false, "todo");
}

/**
 * class: TokenDoubleEquals
 */
TokenDoubleEquals::TokenDoubleEquals(usize location) : TokenOperator(location)
{}

TokenType
TokenDoubleEquals::type() const
{
  return TokenType::DoubleEquals;
}

TokenFlags
TokenDoubleEquals::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenDoubleEquals::value() const
{
  return "==";
}

u8
TokenDoubleEquals::left_precedence() const
{
  return 3;
}

std::unique_ptr<Expression>
TokenDoubleEquals::construct_binary_expression(const Expression *lhs,
                                               const Expression *rhs) const
{
  return std::make_unique<Equal>(location(), lhs, rhs);
}

/**
 * class: TokenExclamationEquals
 */
TokenExclamationEquals::TokenExclamationEquals(usize location)
    : TokenOperator(location)
{}

TokenType
TokenExclamationEquals::type() const
{
  return TokenType::DoubleEquals;
}

TokenFlags
TokenExclamationEquals::flags() const
{
  return TokenFlag::BinaryOperator;
}

std::string
TokenExclamationEquals::value() const
{
  return "!=";
}

u8
TokenExclamationEquals::left_precedence() const
{
  return 3;
}

std::unique_ptr<Expression>
TokenExclamationEquals::construct_binary_expression(const Expression *lhs,
                                                    const Expression *rhs) const
{
  return std::make_unique<NotEqual>(location(), lhs, rhs);
}
