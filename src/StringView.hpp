#pragma once

#include "Common.hpp"
#include "Maybe.hpp"

#include <cstring>
#include <string>

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
  StringView(const char *cstr)
      : data(cstr), length(cstr != nullptr ? std::strlen(cstr) : 0)
  {}
  /* A view of a std::string, so a boundary that still holds one passes it to a
     function taking a view. This is transitional, removed once std::string is
     gone from the tree. TODO: drop with the last std::string. */
  StringView(const std::string &s) : data(s.data()), length(s.size()) {}

  [[nodiscard]] usize count() const { return length; }
  [[nodiscard]] bool is_empty() const { return length == 0; }
  [[nodiscard]] char operator[](usize i) const { return data[i]; }

  [[nodiscard]] bool operator==(StringView other) const
  {
    return length == other.length &&
           (length == 0 || std::memcmp(data, other.data, length) == 0);
  }
  [[nodiscard]] bool operator!=(StringView other) const
  {
    return !(*this == other);
  }

  /* The index of the first occurrence of a byte, or None when it is absent.
     A Maybe keeps the absent case out of band rather than using a sentinel
     index. */
  [[nodiscard]] Maybe<usize> find_character(char wanted) const
  {
    for (usize i = 0; i < length; i++)
      if (data[i] == wanted) return i;
    return None;
  }

  /* The view from start to the end. A start past the end yields an empty view.
   */
  [[nodiscard]] StringView substring(usize start) const
  {
    if (start >= length) return StringView{data + length, 0};
    return StringView{data + start, length - start};
  }

  /* The view of count bytes from start, clamped to what remains. */
  [[nodiscard]] StringView substring_of_length(usize start, usize count) const
  {
    if (start >= length) return StringView{data + length, 0};
    usize remaining = length - start;
    return StringView{data + start, count < remaining ? count : remaining};
  }

  [[nodiscard]] bool starts_with(StringView prefix) const
  {
    if (prefix.length > length) return false;
    /* An empty prefix matches, and the guard keeps a null data pointer out of
       memcmp, which is undefined even for a zero length. */
    return prefix.length == 0 ||
           std::memcmp(data, prefix.data, prefix.length) == 0;
  }
};

/* A growable hash of a byte range, FNV-1a over the short keys a shell uses. */
inline u64 hash_bytes(StringView view)
{
  u64 hash = 14695981039346656037ull;
  for (usize i = 0; i < view.length; i++) {
    hash ^= static_cast<unsigned char>(view.data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

} /* namespace shit */
