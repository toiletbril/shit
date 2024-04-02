#pragma once

#include "Common.hpp"

#if !defined NDEBUG
#include <cstdio>
#define t__trace_lf() UNUSED(fputc('\n', stderr))
/* fprintf(stderr, ...) */
#define TRACE(...)                                                             \
  do {                                                                         \
    UNUSED(std::fprintf(stderr, "[TRACE] " __FILE__ ":%d: ", __LINE__));       \
    UNUSED(std::fprintf(stderr, __VA_ARGS__));                                 \
  } while (0)
/* fprintf(stderr, ... + "\n") */
#define TRACELN(...)                                                           \
  do {                                                                         \
    TRACE(__VA_ARGS__);                                                        \
    t__trace_lf();                                                             \
  } while (0)
#else                 /* !NDEBUG */
#define t__trace_lf() /* nothing */
#define TRACE(...)    /* nothing */
#define TRACELN(...)  /* nothing */
#endif

/* Cause the debugger to break on this call. */
#define DEBUGTRAP()                                                            \
  do {                                                                         \
    TRACELN("Encountered a debug trap");                                       \
    t__debugtrap();                                                            \
  } while (0)

/* This code path is unreachable. */
#define UNREACHABLE()                                                          \
  do {                                                                         \
    TRACELN("Reached an unreachable statement");                               \
    t__unreachable();                                                          \
  } while (0)

#define t__va_is_empty(...) (sizeof((char[]){#__VA_ARGS__}) == 1)

/* True if __VA_ARGS__ passed as an argument is empty. */
#define VA_IS_EMPTY(...) t__va_is_empty(__VA_ARGS__)

/* Like assert(), but more fancy. Format string containing error details can be
   specified after the condition. */
#define INSIST(x, ...)                                                         \
  do {                                                                         \
    if (!(x)) [[unlikely]] {                                                   \
      TRACELN("'INSIST(" #x ")' fail in %s().", __func__);                     \
      if (!t__va_is_empty(__VA_ARGS__))                                        \
        TRACELN("Details: " __VA_ARGS__);                                      \
      DEBUGTRAP();                                                             \
    }                                                                          \
  } while (0)

#if T__HAS_GCC_EXTENSIONS
#define t__unique_name() concat_literal(t__var, __LINE__)
#define NONNULL(ptr)                                                           \
  ({                                                                           \
    typeof(ptr) t__unique_name() = ptr;                                        \
    INSIST(t__unique_name() != NULL);                                          \
    t__unique_name();                                                          \
  })
#else
#define NONNULL(...) (0)
#endif
