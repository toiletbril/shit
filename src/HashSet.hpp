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

  hot fn add(StringView key) throws -> void { m_map.set(key, Nothing{}); }

  cold fn remove(StringView key) throws -> void { m_map.erase(key); }

  hot mustuse pure fn contains(StringView key) const wontthrow -> bool
  {
    return m_map.find(key) != nullptr;
  }

  mustuse pure fn count() const wontthrow -> usize { return m_map.count(); }

  /* An explicit deep copy, so a caller that means to duplicate the set says so
     rather than leaning on an implicit copy. */
  mustuse cold fn clone() const throws -> HashSet { return HashSet{*this}; }

  template <class Fn>
  fn for_each(Fn callback) const throws -> void
  {
    m_map.for_each(
        [&callback](StringView key, const Nothing &) { callback(key); });
  }

private:
  HashMap<Nothing> m_map;
};

} /* namespace shit */
