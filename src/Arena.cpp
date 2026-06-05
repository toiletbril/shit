#include "Arena.hpp"

#include "Allocator.hpp"
#include "Containers.hpp"

#include <cstdlib>

namespace shit {

BumpArena *AST_ARENA = nullptr;
BumpArena *FUNCTION_ARENA = nullptr;

bool is_arena_pointer(const void *pointer)
{
  return (AST_ARENA != nullptr && AST_ARENA->owns(pointer)) ||
         (FUNCTION_ARENA != nullptr && FUNCTION_ARENA->owns(pointer));
}

BumpArena::BumpArena() = default;

BumpArena::~BumpArena()
{
  for (Block &block : m_blocks)
    std::free(block.base);
}

void BumpArena::add_block(usize minimum_size)
{
  usize size = DEFAULT_BLOCK_SIZE;
  if (minimum_size > size) size = minimum_size;

  u8 *base = static_cast<u8 *>(std::malloc(size));
  if (base == nullptr) throw std::bad_alloc{};

  m_blocks.push_back(Block{base, size, 0});
}

void *BumpArena::allocate(usize size, usize alignment)
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

bool BumpArena::owns(const void *pointer) const
{
  const u8 *p = static_cast<const u8 *>(pointer);
  for (const Block &block : m_blocks) {
    if (p >= block.base && p < block.base + block.size) return true;
  }
  return false;
}

BumpArena::Mark BumpArena::mark() const
{
  if (m_blocks.empty()) return Mark{0, 0};
  return Mark{m_blocks.size(), m_blocks.back().used};
}

void BumpArena::release(Mark saved)
{
  /* Reset the bump pointer to the marked position, keeping the blocks so a loop
     body reuses the same storage each turn instead of asking the system again.
     The blocks past the mark stay allocated but become free space. */
  for (usize i = saved.block_count; i < m_blocks.size(); i++)
    m_blocks[i].used = 0;
  if (saved.block_count > 0)
    m_blocks[saved.block_count - 1].used = saved.used_in_last;
}

void BumpArena::reset()
{
  /* Keep the first block and drop the rest, so a typical command reuses one
     block without asking the system for memory again. */
  for (usize i = 1; i < m_blocks.size(); i++)
    std::free(m_blocks[i].base);
  if (m_blocks.size() > 1) m_blocks.resize(1);
  if (!m_blocks.empty()) m_blocks.front().used = 0;
}

} /* namespace shit */
