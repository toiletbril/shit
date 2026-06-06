#pragma once

#include "Common.hpp"

namespace shit {
struct String;
} /* namespace shit */

#if !defined NDEBUG
#include <cstdio>
/* fprintf(stderr, ...) */
#define SHIT_TRACE(...)                                                        \
  do {                                                                         \
    unused(                                                               \
        std::fprintf(stderr, "[SHIT_TRACE] " __FILE__ ":%d: ", __LINE__));     \
    unused(std::fprintf(stderr, __VA_ARGS__));                            \
    unused(fflush(stderr));                                               \
  } while (0)
/* fprintf(stderr, ... + "\n") */
#define SHIT_TRACELN(...)                                                      \
  do {                                                                         \
    unused(                                                               \
        std::fprintf(stderr, "[SHIT_TRACE] " __FILE__ ":%d: ", __LINE__));     \
    unused(std::fprintf(stderr, __VA_ARGS__));                            \
    unused(fputc('\n', stderr));                                          \
    unused(fflush(stderr));                                               \
  } while (0)
#if defined __clang__
#include <cstdarg>
/* The string parameter is a template so the body sees a complete ::shit::String
   only at the call site. Debug.hpp can not include String.hpp, since String.hpp
   includes Debug.hpp, so naming String here would close an include cycle. */
template <class StringT>
SHIT_USED void t__strprintf(StringT &s, const char *fmt, ...)
{
  va_list a;
  va_start(a, fmt);
  va_list ac;
  va_copy(ac, a);
  int written = vsnprintf(nullptr, 0, fmt, ac);
  if (written < 0) {
    va_end(ac);
    va_end(a);
    return;
  }
  usize n = static_cast<usize>(written);
  char *b = new char[n + 1];
  unused(vsnprintf(b, n + 1, fmt, a));
  s.append(b);
  delete[] b;
  va_end(ac);
  va_end(a);
}
/* The string type is a template parameter so the return type is dependent and
   its completeness is checked at the call site, where ::shit::String is whole,
   rather than here where it is only forward-declared. */
template <class StringT, class T>
StringT t__string_from_struct(const T &x)
{
  StringT s{};
  __builtin_dump_struct(&x, t__strprintf<StringT>, s);
  return s;
}
#define SHIT_STRUCT_STRING(x) ::shit::t__string_from_struct<::shit::String>(x)
#endif
#else /* !NDEBUG */
#define SHIT_STRUCT_STRING(...) ::shit::String{"<optimized out>"}
#define SHIT_TRACE(...)         /* None */
#define SHIT_TRACELN(...)       /* None */
#endif

#if !defined SHIT_STRUCT_STRING
#define SHIT_STRUCT_STRING(...) ::shit::String{"<not supported>"}
#endif

#define t__va_are_empty(...) (sizeof((char[]) {#__VA_ARGS__}) == 1)

/* True if __VA_ARGS__ passed as an argument is empty. */
#define SHIT_VA_ARE_EMPTY(...) t__va_are_empty(__VA_ARGS__)

#if !defined NDEBUG
/* Cause the debugger to break on this call. */
#define SHIT_TRAP(...)                                                         \
  do {                                                                         \
    SHIT_TRACELN("Encountered a debug trap");                                  \
    if (!SHIT_VA_ARE_EMPTY(__VA_ARGS__)) {                                     \
      SHIT_TRACELN("Details: " __VA_ARGS__);                                   \
    }                                                                          \
    t__debugtrap();                                                            \
  } while (0)
#else
#define SHIT_TRAP(...) abort()
#endif

#if !defined NDEBUG
/* This code path is unreachable. */
#define unreachable(...)                                                  \
  do {                                                                         \
    SHIT_TRACELN("Reached an unreachable statement");                          \
    if (!SHIT_VA_ARE_EMPTY(__VA_ARGS__)) {                                     \
      SHIT_TRACELN("Details: " __VA_ARGS__);                                   \
    }                                                                          \
    t__unreachable();                                                          \
  } while (0)
#else
#define unreachable(...) t__unreachable()
#endif

#if !defined NDEBUG
#define ASSERT(x, ...)                                                    \
  do {                                                                         \
    if (!(x)) [[unlikely]] {                                                   \
      SHIT_TRACELN("'ASSERT(" #x ")' fail in %s().", __func__);           \
      if (!SHIT_VA_ARE_EMPTY(__VA_ARGS__)) {                                   \
        SHIT_TRACELN("Details: " __VA_ARGS__);                                 \
      }                                                                        \
      SHIT_TRAP();                                                             \
    }                                                                          \
  } while (0)
#else
#define ASSERT(...) /* None */
#endif
