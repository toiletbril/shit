#include "Arena.hpp"

#include "Allocator.hpp"
#include "Containers.hpp"

#include <cstdlib>

namespace shit {

BumpArena *AST_ARENA = nullptr;
BumpArena *FUNCTION_ARENA = nullptr;

fn is_arena_pointer(const void *pointer) wontthrow -> bool
{
  return (AST_ARENA != nullptr && AST_ARENA->owns(pointer)) ||
         (FUNCTION_ARENA != nullptr && FUNCTION_ARENA->owns(pointer));
}

hot fn bump_arena_allocate(BumpArena *arena, usize length, usize alignment)
    throws -> void *
{
  return arena->allocate(length, alignment);
}

BumpArena::BumpArena() = default;

BumpArena::~BumpArena()
{
  run_destructors_down_to(0);
  for (block &block : m_blocks)
    std::free(block.base);
}

cold fn BumpArena::run_destructors_down_to(usize first) wontthrow -> void
{
  while (m_destructors.count() > first) {
    let const pending = m_destructors.back();
    m_destructors.pop_back();
    pending.run(pending.object);
  }
}

cold fn BumpArena::add_block(usize minimum_size) throws -> void
{
  let size = DEFAULT_BLOCK_SIZE;
  if (minimum_size > size) size = minimum_size;

  let const base = static_cast<u8 *>(std::malloc(size));
  if (base == nullptr) throw std::bad_alloc{};

  ASSERT(size >= minimum_size, "fresh block must fit the requested allocation");

  m_blocks.push(block{base, size, 0});
}

hot fn BumpArena::allocate(usize size, usize alignment) throws -> void *
{
  for (;;) {
    if (!m_blocks.is_empty()) {
      let &block = m_blocks.back();
      let const aligned = (block.used + (alignment - 1)) & ~(alignment - 1);

      if (aligned + size <= block.size) [[likely]] {
        ASSERT(block.base != nullptr);

        let const pointer = block.base + aligned;
        block.used = aligned + size;

        return pointer;
      }
    }

    add_block(size + alignment);
  }
}

hot fn BumpArena::owns(const void *pointer) const wontthrow -> bool
{
  let const p = static_cast<const u8 *>(pointer);
  for (const block &block : m_blocks) {
    if (p >= block.base && p < block.base + block.size) return true;
  }
  return false;
}

fn BumpArena::bytes_used() const wontthrow -> usize
{
  usize total = 0;
  for (const block &block : m_blocks)
    total += block.used;
  return total;
}

fn BumpArena::mark() const wontthrow -> BumpArena::Mark
{
  if (m_blocks.is_empty()) return Mark{0, 0, m_destructors.count()};
  return Mark{m_blocks.count(), m_blocks.back().used, m_destructors.count()};
}

fn BumpArena::release(Mark saved) wontthrow -> void
{
  ASSERT(saved.block_count <= m_blocks.count(),
         "mark cannot name more blocks than the arena holds");

  run_destructors_down_to(saved.destructor_count);

  /* Reset the bump pointer to the marked position, keeping the blocks so a loop
     body reuses the same storage each turn instead of asking the system again.
     The blocks past the mark stay allocated but become free space. */
  for (usize i = saved.block_count; i < m_blocks.count(); i++)
    m_blocks[i].used = 0;

  if (saved.block_count > 0) {
    ASSERT(saved.used_in_last <= m_blocks[saved.block_count - 1].size);
    m_blocks[saved.block_count - 1].used = saved.used_in_last;
  }
}

cold fn BumpArena::reset() wontthrow -> void
{
  run_destructors_down_to(0);

  /* Every reset reclaims the storage a cached pointer may hold, so bump the
     generation to invalidate any cache that recorded an earlier one. */
  m_reset_generation++;

  /* Keep the first block and drop the rest, so a typical command reuses one
     block without asking the system for memory again. */
  for (usize i = 1; i < m_blocks.count(); i++)
    std::free(m_blocks[i].base);
  while (m_blocks.count() > 1)
    m_blocks.pop_back();
  if (!m_blocks.is_empty()) m_blocks.front().used = 0;
}

} /* namespace shit */
