#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"

namespace shit {

template <class T>
class ArrayList
{
public:
  explicit ArrayList(Allocator allocator) : m_allocator(allocator) {}

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

  hot flatten mustuse pure fn
  operator==(const ArrayList &other) const wontthrow->bool
  {
    if (m_length != other.m_length) return false;
    if constexpr (std::is_integral_v<T> || std::is_enum_v<T> ||
                  std::is_pointer_v<T>)
    {
      return m_length == 0 ||
             __builtin_memcmp(m_data, other.m_data, m_length * sizeof(T)) == 0;
    } else {
      for (usize i = 0; i < m_length; i++)
        if (!(m_data[i] == other.m_data[i])) return false;
      return true;
    }
  }
  hot flatten mustuse pure fn
  operator!=(const ArrayList &other) const wontthrow->bool
  {
    return !(*this == other);
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

  pure fn allocator() const wontthrow -> Allocator { return m_allocator; }

  template <typename... Args>
  hot fn push_managed(Args &&...args) throws -> void
  {
    push(T{m_allocator, static_cast<Args &&>(args)...});
  }

  /* The caller guarantees the list is not empty. */
  fn pop_back() wontthrow -> void
  {
    ASSERT(m_length > 0, "pop_back on an empty list");
    m_length--;
    m_data[m_length].~T();
  }

  /* The caller guarantees index is in range. */
  fn remove(usize index) wontthrow -> void
  {
    ASSERT(index < m_length, "remove past the end of the list");
    for (usize i = index; i + 1 < m_length; i++)
      m_data[i] = steal(m_data[i + 1]);
    m_length--;
    m_data[m_length].~T();
  }

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

  template <typename Compare>
  fn sort(Compare is_less) throws -> void
  {
    if (m_length < 2) return;

    if (m_length <= INSERTION_SORT_THRESHOLD)
      insertion_sort(is_less);
    else
      heap_sort(is_less);
  }

  fn sort() throws -> void
  {
    sort([](const T &a, const T &b) { return a < b; });
  }

private:
  /* A default list is heap-backed and empty, so it can serve as the value a
     StringMap slot holds before a real list is placed into it. The friend keeps
     it reachable to the table while every call site must name its lifetime. */
  template <class Value>
  friend class StringMap;
  ArrayList() : m_allocator(heap_allocator()) {}

  static constexpr usize INSERTION_SORT_THRESHOLD = 16;

  fn swap_elements(usize a, usize b) wontthrow -> void
  {
    T temporary = steal(m_data[a]);
    m_data[a] = steal(m_data[b]);
    m_data[b] = steal(temporary);
  }

  template <typename Compare>
  fn insertion_sort(Compare is_less) throws -> void
  {
    for (usize i = 1; i < m_length; i++) {
      T key = steal(m_data[i]);
      usize j = i;
      while (j > 0 && is_less(key, m_data[j - 1])) {
        m_data[j] = steal(m_data[j - 1]);
        j--;
      }
      m_data[j] = steal(key);
    }
  }

  /* Restore the max-heap property at root within the first heap_length
     elements, sinking the root past any larger child. */
  template <typename Compare>
  fn sift_down(usize root, usize heap_length, Compare is_less) throws -> void
  {
    loop
    {
      usize largest = root;
      let const left = 2 * root + 1;
      let const right = 2 * root + 2;

      if (left < heap_length && is_less(m_data[largest], m_data[left])) {
        largest = left;
      }
      if (right < heap_length && is_less(m_data[largest], m_data[right])) {
        largest = right;
      }

      if (largest == root) break;

      swap_elements(root, largest);
      root = largest;
    }
  }

  template <typename Compare>
  fn heap_sort(Compare is_less) throws -> void
  {
    for (usize parent = m_length / 2; parent > 0; parent--)
      sift_down(parent - 1, m_length, is_less);

    for (usize end = m_length; end > 1; end--) {
      swap_elements(0, end - 1);
      sift_down(0, end - 1, is_less);
    }
  }

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

} // namespace shit
