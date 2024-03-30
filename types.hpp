#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Crablang T___T */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef u8 uchar;
typedef i8 ichar;

/* The maximum size of a theoretically possible object of any type. */
typedef size_t usize;
/* The result of subtracting two pointers. */
typedef ptrdiff_t isize;

typedef uintptr_t uintptr;

/* Type signature of malloc() */
typedef void *(*mallocfn)(usize);
/* Type signature of realloc() */
typedef void *(*reallocfn)(void *, usize);
/* Type signature of free() */
typedef void (*freefn)(void *);
