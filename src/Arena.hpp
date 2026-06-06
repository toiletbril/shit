#pragma once

#include "Common.hpp"

#include <new>
#include <utility>
#include <vector>

namespace shit {

/* A bump arena hands out node storage from large blocks and frees every block
   at once on reset. The AST and its tokens live here for one command, so their
   many small allocations collapse into a handful of block allocations. The node
   destructors still run through the normal delete, see the operator delete on
   Expression and Token that no-ops for arena storage. */
struct BumpArena
{
  BumpArena();
  ~BumpArena();

  BumpArena(const BumpArena &) = delete;
  BumpArena &operator=(const BumpArena &) = delete;

  fn allocate(usize size, usize alignment) -> void *;
  fn owns(const void *pointer) const -> bool;
  fn reset() -> void;

  /* A saved bump position, so a scope can reclaim everything it allocated above
     the mark while leaving earlier allocations alone. The marks nest, so a
     command substitution inside an expansion releases only its own region. */
  struct Mark
  {
    usize block_count;
    usize used_in_last;
  };
  fn mark() const -> Mark;
  fn release(Mark saved) -> void;

  template <class T, class... Args>
  fn create(Args &&...args) -> T *
  {
    void *storage = allocate(sizeof(T), alignof(T));
    return new (storage) T(std::forward<Args>(args)...);
  }

private:
  struct Block
  {
    u8 *base;
    usize size;
    usize used;
  };

  static constexpr usize DEFAULT_BLOCK_SIZE = 64 * 1024;

  std::vector<Block> m_blocks{};

  fn add_block(usize minimum_size) -> void;
};

/* The arena that the lexer and parser allocate nodes from while a command is
   being built. The operator delete on a node consults it to tell arena storage
   apart from an ordinary heap node, since a few arithmetic nodes are still
   built with make_unique. It is null outside a parse. */
extern BumpArena *AST_ARENA;

/* The arena that holds function bodies. A function body outlives the command
   that defined it, so it is parsed here instead of the per-command arena, which
   resets after every command. It is never reset during a run. */
extern BumpArena *FUNCTION_ARENA;

/* True when the pointer belongs to either arena, so a node delete knows to
   leave the storage to the arena. */
fn is_arena_pointer(const void *pointer) -> bool;

} /* namespace shit */
