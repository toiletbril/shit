#pragma once

/* A value or an Error, returned by a function that can fail instead of
   throwing. The error path carries the same Error the throwing code built, so
   the located reporting at the boundary stays identical. The variant is
   accessed through get_if rather than get, so the no-exceptions build never
   reaches a throwing path. */

#include "Common.hpp"
#include "Errors.hpp"

#include <string>
#include <utility>
#include <variant>

namespace shit {

/* The success payload of a fallible function that returns no value. A caller
   writes ErrorOr<Ok> and returns Ok{} on success. */
struct Ok
{};

template <class T>
struct [[nodiscard]] ErrorOr
{
  ErrorOr(T value) : m_data(std::move(value)) {}
  ErrorOr(Error error) : m_data(std::move(error)) {}

  bool is_error() const { return m_data.index() == 1; }

  T &value() { return *std::get_if<T>(&m_data); }
  const T &value() const { return *std::get_if<T>(&m_data); }

  Error &error() { return *std::get_if<Error>(&m_data); }
  const Error &error() const { return *std::get_if<Error>(&m_data); }

  /* Move the value out, called once the caller has checked is_error. */
  T take() { return std::move(*std::get_if<T>(&m_data)); }

private:
  std::variant<T, Error> m_data;
};

} /* namespace shit */

/* Evaluate a fallible expression, return its error early on failure, and yield
   its value otherwise. Used inside a function that itself returns an ErrorOr.
   The name differs from the Maybe SHIT_TRY, since this one propagates an Error
   rather than None. */
#define SHIT_UNWRAP(expr)                                                      \
  ({                                                                           \
    auto t__result = (expr);                                                   \
    if (t__result.is_error()) return t__result.error();                       \
    t__result.take();                                                          \
  })

/* Build an Error whose message carries the source file and line, for an
   internal failure that should not normally reach the user. */
#define SHIT_MAKE_ERROR(msg)                                                   \
  ::shit::Error                                                                \
  {                                                                            \
    std::string{__FILE__ ":"} + std::to_string(__LINE__) + ": " + (msg)       \
  }
