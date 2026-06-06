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
class String
{
public:
  /* A default String is heap-backed and empty, so it can serve as a container
     slot before its real allocator and value are assigned. */
  String() : m_allocator(heap_allocator()) {}
  explicit String(Allocator allocator) : m_allocator(allocator) {}
  String(Allocator allocator, StringView initial) throws;
  /* Heap-backed conversions from a literal or a view. The literal form wins
     over the view form for a const char*, since it is a single conversion. */
  String(const char *cstr) throws;
  String(StringView initial) throws;

  String(const String &other) throws;
  String(String &&other) wontthrow;

  fn operator=(const String &other) throws -> String &;
  fn operator=(String &&other) wontthrow -> String &;

  ~String() { free_storage(); }

  mustuse pure fn count() const wontthrow -> usize { return m_length; }
  mustuse pure fn is_empty() const wontthrow -> bool { return m_length == 0; }
  mustuse pure fn operator[](usize i) const wontthrow -> char
  {
    return m_data[i];
  }
  mustuse pure fn view() const wontthrow -> StringView
  {
    return StringView{m_data, m_length};
  }
  /* A String reads as a view wherever one is expected, so an owned string
     passes to a comparison or a function taking a view without spelling out
     view(). */
  operator StringView() const wontthrow { return StringView{m_data, m_length}; }
  mustuse pure fn c_str() const wontthrow -> const char *
  {
    return m_data != nullptr ? m_data : "";
  }

  fn clear() wontthrow -> void;

  fn push(char c) throws -> void;
  fn append(StringView other) throws -> void;

  fn reserve(usize needed) throws -> void;

  /* The byte buffer, always null terminated. */
  mustuse pure fn data() const wontthrow -> const char * { return c_str(); }
  mustuse pure fn length() const wontthrow -> usize { return m_length; }
  mustuse pure fn back() const wontthrow -> char
  {
    ASSERT(m_length > 0, "back() on an empty string");
    return m_data[m_length - 1];
  }

  fn pop_back() wontthrow -> void;

  fn append(char c) throws -> void { push(c); }
  fn operator+=(StringView other) throws -> String &;
  fn operator+=(char c) throws -> String &;

  /* Search and slice forward to the view, so the owned string answers the same
     questions through the view without exposing its buffer. */
  mustuse pure fn find_character(char wanted) const wontthrow -> Maybe<usize>
  {
    return view().find_character(wanted);
  }
  mustuse pure fn substring(usize start) const wontthrow -> StringView
  {
    return view().substring(start);
  }
  mustuse pure fn substring_of_length(usize start, usize count) const wontthrow
      -> StringView
  {
    return view().substring_of_length(start, count);
  }
  mustuse pure fn starts_with(StringView prefix) const wontthrow -> bool
  {
    return view().starts_with(prefix);
  }

  mustuse pure fn operator==(StringView other) const wontthrow -> bool
  {
    return view() == other;
  }
  mustuse pure fn operator!=(StringView other) const wontthrow -> bool
  {
    return !(view() == other);
  }

  /* Byte order, so a sort matches the C locale collating order. */
  mustuse pure fn operator<(const String &other) const wontthrow -> bool;

  /* The first byte. The caller guarantees the string is not empty. */
  mustuse pure fn first_character() const wontthrow -> char
  {
    ASSERT(m_length > 0, "first_character() on an empty string");
    return m_data[0];
  }

  /* The index of the first occurrence of a substring at or after a start, or
     None when it is absent. */
  mustuse pure fn find_substring(StringView needle, usize from = 0) const
      wontthrow -> Maybe<usize>;

  /* The index of the last occurrence of a byte, or None when it is absent. */
  mustuse pure fn find_last_character(char wanted) const wontthrow
      -> Maybe<usize>;

private:
  fn free_storage() wontthrow -> void;

  Allocator m_allocator;
  char *m_data{nullptr};
  usize m_length{0};
  usize m_capacity{0};
};

/* Concatenate two byte ranges into a fresh heap String. A String and a literal
   both read as a view, so str + "x", "x" + str, and str + str all resolve here.
   The result is heap-backed, the default for an expression temporary. */
fn operator+(StringView left, StringView right) throws -> String;

} /* namespace shit */
