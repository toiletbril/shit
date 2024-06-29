#pragma once

/* Timestamp the build. */

#if !defined SHIT_COMMIT_HASH
#warning SHIT_COMMIT_HASH should be defined.Please use provided makefile for \
         compilation!
#define SHIT_COMMIT_HASH "<unknown>"
#endif

#if !defined SHIT_BUILD_MODE
#warning SHIT_BUILD_MODE should be defined. Please use provided makefile for \
         compilation!
#define SHIT_BUILD_MODE "<unset>"
#endif

#define SHIT_BUILD_DATE (__DATE__ " at " __TIME__)

/* Constants for --help and --version. */

#define SHIT_VER_MAJOR 0
#define SHIT_VER_MINOR 0
#define SHIT_VER_PATCH 5
#define SHIT_VER_EXTRA "alpha"

#define SHIT_SHORT_LICENSE                                                     \
  "Licensed under the 3-Clause BSD License.\n"                                 \
  "There is NO WARRANTY, to the extent permitted by law."

#include <cstddef>
#include <cstdint>
#include <cstdlib>

/* Crablang types. */

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using uchar = u8;
using ichar = i8;

using usize = size_t;
using isize = ptrdiff_t;
using uintptr = uintptr_t;

#if defined __GNUC__ || defined __clang__ || defined __COSMOCC__
#define T__HAS_GCC_EXTENSIONS 1
#define t__used               __attribute__((used))
#define t__forceinline        inline __attribute__((always_inline))
#define t__unreachable()      __builtin_unreachable()
#define t__debugtrap()        __builtin_trap()
#else /* __GNUC__ || __clang__ || __COSMOCC__ */
#error Oh no! Segmentation fault. Please download a better compiler that \
       supports GNU extensions!
#define T__HAS_GCC_EXTENSIONS 0
#define t__used               /* nothing */
#define t__forceinline        /* nothing */
#define t__unreachable()      abort()
#define t__debugtrap()        abort()
#endif

/* Do not remove the symbol, even if it's not used. */
#define SHIT_USED t__used
/* The return value must be used. */
#define SHIT_FORCEINLINE t__forceinline
/* The value is unused. Suppress the compiler warning. */
#define SHIT_UNUSED(x) ((void) (x))

#define t__concat_literal(x, y) x##y
/* Concatenate two identifiers without quoting them. */
#define concat_literal(x, y) t__concat_literal(x, y)

template <typename T>
struct t__exit_scope
{
  t__exit_scope(T lambda) : m_lambda(lambda) {}
  ~t__exit_scope() { m_lambda(); }
  t__exit_scope(const t__exit_scope &);

private:
  T              m_lambda;
  t__exit_scope &operator=(const t__exit_scope &);
};

struct t__exit_scope_help
{
  template <typename T>
  t__exit_scope<T>
  operator+(T t)
  {
    return t;
  }
};

/* Silence enum warnings. */
#define E(e) static_cast<int>(e)

/* Defer a block until the end of the scope. */
#define SHIT_DEFER                                                             \
  const auto &concat_literal(defer__, __LINE__) = t__exit_scope_help() + [&]()

/* The length of statically allocated array. */
#define countof(arr) (sizeof(arr) / sizeof(*(arr)))
