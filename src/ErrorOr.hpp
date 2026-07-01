#pragma once

/* A value or an Error, returned by a function that can fail instead of
   throwing. The error path carries the same Error the throwing code built. The
   active member is read through is_error and then value or error, so the
   no-exceptions build never reaches a throwing path. */

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"

namespace shit {

/* The success payload of a fallible function that returns no value. A caller
   writes ErrorOr<Ok> and returns Success on success. */
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

  mustuse fn clone() const throws -> ErrorOr { return ErrorOr{*this}; }

  fn operator=(const ErrorOr &other) throws->ErrorOr &
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
  fn operator=(ErrorOr &&other) noexcept -> ErrorOr &
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

  hot mustuse pure fn is_error() const wontthrow -> bool { return m_is_error; }

  hot mustuse pure fn value() wontthrow -> T &
  {
    ASSERT(!m_is_error);
    return value_reference();
  }
  hot mustuse pure fn value() const wontthrow -> const T &
  {
    ASSERT(!m_is_error);
    return value_reference();
  }

  mustuse pure fn error() wontthrow -> Error &
  {
    ASSERT(m_is_error);
    return error_reference();
  }
  mustuse pure fn error() const wontthrow -> const Error &
  {
    ASSERT(m_is_error);
    return error_reference();
  }

  /* Move the value out, called once the caller has checked is_error. */
  hot mustuse fn take() throws -> T
  {
    ASSERT(!m_is_error);
    return steal(value_reference());
  }

private:
  fn destroy() wontthrow -> void
  {
    if (m_is_error)
      error_reference().~Error();
    else
      value_reference().~T();
  }

  hot flatten fn value_reference() wontthrow -> T &
  {
    return *reinterpret_cast<T *>(&m_storage);
  }
  hot flatten fn value_reference() const wontthrow -> const T &
  {
    return *reinterpret_cast<const T *>(&m_storage);
  }
  fn error_reference() wontthrow -> Error &
  {
    return *reinterpret_cast<Error *>(&m_storage);
  }
  fn error_reference() const wontthrow -> const Error &
  {
    return *reinterpret_cast<const Error *>(&m_storage);
  }

  bool m_is_error;
  alignas(T) alignas(
      Error) unsigned char m_storage[sizeof(T) > sizeof(Error) ? sizeof(T)
                                                               : sizeof(Error)];
};

} // namespace shit

/* Evaluate a fallible expression, return its error early on failure, and yield
   its value otherwise. Used inside a function that itself returns an ErrorOr.
 */
#define TRY(expr)                                                              \
  ({                                                                           \
    auto t__result = (expr);                                                   \
    if (t__result.is_error()) [[unlikely]]                                     \
      return t__result.error();                                                \
    t__result.take();                                                          \
  })

/* Build an Error whose message carries the source file and line, for an
   internal failure that should not normally reach the user. */
#define MAKE_ERROR(msg)                                                        \
  ::shit::Error                                                                \
  {                                                                            \
    ::shit::String{__FILE__ ":"} +                                             \
        ::shit::String::from(__LINE__, ::shit::heap_allocator()) + ": " +      \
        (msg)                                                                  \
  }
