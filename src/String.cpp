#include "String.hpp"

namespace shit {

String::String(Allocator allocator, StringView initial) throws
    : m_allocator(allocator)
{
  append(initial);
}

String::String(const char *cstr) throws : m_allocator(heap_allocator())
{
  append(StringView{cstr});
}

String::String(StringView initial) throws : m_allocator(heap_allocator())
{
  append(initial);
}

String::String(const String &other) throws : m_allocator(other.m_allocator)
{
  append(other.view());
}

String::String(String &&other) wontthrow : m_allocator(other.m_allocator),
                                           m_data(other.m_data),
                                           m_length(other.m_length),
                                           m_capacity(other.m_capacity)
{
  other.m_data = nullptr;
  other.m_length = 0;
  other.m_capacity = 0;
}

fn String::operator=(const String &other) throws -> String &
{
  if (this != &other) {
    clear();
    append(other.view());
  }
  return *this;
}

fn String::operator=(String &&other) wontthrow -> String &
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

fn String::clear() wontthrow -> void
{
  m_length = 0;
  if (m_data != nullptr) m_data[0] = '\0';
}

fn String::push(char c) throws -> void
{
  reserve(m_length + 1);
  m_data[m_length++] = c;
  m_data[m_length] = '\0';
}

fn String::append(StringView other) throws -> void
{
  if (other.length == 0) return;
  reserve(m_length + other.length);
  std::memcpy(m_data + m_length, other.data, other.length);
  m_length += other.length;
  m_data[m_length] = '\0';
}

fn String::reserve(usize needed) throws -> void
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

fn String::pop_back() wontthrow -> void
{
  ASSERT(m_length > 0, "pop_back on empty string");
  m_length--;
  m_data[m_length] = '\0';
}

fn String::operator+=(StringView other) throws -> String &
{
  append(other);
  return *this;
}

fn String::operator+=(char c) throws -> String &
{
  push(c);
  return *this;
}

fn String::operator<(const String &other) const wontthrow -> bool
{
  usize shared = m_length < other.m_length ? m_length : other.m_length;
  int order = shared == 0 ? 0 : std::memcmp(c_str(), other.c_str(), shared);
  if (order != 0) return order < 0;
  return m_length < other.m_length;
}

fn String::find_substring(StringView needle, usize from) const wontthrow
    -> Maybe<usize>
{
  if (needle.length == 0) return from <= m_length ? Maybe<usize>{from} : None;
  if (needle.length > m_length) return None;
  for (usize i = from; i + needle.length <= m_length; i++)
    if (std::memcmp(m_data + i, needle.data, needle.length) == 0) return i;
  return None;
}

fn String::find_last_character(char wanted) const wontthrow -> Maybe<usize>
{
  for (usize i = m_length; i > 0; i--)
    if (m_data[i - 1] == wanted) return i - 1;
  return None;
}

fn String::free_storage() wontthrow -> void
{
  if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
  m_data = nullptr;
  m_capacity = 0;
}

fn operator+(StringView left, StringView right) throws->String
{
  String result{heap_allocator()};
  result.reserve(left.length + right.length);
  result.append(left);
  result.append(right);
  return result;
}

} /* namespace shit */
