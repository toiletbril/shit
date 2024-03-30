#pragma once

#include <stddef.h>
#include <stdlib.h>

#if defined __GNUC__ || defined __clang__ || defined __COSMOCC__
#define T__HAS_GCC_EXTENSIONS 1
#define t__used               __attribute__((used))
#define t__likely(x)          __builtin_expect(!!(x), 1)
#define t__unlikely(x)        __builtin_expect(!!(x), 0)
#define t__mustuse            __attribute__((warn_unused_result))
#define t__threadlocal        __thread
#define t__wontreturn         __attribute__((noreturn))
#define t__maybeunused        __attribute__((unused))
#define t__forceinline        inline __attribute__((always_inline))
#define t__unreachable()      __builtin_unreachable()
#define t__debugtrap()        __builtin_trap()
#else /* __GNUC__ || __clang__ || __COSMOCC__ */
#error Oh no! Segmentation fault. Please download a better compiler that \
       supports GNU extensions!
#define t__used          /* nothing */
#define t__likely(x)     (x)
#define t__unlikely(x)   (x)
#define t__mustuse       /* nothing */
#define t__threadlocal   /* nothing */
#define t__wontreturn    /* nothing */
#define t__maybeunused   /* nothing */
#define t__forceinline   /* nothing */
#define t__unreachable() abort()
#define t__debugtrap()   abort()
#endif

/* Do not remove the symbol, even if it's not used. */
#define used t__used
/* x is likely to be true. */
#define likely(x) t__likely(x)
/* x is likely to be false. */
#define unlikely(x) t__unlikely(x)
/* The return value must be used. */
#define mustuse t__mustuse
/* Variable is unique to each thread. */
#define threadlocal t__threadlocal
/* The function will not return. */
#define wontreturn t__wontreturn
/* The value is not used. */
#define maybeunused t__maybeunused
/* The function is always inlined. */
#define forceinline t__forceinline
/* The value is unused. Suppress the compiler warning. */
#define unused(x) ((void) (x))

#define t__concat_literal(x, y) x##y
/* Concatenate two identifiers without quoting them. */
#define concat_literal(x, y) t__concat_literal(x, y)

template <typename T> struct t__exit_scope {
  T lambda;
  t__exit_scope(T lambda) : lambda(lambda) {}
  ~t__exit_scope() { lambda(); }
  t__exit_scope(const t__exit_scope &);

private:
  t__exit_scope &operator=(const t__exit_scope &);
};

class t__exit_scope_help
{
public:
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
