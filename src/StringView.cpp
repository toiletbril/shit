#include "StringView.hpp"

namespace shit {

StringView::StringView(const char *cstr) wontthrow
    : data(cstr), length(cstr != nullptr ? std::strlen(cstr) : 0)
{}

fn StringView::operator==(StringView other) const wontthrow -> bool
{
  return length == other.length &&
         (length == 0 || std::memcmp(data, other.data, length) == 0);
}

fn StringView::find_character(char wanted) const wontthrow -> Maybe<usize>
{
  for (usize i = 0; i < length; i++)
    if (data[i] == wanted) return i;
  return None;
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
  /* An empty prefix matches, and the guard keeps a null data pointer out of
     memcmp, which is undefined even for a zero length. */
  return prefix.length == 0 ||
         std::memcmp(data, prefix.data, prefix.length) == 0;
}

fn hash_bytes(StringView view) wontthrow -> u64
{
  u64 hash = 14695981039346656037ull;
  for (usize i = 0; i < view.length; i++) {
    hash ^= static_cast<unsigned char>(view.data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

} /* namespace shit */
