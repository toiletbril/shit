#pragma once

#include "Common.hpp"
#include "Maybe.hpp"
#include "PackedStringKey.hpp"
#include "StringView.hpp"

namespace shit {

/* A frozen map from a short byte string to a value, stored in static storage as
   a flat array of packed-key entries. A lookup packs the query into two words
   and scans, so a tiny table resolves in a handful of integer compares with no
   hashing and no allocation. */
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

  [[nodiscard]] Maybe<Value> find(StringView text) const
  {
    if (text.size() > 16) return None;
    PackedStringKey wanted = PackedStringKey::from_view(text);
    /* TODO: slow? */
    for (usize i = 0; i < entry_count; i++)
      if (entries[i].key == wanted) return entries[i].value;
    return None;
  }
};

} /* namespace shit */
