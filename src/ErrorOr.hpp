#pragma once

/* A value or an Error, returned by a function that can fail instead of
   throwing. The error path carries the same Error the throwing code built, so
   the located reporting at the boundary stays identical. The variant is
   accessed through get_if rather than get, so the no-exceptions build never
   reaches a throwing path. */

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"

namespace shit {

namespace utils {
/* Declared here rather than included from Utils.hpp, since Utils.hpp includes
   ErrorOr.hpp and the include would close a cycle. The definition lives in
   Utils.cpp. */
String int_to_text(i64 value);
} /* namespace utils */

/* The success payload of a fallible function that returns no value. A caller
   writes ErrorOr<Ok> and returns Success on success, the way None is the one
   instance of Nothing. */
class Ok
{};

inline constexpr Ok Success{};

/* A value or an Error, held in one of two members of an explicit tagged union
   rather than a std::variant. The active member is tracked by m_is_error, and
   the storage is sized for the larger of the two, so only one is alive at a
   time and no heap allocation happens for the discriminant. */
template <class T>
class mustuse ErrorOr
{
public:
  ErrorOr(T value) : m_is_error(false) { new (&m_storage) T(steal(value)); }
  ErrorOr(Error error) : m_is_error(true)
  {
    new (&m_storage) Error(steal(error));
  }

  ErrorOr(const ErrorOr &other) : m_is_error(other.m_is_error)
  {
    if (m_is_error)
      new (&m_storage) Error(other.error_reference());
    else
      new (&m_storage) T(other.value_reference());
  }
  ErrorOr(ErrorOr &&other) noexcept : m_is_error(other.m_is_error)
  {
    if (m_is_error)
      new (&m_storage) Error(steal(other.error_reference()));
    else
      new (&m_storage) T(steal(other.value_reference()));
  }

  ErrorOr &operator=(const ErrorOr &other)
  {
    if (this != &other) {
      destroy();
      m_is_error = other.m_is_error;
      if (m_is_error)
        new (&m_storage) Error(other.error_reference());
      else
        new (&m_storage) T(other.value_reference());
    }
    return *this;
  }
  ErrorOr &operator=(ErrorOr &&other) noexcept
  {
    if (this != &other) {
      destroy();
      m_is_error = other.m_is_error;
      if (m_is_error)
        new (&m_storage) Error(steal(other.error_reference()));
      else
        new (&m_storage) T(steal(other.value_reference()));
    }
    return *this;
  }

  ~ErrorOr() { destroy(); }

  mustuse bool is_error() const { return m_is_error; }

  mustuse T &value()
  {
    ASSERT(!m_is_error);
    return value_reference();
  }
  mustuse const T &value() const
  {
    ASSERT(!m_is_error);
    return value_reference();
  }

  mustuse Error &error()
  {
    ASSERT(m_is_error);
    return error_reference();
  }
  mustuse const Error &error() const
  {
    ASSERT(m_is_error);
    return error_reference();
  }

  /* Move the value out, called once the caller has checked is_error. */
  mustuse T take()
  {
    ASSERT(!m_is_error);
    return steal(value_reference());
  }

private:
  void destroy() noexcept
  {
    if (m_is_error)
      error_reference().~Error();
    else
      value_reference().~T();
  }

  T &value_reference() noexcept { return *reinterpret_cast<T *>(&m_storage); }
  const T &value_reference() const noexcept
  {
    return *reinterpret_cast<const T *>(&m_storage);
  }
  Error &error_reference() noexcept
  {
    return *reinterpret_cast<Error *>(&m_storage);
  }
  const Error &error_reference() const noexcept
  {
    return *reinterpret_cast<const Error *>(&m_storage);
  }

  bool m_is_error;
  alignas(T) alignas(
      Error) unsigned char m_storage[sizeof(T) > sizeof(Error) ? sizeof(T)
                                                               : sizeof(Error)];
};

} /* namespace shit */

/* Evaluate a fallible expression, return its error early on failure, and yield
   its value otherwise. Used inside a function that itself returns an ErrorOr.
   The name differs from the Maybe UNWRAP, since this one propagates an
   Error rather than None. */
#define TRY(expr)                                                              \
  ({                                                                           \
    auto t__result = (expr);                                                   \
    if (t__result.is_error()) return t__result.error();                        \
    t__result.take();                                                          \
  })

/* Build an Error whose message carries the source file and line, for an
   internal failure that should not normally reach the user. */
#define MAKE_ERROR(msg)                                                        \
  ::shit::Error                                                                \
  {                                                                            \
    ::shit::String{__FILE__ ":"} + ::shit::utils::int_to_text(__LINE__) +      \
        ": " + (msg)                                                           \
  }
