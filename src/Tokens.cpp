#include "Tokens.hpp"

#include "Debug.hpp"

namespace shit {

/* Implementations for specific token types */

/**
 * class: Token
 */
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

/**
 * class: TokenIf
 */
TokenIf::TokenIf(usize location) : Token(location) {}

Token::Kind
TokenIf::kind() const
{
  return Token::Kind::If;
}

Token::Flags
TokenIf::flags() const
{
  return Token::Flag::Sentinel;
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

Token::Kind
TokenElse::kind() const
{
  return Token::Kind::Else;
}

Token::Flags
TokenElse::flags() const
{
  return Token::Flag::Sentinel;
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

Token::Kind
TokenThen::kind() const
{
  return Token::Kind::Then;
}

Token::Flags
TokenThen::flags() const
{
  return Token::Flag::Sentinel;
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

Token::Kind
TokenFi::kind() const
{
  return Token::Kind::Fi;
}

Token::Flags
TokenFi::flags() const
{
  return Token::Flag::Sentinel;
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

Token::Kind
TokenEndOfFile::kind() const
{
  return Token::Kind::EndOfFile;
}

Token::Flags
TokenEndOfFile::flags() const
{
  return Token::Flag::Sentinel;
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

Token::Kind
TokenSemicolon::kind() const
{
  return Token::Kind::Semicolon;
}

Token::Flags
TokenSemicolon::flags() const
{
  return Token::Flag::Sentinel;
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

Token::Kind
TokenDot::kind() const
{
  return Token::Kind::Dot;
}

Token::Flags
TokenDot::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
TokenDot::value() const
{
  return ".";
}

/**
 * class: TokenDollar
 */
TokenDollar::TokenDollar(usize location) : Token(location) {}

Token::Kind
TokenDollar::kind() const
{
  return Token::Kind::Dollar;
}

Token::Flags
TokenDollar::flags() const
{
  return Token::Flag::Sentinel;
}

std::string
TokenDollar::value() const
{
  return "$";
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

Token::Kind
TokenNumber::kind() const
{
  return Token::Kind::Number;
}

Token::Flags
TokenNumber::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenString
 */
TokenString::TokenString(usize location, std::string_view sv)
    : TokenValue(location, sv)
{}

Token::Kind
TokenString::kind() const
{
  return Token::Kind::String;
}

Token::Flags
TokenString::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenIdentifier
 */
TokenIdentifier::TokenIdentifier(usize location, std::string_view sv)
    : TokenValue(location, sv)
{}

Token::Kind
TokenIdentifier::kind() const
{
  return Token::Kind::Identifier;
}

Token::Flags
TokenIdentifier::flags() const
{
  return Token::Flag::Value;
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
  SHIT_UNUSED(lhs);
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid binary operator construction of type %d",
                   E(kind()));
}

std::unique_ptr<Expression>
TokenOperator::construct_unary_expression(const Expression *rhs) const
{
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid unary operator construction of type %d", E(kind()));
}

/**
 * class: TokenPlus
 */
TokenPlus::TokenPlus(usize location) : TokenOperator(location) {}

Token::Kind
TokenPlus::kind() const
{
  return Token::Kind::Plus;
}

Token::Flags
TokenPlus::flags() const
{
  return Token::Flag::BinaryOperator | Token::Flag::UnaryOperator;
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

Token::Kind
TokenMinus::kind() const
{
  return Token::Kind::Minus;
}

Token::Flags
TokenMinus::flags() const
{
  return Token::Flag::BinaryOperator | Token::Flag::UnaryOperator;
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

Token::Kind
TokenSlash::kind() const
{
  return Token::Kind::Slash;
}

Token::Flags
TokenSlash::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenAsterisk::kind() const
{
  return Token::Kind::Asterisk;
}

Token::Flags
TokenAsterisk::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenPercent::kind() const
{
  return Token::Kind::Percent;
}

Token::Flags
TokenPercent::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenLeftParen::kind() const
{
  return Token::Kind::LeftParen;
}

std::string
TokenLeftParen::value() const
{
  return "(";
}

Token::Flags
TokenLeftParen::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenRightParen
 */
TokenRightParen::TokenRightParen(usize location) : Token(location) {}

Token::Kind
TokenRightParen::kind() const
{
  return Token::Kind::RightParen;
}

std::string
TokenRightParen::value() const
{
  return ")";
}

Token::Flags
TokenRightParen::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenLeftSquareBracket
 */
TokenLeftSquareBracket::TokenLeftSquareBracket(usize location) : Token(location)
{}

Token::Kind
TokenLeftSquareBracket::kind() const
{
  return Token::Kind::LeftSquareBracket;
}

std::string
TokenLeftSquareBracket::value() const
{
  return "[";
}

Token::Flags
TokenLeftSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenRightSquareBracket
 */
TokenRightSquareBracket::TokenRightSquareBracket(usize location)
    : Token(location)
{}

Token::Kind
TokenRightSquareBracket::kind() const
{
  return Token::Kind::RightSquareBracket;
}

std::string
TokenRightSquareBracket::value() const
{
  return "]";
}

Token::Flags
TokenRightSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenDoubleLeftSquareBracket
 */
TokenDoubleLeftSquareBracket::TokenDoubleLeftSquareBracket(usize location)
    : Token(location)
{}

Token::Kind
TokenDoubleLeftSquareBracket::kind() const
{
  return Token::Kind::DoubleLeftSquareBracket;
}

std::string
TokenDoubleLeftSquareBracket::value() const
{
  return "[[";
}

Token::Flags
TokenDoubleLeftSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenDoubleRightSquareBracket
 */
TokenDoubleRightSquareBracket::TokenDoubleRightSquareBracket(usize location)
    : Token(location)
{}

Token::Kind
TokenDoubleRightSquareBracket::kind() const
{
  return Token::Kind::DoubleRightSquareBracket;
}

std::string
TokenDoubleRightSquareBracket::value() const
{
  return "]]";
}

Token::Flags
TokenDoubleRightSquareBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenLeftBracket
 */
TokenLeftBracket::TokenLeftBracket(usize location) : Token(location) {}

Token::Kind
TokenLeftBracket::kind() const
{
  return Token::Kind::LeftBracket;
}

std::string
TokenLeftBracket::value() const
{
  return "[";
}

Token::Flags
TokenLeftBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenRightBracket
 */
TokenRightBracket::TokenRightBracket(usize location) : Token(location) {}

Token::Kind
TokenRightBracket::kind() const
{
  return Token::Kind::RightBracket;
}

std::string
TokenRightBracket::value() const
{
  return "]";
}

Token::Flags
TokenRightBracket::flags() const
{
  return Token::Flag::Value;
}

/**
 * class: TokenExclamationMark
 */
TokenExclamationMark::TokenExclamationMark(usize location)
    : TokenOperator(location)
{}

Token::Kind
TokenExclamationMark::kind() const
{
  return Token::Kind::ExclamationMark;
}

Token::Flags
TokenExclamationMark::flags() const
{
  return Token::Flag::UnaryOperator;
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

Token::Kind
TokenTilde::kind() const
{
  return Token::Kind::Tilde;
}

Token::Flags
TokenTilde::flags() const
{
  return Token::Flag::UnaryOperator;
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

Token::Kind
TokenAmpersand::kind() const
{
  return Token::Kind::Ampersand;
}

Token::Flags
TokenAmpersand::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenDoubleAmpersand::kind() const
{
  return Token::Kind::DoubleAmpersand;
}

Token::Flags
TokenDoubleAmpersand::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenGreater::kind() const
{
  return Token::Kind::Greater;
}

Token::Flags
TokenGreater::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenDoubleGreater::kind() const
{
  return Token::Kind::DoubleGreater;
}

Token::Flags
TokenDoubleGreater::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenGreaterEquals::kind() const
{
  return Token::Kind::GreaterEquals;
}

Token::Flags
TokenGreaterEquals::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenLess::kind() const
{
  return Token::Kind::Less;
}

Token::Flags
TokenLess::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenDoubleLess::kind() const
{
  return Token::Kind::DoubleLess;
}

Token::Flags
TokenDoubleLess::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenLessEquals::kind() const
{
  return Token::Kind::LessEquals;
}

Token::Flags
TokenLessEquals::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenPipe::kind() const
{
  return Token::Kind::Pipe;
}

Token::Flags
TokenPipe::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenDoublePipe::kind() const
{
  return Token::Kind::DoublePipe;
}

Token::Flags
TokenDoublePipe::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenCap::kind() const
{
  return Token::Kind::Cap;
}

Token::Flags
TokenCap::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenEquals::kind() const
{
  return Token::Kind::Equals;
}

Token::Flags
TokenEquals::flags() const
{
  return Token::Flag::BinaryOperator;
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
  SHIT_UNUSED(lhs);
  SHIT_UNUSED(rhs);
  SHIT_UNREACHABLE("Invalid call");
}

/**
 * class: TokenDoubleEquals
 */
TokenDoubleEquals::TokenDoubleEquals(usize location) : TokenOperator(location)
{}

Token::Kind
TokenDoubleEquals::kind() const
{
  return Token::Kind::DoubleEquals;
}

Token::Flags
TokenDoubleEquals::flags() const
{
  return Token::Flag::BinaryOperator;
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

Token::Kind
TokenExclamationEquals::kind() const
{
  return Token::Kind::DoubleEquals;
}

Token::Flags
TokenExclamationEquals::flags() const
{
  return Token::Flag::BinaryOperator;
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

} /* namespace shit */
