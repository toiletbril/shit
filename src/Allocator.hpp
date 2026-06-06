#pragma once

#include "Arena.hpp"
#include "Common.hpp"

#include <cstdlib>
#include <cstring>

namespace shit {

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

  void *raw_alloc(usize length, usize alignment) const
  {
    return vtable->alloc(context, length, alignment);
  }
  bool raw_resize(void *pointer, usize old_length, usize new_length,
                  usize alignment) const
  {
    return vtable->resize(context, pointer, old_length, new_length, alignment);
  }
  void raw_free(void *pointer, usize length, usize alignment) const
  {
    vtable->free(context, pointer, length, alignment);
  }

  template <class T>
  T *alloc_array(usize count) const
  {
    return static_cast<T *>(raw_alloc(count * sizeof(T), alignof(T)));
  }
  template <class T>
  void free_array(T *pointer, usize count) const
  {
    raw_free(pointer, count * sizeof(T), alignof(T));
  }
};

namespace allocators {

/* The bump adapter over a BumpArena. A free is a no-op and a resize never grows
   in place, since the arena reclaims everything at once on reset. The whole
   lifetime of its allocations is the lifetime of the arena. */
inline void *bump_alloc(void *context, usize length, usize alignment)
{
  return static_cast<BumpArena *>(context)->allocate(length, alignment);
}
inline bool bump_resize(void *context, void *pointer, usize old_length,
                        usize new_length, usize alignment)
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
inline void bump_free(void *context, void *pointer, usize length,
                      usize alignment)
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
inline void *heap_alloc(void *context, usize length, usize alignment)
{
  unused(context);
  unused(alignment);
  return std::malloc(length);
}
inline bool heap_resize(void *context, void *pointer, usize old_length,
                        usize new_length, usize alignment)
{
  unused(context);
  unused(pointer);
  unused(old_length);
  unused(new_length);
  unused(alignment);
  return false;
}
inline void heap_free(void *context, void *pointer, usize length,
                      usize alignment)
{
  unused(context);
  unused(length);
  unused(alignment);
  std::free(pointer);
}

inline constexpr Allocator::VTable HEAP_VTABLE{heap_alloc, heap_resize,
                                               heap_free};

} /* namespace allocators */

/* Make a bump allocator bound to an arena. */
inline Allocator bump_allocator(BumpArena &arena)
{
  return Allocator{&arena, &allocators::BUMP_VTABLE};
}

/* Make a heap allocator over the C allocator. */
inline Allocator heap_allocator()
{
  return Allocator{nullptr, &allocators::HEAP_VTABLE};
}

} /* namespace shit */
