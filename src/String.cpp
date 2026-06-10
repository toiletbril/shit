#include "String.hpp"

namespace shit {

String::String(Allocator allocator, StringView initial) throws
    : m_allocator(allocator)
{
  reset_to_inline();
  append(initial);
}

String::String(const char *cstr) throws : m_allocator(heap_allocator())
{
  reset_to_inline();
  append(StringView{cstr});
}

String::String(StringView initial) throws : m_allocator(heap_allocator())
{
  reset_to_inline();
  append(initial);
}

cold String::String(const String &other) throws : m_allocator(other.m_allocator)
{
  reset_to_inline();
  append(other.view());
}

/* An inline source cannot have its pointer stolen, since the bytes live in its
   own object, so they are copied into this inline buffer. A heap source hands
   over its allocation and is left valid and empty. */
String::String(String &&other) wontthrow : m_allocator(other.m_allocator)
{
  if (other.is_inline()) {
    std::memcpy(m_inline, other.m_inline, other.m_length + 1);
    m_data = m_inline;
    m_length = other.m_length;
    m_capacity = INLINE_CAPACITY;
  } else {
    m_data = other.m_data;
    m_length = other.m_length;
    m_capacity = other.m_capacity;
    other.reset_to_inline();
  }
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
    if (other.is_inline()) {
      std::memcpy(m_inline, other.m_inline, other.m_length + 1);
      m_data = m_inline;
      m_length = other.m_length;
      m_capacity = INLINE_CAPACITY;
    } else {
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;
      other.reset_to_inline();
    }
  }
  return *this;
}

fn String::clear() wontthrow -> void
{
  m_length = 0;
  if (m_data != nullptr) m_data[0] = '\0';
}

hot fn String::push(char c) throws -> void
{
  reserve(m_length + 1);
  m_data[m_length++] = c;
  m_data[m_length] = '\0';
}

hot fn String::append(StringView other) throws -> void
{
  if (other.length == 0) return;
  reserve(m_length + other.length);
  std::memcpy(m_data + m_length, other.data, other.length);
  m_length += other.length;
  m_data[m_length] = '\0';
}

cold fn String::reserve(usize needed) throws -> void
{
  if (needed + 1 <= m_capacity) [[likely]]
    return;
  /* A small buffer quadruples so a string built one append at a time leaves the
     inline size in one realloc rather than several, while a large buffer
     doubles to keep the overshoot bounded. */
  let new_capacity = m_capacity < 64 ? m_capacity * 4 : m_capacity * 2;
  while (new_capacity < needed + 1)
    new_capacity *= 2;
  let fresh = m_allocator.alloc_array<char>(new_capacity);
  let const preserved_length = m_length;
  if (preserved_length > 0) std::memcpy(fresh, m_data, preserved_length);
  fresh[preserved_length] = '\0';
  /* Release the old allocation before adopting the fresh one. An inline buffer
     owns no allocation, so free_storage leaves it alone. The reset clears
     m_length, so it is restored from the preserved value afterwards. */
  free_storage();
  m_data = fresh;
  m_length = preserved_length;
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

hot fn String::operator<(const String &other) const wontthrow -> bool
{
  let const shared = m_length < other.m_length ? m_length : other.m_length;
  let const order =
      shared == 0 ? 0 : std::memcmp(c_str(), other.c_str(), shared);
  if (order != 0) return order < 0;
  return m_length < other.m_length;
}

fn String::find_substring(StringView needle, usize from) const wontthrow
    -> Maybe<usize>
{
  if (needle.length == 0) return from <= m_length ? Maybe<usize>{from} : None;
  if (needle.length > m_length) return None;
  /* memchr finds each candidate first byte with a vectorized scan and memcmp
     confirms the rest, which skips the bulk of the per-position compares. The
     scan is bounded so a first byte never lands where the needle would overrun
     the end. */
  let i = from;
  while (i + needle.length <= m_length) {
    let const scan_length = m_length - needle.length - i + 1;
    let const found = std::memchr(
        m_data + i, static_cast<unsigned char>(needle.data[0]), scan_length);
    if (found == nullptr) return None;
    let const candidate =
        static_cast<usize>(static_cast<const char *>(found) - m_data);
    if (std::memcmp(m_data + candidate, needle.data, needle.length) == 0)
      return candidate;
    i = candidate + 1;
  }
  return None;
}

fn String::find_last_character(char wanted) const wontthrow -> Maybe<usize>
{
  for (usize i = m_length; i > 0; i--)
    if (m_data[i - 1] == wanted) return i - 1;
  return None;
}

cold fn String::free_storage() wontthrow -> void
{
  /* Only a heap or arena buffer is freed. The inline buffer is part of the
     object. The reset leaves the object a valid empty inline string, which is
     moot in the destructor and required in operator= where the object lives on.
   */
  if (m_data != nullptr && m_data != m_inline)
    m_allocator.free_array(m_data, m_capacity);
  reset_to_inline();
}

fn operator+(StringView left, StringView right) throws->String
{
  let result = String{heap_allocator()};
  result.reserve(left.length + right.length);
  result.append(left);
  result.append(right);
  return result;
}

} /* namespace shit */
