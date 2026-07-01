#include "StringView.hpp"

#include "ErrorOr.hpp"
#include "IntBase.hpp"

#include <limits>

namespace shit {

namespace utils {
fn parse_decimal_integer(StringView text) throws -> ErrorOr<i64>;
fn parse_integer_in_base(StringView text, int_base base) throws -> ErrorOr<i64>;
} // namespace utils

template <class T> static fn narrow_integer(i64 value) throws -> ErrorOr<T>
{
  static_assert(std::is_integral_v<T>, "narrow_integer targets an integer");
  if constexpr (std::is_same_v<T, i64>) {
    return value;
  } else if constexpr (std::is_signed_v<T>) {
    if (value < static_cast<i64>(std::numeric_limits<T>::min()) ||
        value > static_cast<i64>(std::numeric_limits<T>::max()))
      return Error{"integer value out of range"};
    return static_cast<T>(value);
  } else {
    if (value < 0 || static_cast<u64>(value) >
                         static_cast<u64>(std::numeric_limits<T>::max()))
      return Error{"integer value out of range"};
    return static_cast<T>(value);
  }
}

template <class T> fn StringView::to() const throws -> ErrorOr<T>
{
  if constexpr (is_tagged_int_v<T>) {
    using U = typename T::underlying;
    return T{TRY(
        narrow_integer<U>(TRY(utils::parse_integer_in_base(*this, T::base))))};
  } else {
    static_assert(std::is_integral_v<T>, "StringView::to parses an integer");
    return narrow_integer<T>(TRY(utils::parse_decimal_integer(*this)));
  }
}

#define SHIT_STRINGVIEW_TO(T) template ErrorOr<T> StringView::to<T>() const;
SHIT_STRINGVIEW_TO(i16)
SHIT_STRINGVIEW_TO(u16)
SHIT_STRINGVIEW_TO(i32)
SHIT_STRINGVIEW_TO(u32)
SHIT_STRINGVIEW_TO(i64)
SHIT_STRINGVIEW_TO(u64)
SHIT_STRINGVIEW_TO(bi16)
SHIT_STRINGVIEW_TO(bi32)
SHIT_STRINGVIEW_TO(bi64)
SHIT_STRINGVIEW_TO(bu16)
SHIT_STRINGVIEW_TO(bu32)
SHIT_STRINGVIEW_TO(bu64)
SHIT_STRINGVIEW_TO(oi16)
SHIT_STRINGVIEW_TO(oi32)
SHIT_STRINGVIEW_TO(oi64)
SHIT_STRINGVIEW_TO(ou16)
SHIT_STRINGVIEW_TO(ou32)
SHIT_STRINGVIEW_TO(ou64)
SHIT_STRINGVIEW_TO(hi16)
SHIT_STRINGVIEW_TO(hi32)
SHIT_STRINGVIEW_TO(hi64)
SHIT_STRINGVIEW_TO(hu16)
SHIT_STRINGVIEW_TO(hu32)
SHIT_STRINGVIEW_TO(hu64)
#undef SHIT_STRINGVIEW_TO

StringView::StringView(const char *cstr) wontthrow
    : data(cstr),
      length(cstr != nullptr ? std::strlen(cstr) : 0)
{}

fn StringView::find_character(char wanted) const wontthrow -> Maybe<usize>
{
  if (length == 0) return None;
  let const found =
      std::memchr(data, static_cast<unsigned char>(wanted), length);
  if (found == nullptr) return None;

  return static_cast<usize>(static_cast<const char *>(found) - data);
}

fn StringView::substring(usize start) const wontthrow -> StringView
{
  if (start >= length) return StringView{data + length, 0};
  return StringView{data + start, length - start};
}

fn StringView::substring_of_length(usize start, usize count) const wontthrow
    -> StringView
{
  if (start >= length) return StringView{data + length, 0};
  usize remaining = length - start;

  return StringView{data + start, count < remaining ? count : remaining};
}

fn StringView::starts_with(StringView prefix) const wontthrow -> bool
{
  if (prefix.length > length) return false;
  /* The length guard keeps a null data pointer out of memcmp. */
  return prefix.length == 0 ||
         std::memcmp(data, prefix.data, prefix.length) == 0;
}

} // namespace shit
