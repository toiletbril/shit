#include "StringView.hpp"

namespace shit {

StringView::StringView(const char *cstr) wontthrow
    : data(cstr),
      length(cstr != nullptr ? std::strlen(cstr) : 0)
{}

fn StringView::find_character(char wanted) const wontthrow -> Maybe<usize>
{
  if (length == 0) return None;
  /* memchr is vectorized in libc, so a single-byte scan beats a per-byte loop
     on a long line, and the guard keeps a null data pointer out of it. */
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
  /* An empty prefix matches, and the guard keeps a null data pointer out of
     memcmp, which is undefined even for a zero length. */
  return prefix.length == 0 ||
         std::memcmp(data, prefix.data, prefix.length) == 0;
}

} // namespace shit
