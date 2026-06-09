#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Maybe.hpp"
#include "StringView.hpp"

#include <cstring>

namespace shit {

/* An owned, growable byte string over an explicit allocator. A short string
   lives in the inline buffer with no allocation, since the arithmetic loop
   churns tiny strings through malloc and free. A longer string lives in a
   heap or arena buffer. Either way m_data is the access pointer and the buffer
   keeps a trailing null so c_str is free. */
class String
{
public:
  /* The inline buffer length. A string shorter than this, counting the trailing
     null, lives inline. The value keeps sizeof(String) at forty-eight bytes
     next to the sixteen-byte allocator and the three size words. */
  static constexpr usize INLINE_CAPACITY = 24;

  /* A default String is inline and empty, so it can serve as a container slot
     before its real allocator and value are assigned. The default allocator is
     heap, which only matters once the string grows past the inline buffer. */
  String() : m_allocator(heap_allocator()) { reset_to_inline(); }
  explicit String(Allocator allocator) : m_allocator(allocator)
  {
    reset_to_inline();
  }
  String(Allocator allocator, StringView initial) throws;
  /* Heap-backed conversions from a literal or a view. The literal form wins
     over the view form for a const char*, since it is a single conversion. */
  String(const char *cstr) throws;
  String(StringView initial) throws;

  String(const String &other) throws;
  String(String &&other) wontthrow;

  fn operator=(const String &other) throws->String &;
  fn operator=(String &&other) wontthrow->String &;

  /* An explicit deep copy, so a caller that means to duplicate the string says
     so rather than leaning on an implicit copy. */
  mustuse fn clone() const throws -> String { return String{*this}; }

  ~String() { free_storage(); }

  /* The allocator this string was built with, so a container can hand its own
     allocator down to a string it stores. */
  mustuse pure fn allocator() const wontthrow -> Allocator { return m_allocator; }

  hot mustuse pure fn count() const wontthrow -> usize { return m_length; }
  hot mustuse pure fn is_empty() const wontthrow -> bool
  {
    return m_length == 0;
  }
  hot mustuse pure fn operator[](usize i) const wontthrow->char
  {
    return m_data[i];
  }
  hot mustuse pure fn view() const wontthrow -> StringView
  {
    return StringView{m_data, m_length};
  }
  /* A String reads as a view wherever one is expected, so an owned string
     passes to a comparison or a function taking a view without spelling out
     view(). */
  operator StringView() const wontthrow { return StringView{m_data, m_length}; }
  hot mustuse pure fn c_str() const wontthrow -> const char *
  {
    return m_data != nullptr ? m_data : "";
  }

  fn clear() wontthrow -> void;

  hot fn push(char c) throws -> void;
  hot fn append(StringView other) throws -> void;

  cold fn reserve(usize needed) throws -> void;

  /* The byte buffer, always null terminated. */
  mustuse pure fn data() const wontthrow -> const char * { return c_str(); }
  mustuse pure fn length() const wontthrow -> usize { return m_length; }
  hot mustuse pure fn back() const wontthrow -> char
  {
    ASSERT(m_length > 0, "back() on an empty string");
    return m_data[m_length - 1];
  }

  fn pop_back() wontthrow -> void;

  hot flatten fn append(char c) throws -> void { push(c); }
  fn operator+=(StringView other) throws->String &;
  fn operator+=(char c) throws->String &;

  /* Search and slice forward to the view, so the owned string answers the same
     questions through the view without exposing its buffer. */
  hot flatten mustuse pure fn find_character(char wanted) const wontthrow
      -> Maybe<usize>
  {
    return view().find_character(wanted);
  }
  flatten mustuse pure fn substring(usize start) const wontthrow -> StringView
  {
    return view().substring(start);
  }
  flatten mustuse pure fn substring_of_length(usize start,
                                              usize count) const wontthrow
      -> StringView
  {
    return view().substring_of_length(start, count);
  }
  flatten mustuse pure fn starts_with(StringView prefix) const wontthrow -> bool
  {
    return view().starts_with(prefix);
  }

  hot flatten mustuse pure fn operator==(StringView other) const wontthrow->bool
  {
    return view() == other;
  }
  hot flatten mustuse pure fn operator!=(StringView other) const wontthrow->bool
  {
    return !(view() == other);
  }

  /* Byte order, so a sort matches the C locale collating order. */
  hot mustuse pure fn operator<(const String &other) const wontthrow->bool;

  /* The first byte. The caller guarantees the string is not empty. */
  mustuse pure fn first_character() const wontthrow -> char
  {
    ASSERT(m_length > 0, "first_character() on an empty string");
    return m_data[0];
  }

  /* The index of the first occurrence of a substring at or after a start, or
     None when it is absent. */
  mustuse pure fn find_substring(StringView needle,
                                 usize from = 0) const wontthrow
      -> Maybe<usize>;

  /* The index of the last occurrence of a byte, or None when it is absent. */
  mustuse pure fn find_last_character(char wanted) const wontthrow
      -> Maybe<usize>;

private:
  cold fn free_storage() wontthrow -> void;

  /* True when the string lives in the inline buffer rather than an allocation.
   */
  mustuse pure fn is_inline() const wontthrow -> bool
  {
    return m_data == m_inline;
  }

  /* Point the string at its empty inline buffer. The caller must have already
     released any heap storage, since this overwrites the data pointer. */
  fn reset_to_inline() wontthrow -> void
  {
    m_data = m_inline;
    m_inline[0] = '\0';
    m_length = 0;
    m_capacity = INLINE_CAPACITY;
  }

  Allocator m_allocator;
  char *m_data{nullptr};
  usize m_length{0};
  usize m_capacity{0};
  char m_inline[INLINE_CAPACITY];
};

/* Concatenate two byte ranges into a fresh heap String. A String and a literal
   both read as a view, so str + "x", "x" + str, and str + str all resolve here.
   The result is heap-backed, the default for an expression temporary. */
fn operator+(StringView left, StringView right) throws->String;

} /* namespace shit */
