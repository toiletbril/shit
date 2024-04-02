#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

/* Crablang types */

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
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
#define t__threadlocal        /* nothing */
#define t__forceinline        /* nothing */
#define t__unreachable()      abort()
#define t__debugtrap()        abort()
#endif

/* Do not remove the symbol, even if it's not used. */
#define USED t__used
/* The return value must be used. */
#define FORCEINLINE t__forceinline
/* The value is unused. Suppress the compiler warning. */
#define UNUSED(x) ((void) (x))

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

/* Defer a block until the end of the scope. */
#define defer                                                                  \
  const auto &concat_literal(defer__, __LINE__) = t__exit_scope_help() + [&]()

/* The length of statically allocated array. */
#define countof(arr) (sizeof(arr) / sizeof(*(arr)))
