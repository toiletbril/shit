#pragma once

#include "HashMap.hpp"
#include "Maybe.hpp"
#include "StringView.hpp"

namespace shit {

/* A set of byte-string keys over the HashMap open-addressing table. The value
   is None, so a set stores only keys. It owns a copy of every key it holds, so
   a view passed to add need not outlive it. */
class HashSet
{
public:
  explicit HashSet(Allocator allocator) : m_map(allocator) {}

  void add(StringView key) { m_map.set(key, nothing{}); }

  [[nodiscard]] bool contains(StringView key) const
  {
    return m_map.find(key) != nullptr;
  }

  [[nodiscard]] usize size() const { return m_map.size(); }

  template <class Fn>
  void for_each(Fn callback) const
  {
    m_map.for_each(
        [&callback](StringView key, const nothing &) { callback(key); });
  }

private:
  HashMap<nothing> m_map;
};

} /* namespace shit */
