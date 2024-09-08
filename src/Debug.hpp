#pragma once

#include "Common.hpp"

#if !defined NDEBUG
#include <cstdio>
/* fprintf(stderr, ...) */
#define SHIT_TRACE(...)                                                        \
  do {                                                                         \
    SHIT_UNUSED(                                                               \
        std::fprintf(stderr, "[SHIT_TRACE] " __FILE__ ":%d: ", __LINE__));     \
    SHIT_UNUSED(std::fprintf(stderr, __VA_ARGS__));                            \
    SHIT_UNUSED(fflush(stderr));                                               \
  } while (0)
/* fprintf(stderr, ... + "\n") */
#define SHIT_TRACELN(...)                                                      \
  do {                                                                         \
    SHIT_UNUSED(                                                               \
        std::fprintf(stderr, "[SHIT_TRACE] " __FILE__ ":%d: ", __LINE__));     \
    SHIT_UNUSED(std::fprintf(stderr, __VA_ARGS__));                            \
    SHIT_UNUSED(fputc('\n', stderr));                                          \
    SHIT_UNUSED(fflush(stderr));                                               \
  } while (0)
#if defined __clang__
#include <cstdarg>
#include <string>
SHIT_USED static void
t__strprintf(std::string &s, const char *fmt, ...)
{
  va_list a;
  va_start(a, fmt);
  usize n = vsnprintf(nullptr, 0, fmt, a);
  char *b = new char[n];
  SHIT_UNUSED(vsnprintf(b, n, fmt, a));
  s.append(b);
  delete[] b;
}
template <class T>
std::string
t__string_from_struct(const T &x)
{
  std::string s{};
  __builtin_dump_struct(&x, t__strprintf, s);
  return s;
}
#define SHIT_STRUCT_STRING(x) t__string_from_struct(x)
#endif
#else                     /* !NDEBUG */
#define SHIT_TRACE(...)   /* nothing */
#define SHIT_TRACELN(...) /* nothing */
#endif

#if !defined SHIT_STRUCT_STRING
#define SHIT_STRUCT_STRING(...)                                                \
  std::string { "<could not dump>" }
#endif

#define t__va_is_empty(...) (sizeof((char[]){#__VA_ARGS__}) == 1)

/* True if __VA_ARGS__ passed as an argument is empty. */
#define SHIT_VA_ARE_EMPTY(...) t__va_is_empty(__VA_ARGS__)

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
#define SHIT_UNREACHABLE(...)                                                  \
  do {                                                                         \
    SHIT_TRACELN("Reached an unreachable statement");                          \
    if (!SHIT_VA_ARE_EMPTY(__VA_ARGS__)) {                                     \
      SHIT_TRACELN("Details: " __VA_ARGS__);                                   \
    }                                                                          \
    t__unreachable();                                                          \
  } while (0)
#else
#define SHIT_UNREACHABLE(...) t__unreachable()
#endif

#if !defined NDEBUG
#define SHIT_ASSERT(x, ...)                                                    \
  do {                                                                         \
    if (!(x)) [[unlikely]] {                                                   \
      SHIT_TRACELN("'SHIT_ASSERT(" #x ")' fail in %s().", __func__);           \
      if (!SHIT_VA_ARE_EMPTY(__VA_ARGS__)) {                                   \
        SHIT_TRACELN("Details: " __VA_ARGS__);                                 \
      }                                                                        \
      SHIT_TRAP();                                                             \
    }                                                                          \
  } while (0)
#else
#define SHIT_ASSERT(...) /* nothing */
#endif
