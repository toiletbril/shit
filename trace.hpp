#pragma once

#include "common.hpp"

/* clang-format off */

#if !defined NDEBUG
#include <stdio.h>
#define t__trace_lf() (void) fputc('\n', stderr)
/* fprintf(stderr, ...) */
#define trace(...)                                                       \
	do {                                                                 \
		(void) fprintf(stderr, "\n[trace] " __FILE__ ":%d: ", __LINE__); \
		(void) fprintf(stderr, __VA_ARGS__);                             \
	} while (0)
/* fprintf(stderr, ... + "\n") */
#define traceln(...)         \
	do                       \
	{                        \
		trace(__VA_ARGS__);  \
		t__trace_lf();       \
	} while (0)
#else /* !NDEBUG */
#define t__trace_lf() /* nothing */
#define trace(...)    /* nothing */
#define traceln(...)  /* nothing */
#endif

/* clang-format on */

/* Cause the debugger to break on this call. */
#define debugtrap()                        \
	do                                     \
	{                                      \
		trace("Encountered a debug trap"); \
		t__debugtrap();                    \
	} while (0)

/* This code path is unreachable. */
#define unreachable()                                \
	do                                               \
	{                                                \
		traceln("Reached an unreachable statement"); \
		t__unreachable();                            \
	} while (0)

#define t__va_is_empty(...) (sizeof((char[]){#__VA_ARGS__}) == 1)

/* True if __VA_ARGS__ passed as an argument is empty. */
#define va_is_empty(...) t__va_is_empty(__VA_ARGS__)

/* Like assert(), but more fancy. Format string containing error details can be
   specified after the condition. */
#define insist(x, ...)                                         \
	do                                                         \
	{                                                          \
		if (!likely(x))                                        \
		{                                                      \
			trace("'insist(" #x ")' fail in %s().", __func__); \
			if (!t__va_is_empty(__VA_ARGS__))                  \
				traceln("Details: " __VA_ARGS__);              \
			else                                               \
				t__trace_lf();                                 \
			debugtrap();                                       \
		}                                                      \
	} while (0)

/* Trace and do `catch_` if `expr` is not true. Format string containing error
   details can be specified after the `catch_`. */
#define true_or(expr, catch_, ...)                                 \
	do                                                             \
	{                                                              \
		if (!likely(expr))                                         \
		{                                                          \
			trace("'true_or(" #expr ")' fail in %s().", __func__); \
			if (!t__va_is_empty(__VA_ARGS__))                      \
				traceln("Details: " __VA_ARGS__);                  \
			else                                                   \
				t__trace_lf();                                     \
			catch_                                                 \
		}                                                          \
	} while (0)

#if T__HAS_GCC_EXTENSIONS
#define t__unique_name() concat_literal(t__var, __LINE__)
#define nonnull(ptr)                      \
	({                                    \
		void *t__unique_name() = ptr;     \
		insist(t__unique_name() != NULL); \
		t__unique_name();                 \
	})
#else
#define nonnull(...) (0)
#endif
