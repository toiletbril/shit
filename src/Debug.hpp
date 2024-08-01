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
    fflush(stderr);                                                            \
  } while (0)
/* fprintf(stderr, ... + "\n") */
#define SHIT_TRACELN(...)                                                      \
  do {                                                                         \
    SHIT_UNUSED(                                                               \
        std::fprintf(stderr, "[SHIT_TRACE] " __FILE__ ":%d: ", __LINE__));     \
    SHIT_UNUSED(std::fprintf(stderr, __VA_ARGS__));                            \
    SHIT_UNUSED(fputc('\n', stderr));                                          \
    fflush(stderr);                                                            \
  } while (0)
#else /* !NDEBUG */
#define SHIT_TRACE(...)   /* nothing */
#define SHIT_TRACELN(...) /* nothing */
#endif

/* Cause the debugger to break on this call. */
#define SHIT_TRAP(...)                                                         \
  do {                                                                         \
    SHIT_TRACELN("Encountered a debug trap");                                  \
    if (!t__va_is_empty(__VA_ARGS__)) {                                        \
      SHIT_TRACELN("Details: " __VA_ARGS__);                                   \
    }                                                                          \
    t__debugtrap();                                                            \
  } while (0)

/* This code path is unreachable. */
#define SHIT_UNREACHABLE(...)                                                  \
  do {                                                                         \
    SHIT_TRACELN("Reached an unreachable statement");                          \
    if (!t__va_is_empty(__VA_ARGS__)) {                                        \
      SHIT_TRACELN("Details: " __VA_ARGS__);                                   \
    }                                                                          \
    t__unreachable();                                                          \
  } while (0)

#define t__va_is_empty(...) (sizeof((char[]){#__VA_ARGS__}) == 1)

/* True if __VA_ARGS__ passed as an argument is empty. */
#define SHIT_VA_ARE_EMPTY(...) t__va_is_empty(__VA_ARGS__)

/* Like assert(), but more fancy. Format string containing error details can be
   specified after the condition. */
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
