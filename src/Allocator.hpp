#pragma once

#include "Common.hpp"

#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace shit {

/* Forward declared, so the allocator header does not include the arena header,
   which would close the ArrayList -> Allocator -> Arena include cycle. */
class BumpArena;
fn bump_arena_allocate(BumpArena *arena, usize length, usize alignment) throws
    -> opaque *;

/* An allocator value. It carries a context and a table of operations, so a data
   structure is handed the allocator it must use and frees through the same one.
   It is sixteen bytes and passed by value. */
class Allocator
{
public:
  struct VTable
  {
    opaque *(*alloc)(opaque *context, usize length, usize alignment);
    /* Grow or shrink in place. Returns false when the block cannot change size
       without moving, so the caller allocates and copies. */
    bool (*resize)(opaque *context, opaque *pointer, usize old_length,
                   usize new_length, usize alignment);
    void (*free)(opaque *context, opaque *pointer, usize length, usize alignment);
  };

  opaque *context;
  const VTable *vtable;

  hot flatten fn raw_alloc(usize length, usize alignment) const throws -> opaque *
  {
    return vtable->alloc(context, length, alignment);
  }
  fn raw_resize(opaque *pointer, usize old_length, usize new_length,
                usize alignment) const wontthrow -> bool
  {
    return vtable->resize(context, pointer, old_length, new_length, alignment);
  }
  flatten fn raw_free(opaque *pointer, usize length,
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
   in place, since the arena reclaims everything at once on reset. */
hot inline fn bump_alloc(opaque *context, usize length, usize alignment) throws
    -> opaque *
{
  return bump_arena_allocate(static_cast<BumpArena *>(context), length,
                             alignment);
}
inline fn bump_resize(opaque *context, opaque *pointer, usize old_length,
                      usize new_length, usize alignment) wontthrow -> bool
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
inline fn bump_free(opaque *context, opaque *pointer, usize length,
                    usize alignment) wontthrow -> void
{
  unused(context);
  unused(pointer);
  unused(length);
  unused(alignment);
}

inline constexpr Allocator::VTable BUMP_VTABLE{bump_alloc, bump_resize,
                                               bump_free};

/* A size-classed cache over the C allocator. musl returns a freed page group to
   the kernel at once, so a tight allocate then free of the same size churns
   mmap and munmap once per turn, which dominates the bench on Alpine where
   glibc would have cached the page. A freed block parks on a per-class free
   list and is handed back on the next request of that class, so the kernel sees
   a steady working set. The cache is bounded per class so a burst does not pin
   memory. The pool is single threaded, since the evaluator never shares an
   allocator across threads, and it is audited in docs/globals-audit.md. */
struct heap_pool
{
  static constexpr usize MIN_CLASS_SHIFT =
      4; /* the smallest class is 16 bytes */
  static constexpr usize MAX_CLASS_SHIFT =
      16; /* the largest pooled block is 64 KiB */
  static constexpr usize CLASS_COUNT = MAX_CLASS_SHIFT - MIN_CLASS_SHIFT + 1;
  static constexpr usize MAX_BLOCKS_PER_CLASS = 512;

  struct node
  {
    node *next;
  };

  node *bins[CLASS_COUNT] = {};
  u32 counts[CLASS_COUNT] = {};

  /* The class shift is the ceiling of the base two logarithm of the length, so
     a block always covers the request, never below the smallest class. A length
     above the largest class yields a shift past MAX_CLASS_SHIFT, which take and
     give read as the uncached path. */
  hot static fn class_shift_for(usize length) wontthrow -> usize
  {
    let const size = length <= (usize{1} << MIN_CLASS_SHIFT)
                         ? (usize{1} << MIN_CLASS_SHIFT)
                         : length;
    let shift = static_cast<usize>(64 - __builtin_clzll(size - 1));
    if (shift < MIN_CLASS_SHIFT) shift = MIN_CLASS_SHIFT;
    return shift;
  }

  hot fn take(usize length) wontthrow -> opaque *
  {
    let const shift = class_shift_for(length);
    if (shift > MAX_CLASS_SHIFT) return std::malloc(length);

    let const class_index = shift - MIN_CLASS_SHIFT;
    if (bins[class_index] != nullptr) {
      let const reused = bins[class_index];
      bins[class_index] = reused->next;
      counts[class_index]--;
      return reused;
    }

    return std::malloc(usize{1} << shift);
  }

  hot fn give(opaque *pointer, usize length) wontthrow -> void
  {
    if (pointer == nullptr) return;

    let const shift = class_shift_for(length);
    if (shift > MAX_CLASS_SHIFT) {
      std::free(pointer);
      return;
    }

    let const class_index = shift - MIN_CLASS_SHIFT;
    if (counts[class_index] >= MAX_BLOCKS_PER_CLASS) {
      std::free(pointer);
      return;
    }

    let const recycled = static_cast<node *>(pointer);
    recycled->next = bins[class_index];
    bins[class_index] = recycled;
    counts[class_index]++;
  }
};

/* The single process-wide cache, one instance across every translation unit
   through the inline function local static. The pool is trivially destructible,
   so it registers no exit destructor and its storage stays valid through
   process teardown. A heap free from a file-scope cache destructor at process
   exit then reaches live storage whatever the static destruction order names.
 */
hot inline fn heap_pool_instance() wontthrow -> heap_pool &
{
  static heap_pool pool;
  return pool;
}

/* The heap adapter over the C allocator. It frees on demand, so it backs the
   long-lived mutable data the bump model would leak. */
hot inline fn heap_alloc(opaque *context, usize length, usize alignment) wontthrow
    -> opaque *
{
  unused(context);
  /* malloc already meets every alignment up to alignof(max_align_t), so the
     common request stays on the pooled path. An over-aligned type takes
     aligned_alloc, whose result std::free accepts the same as a malloc result.
     aligned_alloc wants a size that is a multiple of the alignment, so the
     length is rounded up. The over-aligned path is rare and stays uncached. */
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
  return heap_pool_instance().take(length);
}
inline fn heap_resize(opaque *context, opaque *pointer, usize old_length,
                      usize new_length, usize alignment) wontthrow -> bool
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
hot inline fn heap_free(opaque *context, opaque *pointer, usize length,
                        usize alignment) wontthrow -> void
{
  unused(context);
  if (alignment > alignof(max_align_t)) {
#if defined(_WIN32)
    /* An over-aligned block came from _aligned_malloc, so it is freed with the
       matching _aligned_free rather than free. */
    _aligned_free(pointer);
#else
    /* An over-aligned block came from aligned_alloc, which std::free accepts.
       It skips the pool, so a pooled block always belongs to one size class. */
    std::free(pointer);
#endif
    return;
  }
  heap_pool_instance().give(pointer, length);
}

inline constexpr Allocator::VTable HEAP_VTABLE{heap_alloc, heap_resize,
                                               heap_free};

} // namespace allocators

inline fn bump_allocator(BumpArena &arena) wontthrow -> Allocator
{
  return Allocator{&arena, &allocators::BUMP_VTABLE};
}

inline fn heap_allocator() wontthrow -> Allocator
{
  return Allocator{nullptr, &allocators::HEAP_VTABLE};
}

} // namespace shit
