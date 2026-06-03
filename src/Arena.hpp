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

  void *allocate(usize size, usize alignment);
  bool owns(const void *pointer) const;
  void reset();

  template <class T, class... Args>
  T *
  create(Args &&...args)
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

  void add_block(usize minimum_size);
};

/* The arena that the lexer and parser allocate nodes from while a command is
   being built. The operator delete on a node consults it to tell arena storage
   apart from an ordinary heap node, since a few arithmetic nodes are still
   built with make_unique. It is null outside a parse. */
extern BumpArena *g_ast_arena;

} /* namespace shit */
