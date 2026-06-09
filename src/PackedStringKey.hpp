#pragma once

#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

#include <cstring>

namespace shit {

/* A short byte string packed into a fixed sixteen-byte block held in two
   machine words. A lookup compares two integers rather than walking bytes,
   which the compiler lowers to a couple of wide register compares, and the
   layout is contiguous so a later pass can scan many keys with one SIMD
   instruction. A key longer than sixteen bytes keeps only its first sixteen
   bytes here and relies on a full compare to disambiguate. */
class PackedStringKey
{
public:
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
    usize count = text.count() < 16 ? text.count() : 16;
    for (usize i = 0; i < count && i < 8; i++)
      key.low_word |= static_cast<u64>(static_cast<u8>(text[i])) << (8 * i);
    for (usize i = 8; i < count; i++)
      key.high_word |= static_cast<u64>(static_cast<u8>(text[i]))
                       << (8 * (i - 8));
    return key;
  }

  mustuse bool operator==(const PackedStringKey &other) const
  {
    return low_word == other.low_word && high_word == other.high_word;
  }

  /* Unpack the bytes back into a String, stopping at the first NUL or the
     sixteen-byte limit. A key built from a name with no embedded NUL and no
     more than sixteen bytes round-trips exactly, which holds for every builtin
     name. */
  mustuse String to_string() const
  {
    char buffer[16];
    usize length = 0;
    for (; length < 8; length++) {
      const char byte = static_cast<char>((low_word >> (8 * length)) & 0xFF);
      if (byte == '\0') return String{StringView{buffer, length}};
      buffer[length] = byte;
    }
    for (; length < 16; length++) {
      const char byte =
          static_cast<char>((high_word >> (8 * (length - 8))) & 0xFF);
      if (byte == '\0') return String{StringView{buffer, length}};
      buffer[length] = byte;
    }
    return String{StringView{buffer, length}};
  }
};

} /* namespace shit */
