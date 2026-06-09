#pragma once

#include "ArrayList.hpp"
#include "Common.hpp"

#include <type_traits>

namespace shit {

/* A bump arena hands out node storage from large blocks and frees every block
   at once on reset. The AST and its tokens live here for one command, so their
   many small allocations collapse into a handful of block allocations. An
   object with a non-trivial destructor, such as a node that owns a heap String,
   has its destructor registered at create and run on reset or release, since
   the block reclaim alone would leak those owned members. */
class BumpArena
{
public:
  BumpArena();
  ~BumpArena();

  BumpArena(const BumpArena &) = delete;
  BumpArena &operator=(const BumpArena &) = delete;

  fn allocate(usize size, usize alignment) throws -> void *;
  fn owns(const void *pointer) const wontthrow -> bool;
  fn reset() wontthrow -> void;

  /* Counts how many times the arena has been reset. A cache that holds a
     pointer into this arena stores the generation it was filled in, so a hit
     after a reset, which reclaimed that storage, is recognised as stale and
     refilled. */
  pure fn reset_generation() const wontthrow -> usize
  {
    return m_reset_generation;
  }

  /* Sum the live bump bytes across every block. The stats path reads it to
     report how much the current command's tree occupies. */
  fn bytes_used() const wontthrow -> usize;

  /* A saved bump position, so a scope can reclaim everything it allocated above
     the mark while leaving earlier allocations alone. The marks nest, so a
     command substitution inside an expansion releases only its own region. */
  struct Mark
  {
    usize block_count;
    usize used_in_last;
    usize destructor_count;
  };
  fn mark() const wontthrow -> Mark;
  fn release(Mark saved) wontthrow -> void;

  template <class T, class... Args>
  flatten fn create(Args &&...args) throws -> T *
  {
    void *const storage = allocate(sizeof(T), alignof(T));
    T *const object = new (storage) T(std::forward<Args>(args)...);
    /* A trivially destructible object needs no teardown, so the registration is
       skipped and only the genuinely-owning ones cost a slot. */
    if constexpr (!std::is_trivially_destructible_v<T>)
      m_destructors.push(pending_destructor{
          object, [](void *p) { static_cast<T *>(p)->~T(); }});
    return object;
  }

private:
  struct block
  {
    u8 *base;
    usize size;
    usize used;
  };

  /* One registered teardown, the object and a thunk that calls its destructor,
     run when the storage above it is reclaimed. */
  struct pending_destructor
  {
    void *object;
    void (*run)(void *);
  };

  static constexpr usize DEFAULT_BLOCK_SIZE = 64 * 1024;

  ArrayList<block> m_blocks{heap_allocator()};
  ArrayList<pending_destructor> m_destructors{heap_allocator()};
  usize m_reset_generation{0};

  fn add_block(usize minimum_size) throws -> void;
  /* Run and drop every registered destructor from the index down to first, in
     reverse of registration so an object tears down before the one it followed.
   */
  fn run_destructors_down_to(usize first) wontthrow -> void;
};

/* The arena that the lexer and parser allocate nodes from while a command is
   being built. The operator delete on a node consults it to tell arena storage
   apart from an ordinary heap node. It is null outside a parse. */
extern BumpArena *AST_ARENA;

/* The arena that holds function bodies. A function body outlives the command
   that defined it, so it is parsed here instead of the per-command arena, which
   resets after every command. It is never reset during a run. */
extern BumpArena *FUNCTION_ARENA;

/* True when the pointer belongs to either arena, so a node delete knows to
   leave the storage to the arena. */
fn is_arena_pointer(const void *pointer) wontthrow -> bool;

} /* namespace shit */
