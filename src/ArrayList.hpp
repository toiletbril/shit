#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"

#include <initializer_list>

namespace shit {

/* A growable array over an explicit allocator, the std::vector replacement. */
template <class T>
class ArrayList
{
public:
  /* A default list is heap-backed and empty, so it can serve as the value a
     StringMap slot holds before a real list is placed into it. */
  ArrayList() : m_allocator(heap_allocator()) {}
  explicit ArrayList(Allocator allocator) : m_allocator(allocator) {}

  /* A heap-backed list of the braced elements, so a static help table can be
     spelled as a brace list. The initializer_list is header-only and adds no
     standard-library link dependency. */
  ArrayList(std::initializer_list<T> elements) : m_allocator(heap_allocator())
  {
    reserve(elements.size());
    for (const T &element : elements)
      push(element);
  }

  cold ArrayList(const ArrayList &other) : m_allocator(other.m_allocator)
  {
    reserve(other.m_length);
#pragma clang loop unroll_count(4)
    for (usize i = 0; i < other.m_length; i++)
      new (&m_data[i]) T(other.m_data[i]);
    m_length = other.m_length;
  }

  /* An explicit deep copy, so a caller that means to duplicate the list says so
     rather than leaning on an implicit copy. */
  mustuse cold fn clone() const throws -> ArrayList { return ArrayList{*this}; }

  ArrayList(ArrayList &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity)
  {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  fn operator=(ArrayList &&other) wontthrow->ArrayList &
  {
    if (this != &other) {
      destroy_all();
      m_allocator = other.m_allocator;
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;
      other.m_data = nullptr;
      other.m_length = 0;
      other.m_capacity = 0;
    }
    return *this;
  }
  cold fn operator=(const ArrayList &other) throws->ArrayList &
  {
    if (this != &other) {
      ArrayList copy{other};
      *this = steal(copy);
    }
    return *this;
  }

  ~ArrayList() { destroy_all(); }

  hot mustuse pure fn count() const wontthrow -> usize { return m_length; }
  mustuse pure fn is_empty() const wontthrow -> bool { return m_length == 0; }
  hot mustuse pure fn operator[](usize i) wontthrow->T & { return m_data[i]; }
  hot mustuse pure fn operator[](usize i) const wontthrow->const T &
  {
    return m_data[i];
  }
  hot mustuse pure fn begin() wontthrow -> T * { return m_data; }
  hot mustuse pure fn end() wontthrow -> T * { return m_data + m_length; }
  hot mustuse pure fn begin() const wontthrow -> const T * { return m_data; }
  hot mustuse pure fn end() const wontthrow -> const T *
  {
    return m_data + m_length;
  }

  hot fn push(T value) throws -> void
  {
    if (m_length == m_capacity) [[unlikely]]
      reserve(m_length + 1);
    new (&m_data[m_length]) T(steal(value));
    m_length++;
  }

  /* The allocator the list owns, handed to an element so a transient pushed
     onto a scratch-arena list lives on the arena rather than the heap. */
  pure fn allocator() const wontthrow -> Allocator { return m_allocator; }

  /* Build the element with the list's own allocator and push it, so a call site
     never spells out the allocator and a scratch list stops minting heap
     values. The element type is constructed from the allocator and the
     forwarded arguments, the way String takes an allocator and a view. */
  template <typename... Args>
  hot fn push_managed(Args &&...args) throws -> void
  {
    push(T{m_allocator, static_cast<Args &&>(args)...});
  }

  /* Destroy and drop the last element. The caller guarantees the list is not
     empty. */
  fn pop_back() wontthrow -> void
  {
    ASSERT(m_length > 0, "pop_back on an empty list");
    m_length--;
    m_data[m_length].~T();
  }

  /* Remove the element at index, shifting every later element down by one so
     the order is kept. The shift moves rather than copies, so no element is
     duplicated. The caller guarantees index is in range. */
  fn remove(usize index) wontthrow -> void
  {
    ASSERT(index < m_length, "remove past the end of the list");
    for (usize i = index; i + 1 < m_length; i++)
      m_data[i] = steal(m_data[i + 1]);
    m_length--;
    m_data[m_length].~T();
  }

  /* Destroy the elements but keep the storage, so a reused list does not
     reallocate. */
  fn clear() wontthrow -> void
  {
    for (usize i = 0; i < m_length; i++)
      m_data[i].~T();
    m_length = 0;
  }

  hot mustuse pure fn back() wontthrow -> T &
  {
    ASSERT(m_length > 0, "back() on an empty list");
    return m_data[m_length - 1];
  }
  hot mustuse pure fn back() const wontthrow -> const T &
  {
    ASSERT(m_length > 0, "back() on an empty list");
    return m_data[m_length - 1];
  }
  mustuse pure fn front() wontthrow -> T &
  {
    ASSERT(m_length > 0, "front() on an empty list");
    return m_data[0];
  }
  mustuse pure fn front() const wontthrow -> const T &
  {
    ASSERT(m_length > 0, "front() on an empty list");
    return m_data[0];
  }

  cold fn reserve(usize needed) throws -> void
  {
    if (needed <= m_capacity) return;
    /* A small list quadruples so a list grown one push at a time reaches a
       useful size in fewer reallocations, while a large list doubles to keep
       the overshoot bounded. */
    usize new_capacity = m_capacity == 0   ? 16
                         : m_capacity < 64 ? m_capacity * 4
                                           : m_capacity * 2;
    while (new_capacity < needed)
      new_capacity *= 2;
    let const fresh = m_allocator.alloc_array<T>(new_capacity);
    for (usize i = 0; i < m_length; i++) {
      new (&fresh[i]) T(steal(m_data[i]));
      m_data[i].~T();
    }
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = fresh;
    m_capacity = new_capacity;
  }

  /* Release the reserved slots past the current length. A list built once and
     then left alone keeps the growth overshoot for its whole lifetime, which a
     long-lived arena never reclaims, so a one-time builder calls this to hand
     the slack back. */
  cold fn shrink_to_fit() throws -> void
  {
    if (m_length == m_capacity) return;
    if (m_length == 0) {
      if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
      m_data = nullptr;
      m_capacity = 0;
      return;
    }
    let const fresh = m_allocator.alloc_array<T>(m_length);
    for (usize i = 0; i < m_length; i++) {
      new (&fresh[i]) T(steal(m_data[i]));
      m_data[i].~T();
    }
    m_allocator.free_array(m_data, m_capacity);
    m_data = fresh;
    m_capacity = m_length;
  }

private:
  fn destroy_all() wontthrow -> void
  {
    for (usize i = 0; i < m_length; i++)
      m_data[i].~T();
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = nullptr;
    m_length = 0;
    m_capacity = 0;
  }

  Allocator m_allocator;
  T *m_data{nullptr};
  usize m_length{0};
  usize m_capacity{0};
};

} /* namespace shit */
