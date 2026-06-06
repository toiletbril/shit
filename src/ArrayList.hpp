#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"

#include <new>
#include <utility>

namespace shit {

/* A growable array over an explicit allocator, the std::vector replacement. */
template <class T>
class ArrayList
{
public:
  /* A default list is heap-backed and empty, so it can serve as the value a
     HashMap slot holds before a real list is placed into it. */
  ArrayList() : m_allocator(heap_allocator()) {}
  explicit ArrayList(Allocator allocator) : m_allocator(allocator) {}

  ArrayList(const ArrayList &other) : m_allocator(other.m_allocator)
  {
    reserve(other.m_length);
    for (usize i = 0; i < other.m_length; i++)
      new (&m_data[i]) T(other.m_data[i]);
    m_length = other.m_length;
  }
  ArrayList(ArrayList &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity)
  {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  ArrayList &operator=(ArrayList &&other) noexcept
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
  ArrayList &operator=(const ArrayList &other)
  {
    if (this != &other) {
      ArrayList copy{other};
      *this = steal(copy);
    }
    return *this;
  }

  ~ArrayList() { destroy_all(); }

  [[nodiscard]] usize size() const { return m_length; }
  [[nodiscard]] bool empty() const { return m_length == 0; }
  [[nodiscard]] T &operator[](usize i) { return m_data[i]; }
  [[nodiscard]] const T &operator[](usize i) const { return m_data[i]; }
  [[nodiscard]] T *begin() { return m_data; }
  [[nodiscard]] T *end() { return m_data + m_length; }
  [[nodiscard]] const T *begin() const { return m_data; }
  [[nodiscard]] const T *end() const { return m_data + m_length; }

  void push(T value)
  {
    reserve(m_length + 1);
    new (&m_data[m_length]) T(steal(value));
    m_length++;
  }

  /* Destroy and drop the last element. The caller guarantees the list is not
     empty. */
  void pop_back()
  {
    ASSERT(m_length > 0, "pop_back on an empty list");
    m_length--;
    m_data[m_length].~T();
  }

  /* Destroy the elements but keep the storage, so a reused list does not
     reallocate. */
  void clear()
  {
    for (usize i = 0; i < m_length; i++)
      m_data[i].~T();
    m_length = 0;
  }

  [[nodiscard]] T &back() { return m_data[m_length - 1]; }
  [[nodiscard]] const T &back() const { return m_data[m_length - 1]; }
  [[nodiscard]] T &front() { return m_data[0]; }
  [[nodiscard]] const T &front() const { return m_data[0]; }

  void reserve(usize needed)
  {
    if (needed <= m_capacity) return;
    usize new_capacity = m_capacity == 0 ? 8 : m_capacity * 2;
    while (new_capacity < needed)
      new_capacity *= 2;
    T *fresh = m_allocator.alloc_array<T>(new_capacity);
    for (usize i = 0; i < m_length; i++) {
      new (&fresh[i]) T(steal(m_data[i]));
      m_data[i].~T();
    }
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = fresh;
    m_capacity = new_capacity;
  }

private:
  void destroy_all()
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
