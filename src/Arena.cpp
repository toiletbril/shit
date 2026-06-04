#include "Arena.hpp"

#include "Allocator.hpp"
#include "Containers.hpp"

#include <cstdlib>

namespace shit {

BumpArena *g_ast_arena = nullptr;
BumpArena *g_function_arena = nullptr;

bool
is_arena_pointer(const void *pointer)
{
  return (g_ast_arena != nullptr && g_ast_arena->owns(pointer)) ||
         (g_function_arena != nullptr && g_function_arena->owns(pointer));
}

BumpArena::BumpArena() = default;

BumpArena::~BumpArena()
{
  for (Block &block : m_blocks)
    std::free(block.base);
}

void
BumpArena::add_block(usize minimum_size)
{
  usize size = DEFAULT_BLOCK_SIZE;
  if (minimum_size > size) size = minimum_size;

  u8 *base = static_cast<u8 *>(std::malloc(size));
  if (base == nullptr) throw std::bad_alloc{};

  m_blocks.push_back(Block{base, size, 0});
}

void *
BumpArena::allocate(usize size, usize alignment)
{
  for (;;) {
    if (!m_blocks.empty()) {
      Block &block = m_blocks.back();
      usize aligned = (block.used + (alignment - 1)) & ~(alignment - 1);
      if (aligned + size <= block.size) {
        void *pointer = block.base + aligned;
        block.used = aligned + size;
        return pointer;
      }
    }
    add_block(size + alignment);
  }
}

bool
BumpArena::owns(const void *pointer) const
{
  const u8 *p = static_cast<const u8 *>(pointer);
  for (const Block &block : m_blocks) {
    if (p >= block.base && p < block.base + block.size) return true;
  }
  return false;
}

void
BumpArena::reset()
{
  /* Keep the first block and drop the rest, so a typical command reuses one
     block without asking the system for memory again. */
  for (usize i = 1; i < m_blocks.size(); i++)
    std::free(m_blocks[i].base);
  if (m_blocks.size() > 1) m_blocks.resize(1);
  if (!m_blocks.empty()) m_blocks.front().used = 0;
}

} /* namespace shit */
