#pragma once

#include "Common.hpp"

#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace shit {

/* Forward declared, so the allocator header does not include the arena header,
   which would close the ArrayList -> Allocator -> Arena include cycle. The bump
   adapter reaches the arena through a free function defined in Arena.cpp. */
class BumpArena;
fn bump_arena_allocate(BumpArena *arena, usize length, usize alignment) throws
    -> void *;

/* An allocator value, in the manner of Zig's std.mem.Allocator. It carries a
   context and a table of operations, so a data structure is handed the
   allocator it must use and frees through the same one. It is sixteen bytes and
   passed by value. */
class Allocator
{
public:
  struct VTable
  {
    void *(*alloc)(void *context, usize length, usize alignment);
    /* Grow or shrink in place. Returns false when the block cannot change size
       without moving, so the caller allocates and copies. */
    bool (*resize)(void *context, void *pointer, usize old_length,
                   usize new_length, usize alignment);
    void (*free)(void *context, void *pointer, usize length, usize alignment);
  };

  void *context;
  const VTable *vtable;

  hot flatten fn raw_alloc(usize length, usize alignment) const throws -> void *
  {
    return vtable->alloc(context, length, alignment);
  }
  fn raw_resize(void *pointer, usize old_length, usize new_length,
                usize alignment) const wontthrow -> bool
  {
    return vtable->resize(context, pointer, old_length, new_length, alignment);
  }
  flatten fn raw_free(void *pointer, usize length,
                      usize alignment) const wontthrow -> void
  {
    vtable->free(context, pointer, length, alignment);
  }

  template <class T>
  hot flatten fn alloc_array(usize count) const throws -> T *
  {
    /* The product overflows usize for a large enough count, wrapping to a small
       request that the caller then writes past. The division guards the
       multiply, since count times sizeof(T) cannot exceed the max when count is
       at most the max divided by sizeof(T). */
    if (sizeof(T) != 0 && count > (static_cast<usize>(-1) / sizeof(T)))
        [[unlikely]]
    {
      throw std::bad_alloc{};
    }
    return static_cast<T *>(raw_alloc(count * sizeof(T), alignof(T)));
  }
  template <class T>
  flatten fn free_array(T *pointer, usize count) const wontthrow -> void
  {
    raw_free(pointer, count * sizeof(T), alignof(T));
  }
};

namespace allocators {

/* The bump adapter over a BumpArena. A free is a no-op and a resize never grows
   in place, since the arena reclaims everything at once on reset. The whole
   lifetime of its allocations is the lifetime of the arena. */
hot inline fn bump_alloc(void *context, usize length, usize alignment) throws
    -> void *
{
  return bump_arena_allocate(static_cast<BumpArena *>(context), length,
                             alignment);
}
inline fn bump_resize(void *context, void *pointer, usize old_length,
                      usize new_length, usize alignment) wontthrow -> bool
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
inline fn bump_free(void *context, void *pointer, usize length,
                    usize alignment) wontthrow -> void
{
  unused(context);
  unused(pointer);
  unused(length);
  unused(alignment);
}

inline constexpr Allocator::VTable BUMP_VTABLE{bump_alloc, bump_resize,
                                               bump_free};

/* The heap adapter over the C allocator. It frees on demand, so it backs the
   long-lived mutable data the bump model would leak, the variable store above
   all. */
inline fn heap_alloc(void *context, usize length, usize alignment) wontthrow
    -> void *
{
  unused(context);
  /* malloc already meets every alignment up to alignof(max_align_t), so the
     common request stays on the plain path with no extra work. An over-aligned
     type takes aligned_alloc, whose result std::free accepts the same as a
     malloc result, so the free path needs no change. aligned_alloc wants a size
     that is a multiple of the alignment, so the length is rounded up. */
  if (alignment > alignof(max_align_t)) {
    let const rounded_length = (length + alignment - 1) & ~(alignment - 1);
#if defined(_WIN32)
    /* Windows has no aligned_alloc, and an _aligned_malloc result must be freed
       with _aligned_free, so the free path mirrors this branch on alignment. */
    return _aligned_malloc(rounded_length, alignment);
#else
    return std::aligned_alloc(alignment, rounded_length);
#endif
  }
  return std::malloc(length);
}
inline fn heap_resize(void *context, void *pointer, usize old_length,
                      usize new_length, usize alignment) wontthrow -> bool
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
inline fn heap_free(void *context, void *pointer, usize length,
                    usize alignment) wontthrow -> void
{
  unused(context);
  unused(length);
#if defined(_WIN32)
  /* An over-aligned block came from _aligned_malloc, so it is freed with the
     matching _aligned_free rather than free. */
  if (alignment > alignof(max_align_t)) {
    _aligned_free(pointer);
    return;
  }
#endif
  unused(alignment);
  std::free(pointer);
}

inline constexpr Allocator::VTable HEAP_VTABLE{heap_alloc, heap_resize,
                                               heap_free};

} /* namespace allocators */

/* Make a bump allocator bound to an arena. */
inline fn bump_allocator(BumpArena &arena) wontthrow -> Allocator
{
  return Allocator{&arena, &allocators::BUMP_VTABLE};
}

/* Make a heap allocator over the C allocator. */
inline fn heap_allocator() wontthrow -> Allocator
{
  return Allocator{nullptr, &allocators::HEAP_VTABLE};
}

} /* namespace shit */
