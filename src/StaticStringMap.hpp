#pragma once

#include "Common.hpp"
#include "Maybe.hpp"
#include "PackedStringKey.hpp"
#include "StringView.hpp"

namespace shit {

template <class Value>
class StaticStringMap
{
public:
  struct entry
  {
    PackedStringKey key;
    Value value;
  };

  const entry *entries;
  usize entry_count;

  /* consteval forces the table to be built at compile time, so a static map is
     a constant with no runtime initialization and a stray runtime construction
     fails to compile rather than packing the keys at run time. */
  consteval StaticStringMap(const entry *table, usize count) wontthrow
      : entries(table),
        entry_count(count)
  {}

  hot mustuse fn find(StringView text) const throws -> Maybe<Value>
  {
    if (text.count() > PackedStringKey::BYTE_CAPACITY) return None;
    let const wanted = PackedStringKey::from_view(text);
    /* A NUL-padded query packs the same as the shorter name it pads, since the
       pad bytes read as the zero fill, so the length is matched to keep a query
       such as a name followed by a NUL from dispatching to that name. */
#pragma clang loop unroll_count(4)
    for (usize i = 0; i < entry_count; i++)
      if (entries[i].key == wanted &&
          text.count() == entries[i].key.packed_length())
        return entries[i].value;
    return None;
  }
};

} // namespace shit
