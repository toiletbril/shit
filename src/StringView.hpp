#pragma once

#include "Common.hpp"
#include "Maybe.hpp"

#include <cstring>

namespace shit {

/* A non-owning view of bytes, the form a function takes when it does not own
   the characters. It points into a String, a literal, or a slice. */
class StringView
{
public:
  const char *data{nullptr};
  usize length{0};

  StringView() = default;
  StringView(const char *bytes, usize count) : data(bytes), length(count) {}
  StringView(const char *cstr) wontthrow;

  hot mustuse pure fn count() const wontthrow -> usize { return length; }
  hot mustuse pure fn is_empty() const wontthrow -> bool
  {
    return length == 0;
  }
  hot mustuse pure fn operator[](usize i) const wontthrow->char
  {
    return data[i];
  }

  hot mustuse pure fn operator==(StringView other) const wontthrow->bool;
  hot flatten mustuse pure fn operator!=(StringView other) const wontthrow->bool
  {
    return !(*this == other);
  }

  /* The index of the first occurrence of a byte, or None when it is absent.
     A Maybe keeps the absent case out of band rather than using a sentinel
     index. */
  hot mustuse pure fn find_character(char wanted) const wontthrow
      -> Maybe<usize>;

  /* The view from start to the end. A start past the end yields an empty view.
   */
  mustuse pure fn substring(usize start) const wontthrow -> StringView;

  /* The view of count bytes from start, clamped to what remains. */
  mustuse pure fn substring_of_length(usize start, usize count) const wontthrow
      -> StringView;

  mustuse pure fn starts_with(StringView prefix) const wontthrow -> bool;
};

/* A growable hash of a byte range, FNV-1a over the short keys a shell uses. */
pure fn hash_bytes(StringView view) wontthrow -> u64;

} /* namespace shit */
