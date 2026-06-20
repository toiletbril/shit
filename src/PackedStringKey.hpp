#pragma once

#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

/* A short byte string packed into a fixed sixty-four-byte block held in eight
   machine words, one cache line that the compiler compares with a couple of
   vector instructions rather than walking bytes. A key longer than sixty-four
   bytes keeps only its first sixty-four bytes here and relies on a full compare
   to disambiguate. */
class PackedStringKey
{
public:
  static constexpr usize WORD_COUNT = 8;
  static constexpr usize BYTE_CAPACITY = WORD_COUNT * 8;

  u64 words[WORD_COUNT]{};

  /* Pack a NUL-terminated literal at compile time, so a static table entry
     becomes a constant with no runtime initialization. consteval forbids the
     runtime path, so a caller that passed a non-literal would fail to compile
     rather than silently pack the key at run time. */
  static consteval fn from_literal(const char *text) wontthrow
      -> PackedStringKey
  {
    PackedStringKey key{};
    for (usize i = 0; text[i] != '\0' && i < BYTE_CAPACITY; i++)
      key.words[i / 8] |= static_cast<u64>(static_cast<u8>(text[i]))
                          << (8 * (i % 8));
    return key;
  }

  /* Pack the first thirty-two bytes of a view at lookup time. */
  hot static fn from_view(StringView text) wontthrow -> PackedStringKey
  {
    PackedStringKey key{};
    let const count =
        text.count() < BYTE_CAPACITY ? text.count() : BYTE_CAPACITY;
    for (usize i = 0; i < count; i++)
      key.words[i / 8] |= static_cast<u64>(static_cast<u8>(text[i]))
                          << (8 * (i % 8));
    return key;
  }

  hot mustuse pure fn
  operator==(const PackedStringKey &other) const wontthrow->bool
  {
    /* The bytewise difference is reduced with no branch so the loop lowers to a
       vector xor and an or reduction over the whole cache line rather than a
       chain of word compares. */
    u64 difference = 0;
    for (usize i = 0; i < WORD_COUNT; i++)
      difference |= words[i] ^ other.words[i];
    return difference == 0;
  }

  /* The byte length of a key with no embedded NUL, read as the position of the
     first zero byte and capped at the byte capacity. A key built from a name
     round-trips its length here, so a packed match alone does not let a
     NUL-padded query stand in for a shorter name. */
  hot mustuse pure fn packed_length() const wontthrow -> usize
  {
    for (usize i = 0; i < BYTE_CAPACITY; i++)
      if (((words[i / 8] >> (8 * (i % 8))) & 0xFF) == 0) return i;
    return BYTE_CAPACITY;
  }

  /* Unpack the bytes back into a String, stopping at the first NUL or the byte
     capacity. A key with no embedded NUL and no more than that many bytes
     round-trips exactly, which holds for every builtin name. */
  cold mustuse fn to_string() const throws -> String
  {
    char buffer[BYTE_CAPACITY];
    usize length = 0;
    for (; length < BYTE_CAPACITY; length++) {
      let const byte =
          static_cast<char>((words[length / 8] >> (8 * (length % 8))) & 0xFF);
      if (byte == '\0') break;
      buffer[length] = byte;
    }
    return String{
        StringView{buffer, length}
    };
  }
};

} // namespace shit

/* Build a PackedStringKey from a string literal at compile time, so a static
   StaticStringMap entry reads as SSK("name") rather than the full call. */
#define SSK(literal) shit::PackedStringKey::from_literal(literal)
