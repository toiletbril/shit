#pragma once

#include "Common.hpp"
#include "StringView.hpp"

#include <cstring>

namespace shit {

/* A short byte string packed into a fixed sixteen-byte block held in two
   machine words. A lookup compares two integers rather than walking bytes,
   which the compiler lowers to a couple of wide register compares, and the
   layout is contiguous so a later pass can scan many keys with one SIMD
   instruction. A key longer than sixteen bytes keeps only its first sixteen
   bytes here and relies on a full compare to disambiguate. */
struct PackedStringKey
{
  u64 low_word{0};
  u64 high_word{0};

  /* Pack a NUL-terminated literal at compile time, so a static table entry
     becomes a constant with no runtime initialization. */
  static constexpr PackedStringKey from_literal(const char *text)
  {
    PackedStringKey key{};
    usize i = 0;
    for (; text[i] != '\0' && i < 8; i++)
      key.low_word |= static_cast<u64>(static_cast<u8>(text[i])) << (8 * i);
    for (; text[i] != '\0' && i < 16; i++)
      key.high_word |= static_cast<u64>(static_cast<u8>(text[i]))
                       << (8 * (i - 8));
    return key;
  }

  /* Pack the first sixteen bytes of a view at lookup time. */
  static PackedStringKey from_view(StringView text)
  {
    PackedStringKey key{};
    usize count = text.size() < 16 ? text.size() : 16;
    for (usize i = 0; i < count && i < 8; i++)
      key.low_word |= static_cast<u64>(static_cast<u8>(text[i])) << (8 * i);
    for (usize i = 8; i < count; i++)
      key.high_word |= static_cast<u64>(static_cast<u8>(text[i]))
                       << (8 * (i - 8));
    return key;
  }

  [[nodiscard]] bool operator==(const PackedStringKey &other) const
  {
    return low_word == other.low_word && high_word == other.high_word;
  }
};

} /* namespace shit */
