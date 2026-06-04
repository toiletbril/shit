#pragma once

#include "Debug.hpp"

#include <new>
#include <utility>

namespace shit {

/* The empty state of a Maybe. A function returns this to say it produced no
   value, and the caller decides what that means. */
struct None
{};

inline constexpr None nothing{};

/* A value or nothing, the replacement for std::optional. There is no failure
   reason inside, so the caller handles the empty case itself. Reading the value
   out of an empty Maybe traps in the debug build. The value is stored inline,
   so no allocation happens. */
template <class T>
struct [[nodiscard]] Maybe
{
  Maybe() noexcept : m_has_value(false) {}
  Maybe(None) noexcept : m_has_value(false) {}
  Maybe(T value) : m_has_value(true) { new (&m_storage) T(std::move(value)); }

  Maybe(const Maybe &other) : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_storage) T(other.reference());
  }
  Maybe(Maybe &&other) noexcept : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_storage) T(std::move(other.reference()));
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
      if (m_has_value) new (&m_storage) T(std::move(other.reference()));
    }
    return *this;
  }

  ~Maybe() { reset(); }

  [[nodiscard]] bool has_value() const noexcept { return m_has_value; }
  [[nodiscard]] explicit operator bool() const noexcept { return m_has_value; }

  [[nodiscard]] T &value()
  {
    SHIT_ASSERT(m_has_value);
    return reference();
  }
  [[nodiscard]] const T &value() const
  {
    SHIT_ASSERT(m_has_value);
    return reference();
  }
  [[nodiscard]] T &operator*() { return value(); }
  [[nodiscard]] const T &operator*() const { return value(); }
  [[nodiscard]] T *operator->() { return &reference(); }
  [[nodiscard]] const T *operator->() const { return &reference(); }

  /* Move the value out, leaving the Maybe empty. */
  [[nodiscard]] T take()
  {
    SHIT_ASSERT(m_has_value);
    T moved = std::move(reference());
    reset();
    return moved;
  }

  /* The value when present, otherwise the fallback. */
  [[nodiscard]] T value_or(T fallback) const
  {
    return m_has_value ? reference() : std::move(fallback);
  }

  /* Equal to a bare value only when present and that value matches, so a
     comparison reads like the one against a std::optional. */
  [[nodiscard]] bool operator==(const T &other) const
  {
    return m_has_value && reference() == other;
  }
  [[nodiscard]] bool operator!=(const T &other) const
  {
    return !(*this == other);
  }

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

/* Evaluate a Maybe expression, return nothing from the enclosing function when
   it is empty, otherwise yield the value. The enclosing function must itself
   return a Maybe. */
#define SHIT_TRY(maybe_expr)                                                   \
  ({                                                                           \
    auto t__result = (maybe_expr);                                            \
    if (!t__result) return ::shit::nothing;                                   \
    t__result.take();                                                          \
  })

} /* namespace shit */
