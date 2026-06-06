#pragma once

#include "Debug.hpp"

#include <new>
#include <utility>

namespace shit {

/* The empty state of a Maybe. A function returns None to say it produced no
   value, and the caller decides what that means. */
class Nothing
{};

inline constexpr Nothing None{};

/* A value or None, the replacement for std::optional. There is no failure
   reason inside, so the caller handles the empty case itself. Reading the value
   out of an empty Maybe traps in the debug build. The value is stored inline,
   so no allocation happens. */
template <class T>
class mustuse Maybe
{
public:
  Maybe() noexcept : m_has_value(false) {}
  Maybe(Nothing) noexcept : m_has_value(false) {}
  Maybe(T value) : m_has_value(true) { new (&m_storage) T(steal(value)); }

  Maybe(const Maybe &other) : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_storage) T(other.reference());
  }
  Maybe(Maybe &&other) noexcept : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_storage) T(steal(other.reference()));
  }

  Maybe &operator=(const Maybe &other)
  {
    if (this != &other) {
      reset();
      m_has_value = other.m_has_value;
      if (m_has_value) new (&m_storage) T(other.reference());
    }
    return *this;
  }
  Maybe &operator=(Maybe &&other) noexcept
  {
    if (this != &other) {
      reset();
      m_has_value = other.m_has_value;
      if (m_has_value) new (&m_storage) T(steal(other.reference()));
    }
    return *this;
  }

  ~Maybe() { reset(); }

  mustuse bool has_value() const noexcept { return m_has_value; }
  mustuse explicit operator bool() const noexcept { return m_has_value; }

  mustuse T &value()
  {
    ASSERT(m_has_value);
    return reference();
  }
  mustuse const T &value() const
  {
    ASSERT(m_has_value);
    return reference();
  }
  mustuse T &operator*() { return value(); }
  mustuse const T &operator*() const { return value(); }
  mustuse T *operator->() { return &reference(); }
  mustuse const T *operator->() const { return &reference(); }

  /* Move the value out, leaving the Maybe empty. */
  mustuse T take()
  {
    ASSERT(m_has_value);
    T moved = steal(reference());
    reset();
    return moved;
  }

  /* The value when present, otherwise the fallback. */
  mustuse T value_or(T fallback) const
  {
    return m_has_value ? reference() : steal(fallback);
  }

  /* Equal to a bare value only when present and that value matches, so a
     comparison reads like the one against a std::optional. */
  mustuse bool operator==(const T &other) const
  {
    return m_has_value && reference() == other;
  }
  mustuse bool operator!=(const T &other) const { return !(*this == other); }

  /* Drop the value, leaving the Maybe empty. */
  void reset() noexcept
  {
    if (m_has_value) {
      reference().~T();
      m_has_value = false;
    }
  }

private:
  T &reference() noexcept { return *reinterpret_cast<T *>(&m_storage); }
  const T &reference() const noexcept
  {
    return *reinterpret_cast<const T *>(&m_storage);
  }

  bool m_has_value;
  alignas(T) unsigned char m_storage[sizeof(T)];
};

/* Evaluate a Maybe expression, return None from the enclosing function when
   it is empty, otherwise yield the value. The enclosing function must itself
   return a Maybe. */
#define UNWRAP(maybe_expr)                                                     \
  ({                                                                           \
    auto t__result = (maybe_expr);                                             \
    if (!t__result) return ::shit::None;                                       \
    t__result.take();                                                          \
  })

} /* namespace shit */
