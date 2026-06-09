#pragma once

/* Timestamp the build. */

#if !defined SHIT_ENVCXXFLAGS
#warning SHIT_ENVCXXFLAGS should be defined. Please use provided makefile \
         for compilation!
#define SHIT_ENVCXXFLAGS "<unknown>"
#endif

#if !defined SHIT_COMPILER_COMMAND
#warning SHIT_COMPILER_COMMAND should be defined. Please use provided makefile \
         for compilation!
#define SHIT_COMPILER_COMMAND "<unknown>"
#endif

#if !defined SHIT_COMMIT_HASH
#warning SHIT_COMMIT_HASH should be defined. Please use provided makefile for \
         compilation!
#define SHIT_COMMIT_HASH "<unknown>"
#endif

#if !defined SHIT_BUILD_MODE
#warning SHIT_BUILD_MODE should be defined. Please use provided makefile for \
         compilation!
#define SHIT_BUILD_MODE "<unset>"
#endif

#if !defined SHIT_OS_INFO
#warning SHIT_OS_INFO should be defined. Please use provided makefile for \
         compilation!
#define SHIT_OS_INFO "<unset>"
#endif

#define SHIT_BUILD_DATE (__DATE__ " at " __TIME__)

#define SHIT_COMPILER SHIT_COMPILER_COMMAND " (" __VERSION__ ")"

/* Constants for --help and --version. */

#define SHIT_VER_MAJOR 0
#define SHIT_VER_MINOR 0
#define SHIT_VER_PATCH 6
#define SHIT_VER_EXTRA "alpha"

/* The version as a compile-time string, so the startup does not rebuild it from
   the numeric parts with a String allocation on every invocation. */
#define SHIT_STRINGIFY_INNER(x) #x
#define SHIT_STRINGIFY(x)       SHIT_STRINGIFY_INNER(x)
#define SHIT_VERSION_STRING                                                    \
  SHIT_STRINGIFY(SHIT_VER_MAJOR)                                               \
  "." SHIT_STRINGIFY(SHIT_VER_MINOR) "." SHIT_STRINGIFY(SHIT_VER_PATCH)        \
  "-" SHIT_VER_EXTRA

#define SHIT_SHORT_LICENSE                                                     \
  "Licensed under the 3-Clause BSD License.\n"                                 \
  "There is NO WARRANTY, to the extent permitted by law."

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

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
#define t__used               /* None */
#define t__forceinline        /* None */
#define t__unreachable()      abort()
#define t__debugtrap()        abort()
#endif

#define donteliminate t__used
#define forceinline   t__forceinline
#define unused(x)     ((void) (x))

#define t__concat_literal(x, y) x##y
#define concat_literal(x, y)    t__concat_literal(x, y)

template <typename T>
class t__exit_scope
{
public:
  t__exit_scope(T lambda) : m_lambda(lambda) {}
  ~t__exit_scope() { m_lambda(); }
  t__exit_scope(const t__exit_scope &);

private:
  T m_lambda;
  t__exit_scope &operator=(const t__exit_scope &);
};

class t__exit_scope_help
{
public:
  template <typename T>
  t__exit_scope<T> operator+(T t)
  {
    return t;
  }
};

/* Defer a block until the end of the scope. */
#define defer                                                                  \
  const auto &concat_literal(defer__, __LINE__) = t__exit_scope_help() + [&]()

/* Silence enum warnings. */
#define ENUM(e) static_cast<int>(e)

#define sub_sat(a, b) ((a) > (b) ? (a) - (b) : 0)

/* The length of statically allocated array. */
#define countof(arr) (sizeof(arr) / sizeof(*(arr)))

/* Move a value, reading as taking ownership out of the source. */
#define steal std::move

/* A return value the caller must not ignore. */
#define mustuse [[nodiscard]]

/* Every function is written `fn name(args) -> ret` and every variable `let x`,
   so the name leads and the type trails, matching the oo style. */
#define fn  auto
#define let auto

/* Markers for a function's effect and exception behavior, written next to the
   fn so the declaration states intent. They map to the matching clang
   constructs. A pure function's result depends only on its arguments, written
   before the fn. A wont_throw function never throws and a may_throw one may,
   written after the parameter list where noexcept goes. */
#if T__HAS_GCC_EXTENSIONS
#define pure __attribute__((pure))
#else
#define pure
#endif

/* Every function and method states wontthrow or throws where noexcept goes,
   both for free functions and members since noexcept applies to both. They map
   to the noexcept keyword and its negation. */
#define wontthrow noexcept
#define throws    noexcept(false)

/* Speed hints written before the fn, mapping to clang attributes. cold marks a
   rarely-taken path such as error reporting so the compiler keeps it out of the
   hot icache, hot marks the evaluation core, flatten inlines a small
   dispatcher's whole call tree, and noinline pins a large cold body out of
   line. A branch is hinted with the literal [[likely]]/[[unlikely]] where it
   occurs. */
#if T__HAS_GCC_EXTENSIONS
#define cold     [[gnu::cold]]
#define hot      [[gnu::hot]]
#define flatten  [[gnu::flatten]]
#define noinline [[gnu::noinline]]
#else
#define cold
#define hot
#define flatten
#define noinline
#endif

namespace shit {
constexpr const char *EXPRESSION_AST_INDENT = " ";
constexpr const char *EXPRESSION_DOUBLE_AST_INDENT = "  ";
} /* namespace shit */
