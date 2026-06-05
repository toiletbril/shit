#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Maybe.hpp"
#include "StringView.hpp"

#include <cstring>
#include <new>

namespace shit {

/* An owned, growable byte string over an explicit allocator. There is no
   small-string buffer yet, so every non-empty string is one allocation, which a
   bump arena makes cheap. The buffer keeps a trailing null so c_str is free. */
struct String
{
  /* A default String is heap-backed and empty, so it can serve as a container
     slot before its real allocator and value are assigned. */
  String() : m_allocator(heap_allocator()) {}
  explicit String(Allocator allocator) : m_allocator(allocator) {}
  String(Allocator allocator, StringView initial) : m_allocator(allocator)
  {
    append(initial);
  }
  /* Heap-backed conversions from a literal or a view, so a String stands in for
     the std::string a conversion replaces. The literal form wins over the view
     form for a const char*, since it is a single conversion. */
  String(const char *cstr) : m_allocator(heap_allocator())
  {
    append(StringView{cstr});
  }
  String(StringView initial) : m_allocator(heap_allocator())
  {
    append(initial);
  }

  String(const String &other) : m_allocator(other.m_allocator)
  {
    append(other.view());
  }
  String(String &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity)
  {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  String &operator=(const String &other)
  {
    if (this != &other) {
      clear();
      append(other.view());
    }
    return *this;
  }
  String &operator=(String &&other) noexcept
  {
    if (this != &other) {
      free_storage();
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

  ~String() { free_storage(); }

  [[nodiscard]] usize size() const { return m_length; }
  [[nodiscard]] bool empty() const { return m_length == 0; }
  [[nodiscard]] char operator[](usize i) const { return m_data[i]; }
  [[nodiscard]] StringView view() const { return StringView{m_data, m_length}; }
  /* A String reads as a view wherever one is expected, so an owned string
     passes to a comparison or a function taking a view without spelling out
     view(). */
  operator StringView() const { return StringView{m_data, m_length}; }
  [[nodiscard]] const char *c_str() const
  {
    return m_data != nullptr ? m_data : "";
  }

  void clear()
  {
    m_length = 0;
    if (m_data != nullptr) m_data[0] = '\0';
  }

  void push(char c)
  {
    reserve(m_length + 1);
    m_data[m_length++] = c;
    m_data[m_length] = '\0';
  }
  void append(StringView other)
  {
    if (other.length == 0) return;
    reserve(m_length + other.length);
    std::memcpy(m_data + m_length, other.data, other.length);
    m_length += other.length;
    m_data[m_length] = '\0';
  }

  void reserve(usize needed)
  {
    if (needed + 1 <= m_capacity) return;
    usize new_capacity = m_capacity == 0 ? 16 : m_capacity * 2;
    while (new_capacity < needed + 1)
      new_capacity *= 2;
    char *fresh = m_allocator.alloc_array<char>(new_capacity);
    if (m_length > 0) std::memcpy(fresh, m_data, m_length);
    fresh[m_length] = '\0';
    free_storage();
    m_data = fresh;
    m_capacity = new_capacity;
  }

  /* The byte buffer, always null terminated. */
  [[nodiscard]] const char *data() const { return c_str(); }
  [[nodiscard]] usize length() const { return m_length; }
  [[nodiscard]] char back() const
  {
    SHIT_ASSERT(m_length > 0, "back() on an empty string");
    return m_data[m_length - 1];
  }

  void pop_back()
  {
    SHIT_ASSERT(m_length > 0, "pop_back on empty string");
    m_length--;
    m_data[m_length] = '\0';
  }

  void append(char c) { push(c); }
  String &operator+=(StringView other)
  {
    append(other);
    return *this;
  }
  String &operator+=(char c)
  {
    push(c);
    return *this;
  }

  /* Search and slice forward to the view, so the owned string answers the same
     questions a std::string does without exposing its buffer. */
  [[nodiscard]] Maybe<usize> find_character(char wanted) const
  {
    return view().find_character(wanted);
  }
  [[nodiscard]] StringView substring(usize start) const
  {
    return view().substring(start);
  }
  [[nodiscard]] StringView substring_of_length(usize start, usize count) const
  {
    return view().substring_of_length(start, count);
  }
  [[nodiscard]] bool starts_with(StringView prefix) const
  {
    return view().starts_with(prefix);
  }

  [[nodiscard]] bool operator==(StringView other) const
  {
    return view() == other;
  }
  [[nodiscard]] bool operator!=(StringView other) const
  {
    return !(view() == other);
  }

  /* Byte order, so a sort matches the C locale collating order. */
  [[nodiscard]] bool operator<(const String &other) const
  {
    usize shared = m_length < other.m_length ? m_length : other.m_length;
    int order = shared == 0 ? 0 : std::memcmp(c_str(), other.c_str(), shared);
    if (order != 0) return order < 0;
    return m_length < other.m_length;
  }

  /* The first byte. The caller guarantees the string is not empty. */
  [[nodiscard]] char first_character() const
  {
    SHIT_ASSERT(m_length > 0, "first_character() on an empty string");
    return m_data[0];
  }

  /* The index of the first occurrence of a substring at or after a start, or
     None when it is absent. */
  [[nodiscard]] Maybe<usize> find_substring(StringView needle,
                                            usize from = 0) const
  {
    if (needle.length == 0) return from <= m_length ? Maybe<usize>{from} : None;
    if (needle.length > m_length) return None;
    for (usize i = from; i + needle.length <= m_length; i++)
      if (std::memcmp(m_data + i, needle.data, needle.length) == 0) return i;
    return None;
  }

  /* The index of the last occurrence of a byte, or None when it is absent. */
  [[nodiscard]] Maybe<usize> find_last_character(char wanted) const
  {
    for (usize i = m_length; i > 0; i--)
      if (m_data[i - 1] == wanted) return i - 1;
    return None;
  }

private:
  void free_storage()
  {
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = nullptr;
    m_capacity = 0;
  }

  Allocator m_allocator;
  char *m_data{nullptr};
  usize m_length{0};
  usize m_capacity{0};
};

/* Concatenate two byte ranges into a fresh heap String. A String and a literal
   both read as a view, so str + "x", "x" + str, and str + str all resolve here.
   The result is heap-backed, the default for an expression temporary. */
inline String operator+(StringView left, StringView right)
{
  String result{heap_allocator()};
  result.reserve(left.length + right.length);
  result.append(left);
  result.append(right);
  return result;
}

} /* namespace shit */
