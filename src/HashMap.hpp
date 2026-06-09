#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "PackedStringKey.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

/* An open-addressing hash table from a string key to a Value over an explicit
   allocator, the std::unordered_map replacement. The value defaults to String
   for the variable store and the traps, and takes a pointer for the function
   table. Each slot caches a PackedStringKey of the key's first sixteen bytes,
   so a probe rejects a mismatch with a two-word compare before the full byte
   compare. Linear probing, power-of-two capacity, grows past a load of three
   quarters, and counts tombstones toward the load so an insert is never
   dropped. */
template <class Value = String>
class HashMap
{
public:
  explicit HashMap(Allocator allocator) : m_allocator(allocator) {}

  HashMap(const HashMap &other) : m_allocator(other.m_allocator)
  {
    rehash(other.m_capacity == 0 ? 16 : other.m_capacity);
    for (usize i = 0; i < other.m_capacity; i++) {
      if (other.m_slots[i].state == slot::Occupied)
        set_value(other.m_slots[i].key.view(), Value{other.m_slots[i].value});
    }
  }
  HashMap(HashMap &&other) noexcept
      : m_allocator(other.m_allocator), m_slots(other.m_slots),
        m_capacity(other.m_capacity), m_count(other.m_count),
        m_tombstones(other.m_tombstones)
  {
    other.m_slots = nullptr;
    other.m_capacity = 0;
    other.m_count = 0;
    other.m_tombstones = 0;
  }
  fn operator=(HashMap &&other) wontthrow->HashMap &
  {
    if (this != &other) {
      destroy_all();
      m_allocator = other.m_allocator;
      m_slots = other.m_slots;
      m_capacity = other.m_capacity;
      m_count = other.m_count;
      m_tombstones = other.m_tombstones;
      other.m_slots = nullptr;
      other.m_capacity = 0;
      other.m_count = 0;
      other.m_tombstones = 0;
    }
    return *this;
  }
  fn operator=(const HashMap &other) throws->HashMap &
  {
    if (this != &other) {
      HashMap copy{other};
      *this = steal(copy);
    }
    return *this;
  }

  ~HashMap() { destroy_all(); }

  mustuse pure fn count() const wontthrow -> usize { return m_count; }

  /* The value for the key, or nullptr when absent. The pointer is stable until
     the next set that grows the table. */
  mustuse pure fn find(StringView key) const wontthrow -> const Value *
  {
    if (m_capacity == 0) return nullptr;
    let const wanted = PackedStringKey::from_view(key);
    let const mask = m_capacity - 1;
    let i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      let const &slot = m_slots[i];
      if (slot.state == slot::Empty) return nullptr;
      /* The packed compare rejects a mismatch in two words before the byte
         compare runs, and the byte compare confirms a key past sixteen bytes.
       */
      if (slot.state == slot::Occupied && slot.packed == wanted &&
          slot.key == key)
        return &slot.value;
      i = (i + 1) & mask;
    }
    return nullptr;
  }

  /* The mutable value for the key, or nullptr when absent, so a caller can edit
     a stored value in place without a copy-out then set. The pointer is stable
     until the next set that grows the table. */
  mustuse fn find(StringView key) wontthrow -> Value *
  {
    return const_cast<Value *>(static_cast<const HashMap *>(this)->find(key));
  }

  /* Store a value the table owns by move. */
  fn set(StringView key, Value value) throws -> void { set_value(key, steal(value)); }

  /* The value for a key, inserting the supplied default when the key is absent,
     then returning a mutable reference. The caller passes the default already
     built with the right allocator. The reference is valid until the next set
     that grows the table. */
  fn get_or_create(StringView key, Value default_value) throws -> Value &
  {
    if (const Value *existing = find(key))
      return *const_cast<Value *>(existing);
    set_value(key, steal(default_value));
    return *const_cast<Value *>(find(key));
  }

  /* Store a String value built from a view, the form the variable store and the
     traps use. Only instantiated when Value is String. An existing slot reuses
     its String buffer rather than allocating a fresh String and freeing the old
     one, so a tight reassignment loop such as a counter pays no per-turn
     allocation once the buffer is large enough.

     The value must not view the existing slot's own buffer, since clear then
     append would read bytes the clear already truncated. Every caller builds
     the value in a fresh String or a stack buffer first, so a self-assignment
     such as x=$x still passes an independent view. */
  fn set(StringView key, StringView value) throws -> void
  {
    if (Value *existing = find(key)) {
      /* A buffer that once held a large value and now takes a far smaller one
         is rebuilt at the right size rather than reused, so a name that held a
         big value does not pin that memory for the session. A small or
         similar-sized value reuses the buffer, which keeps a counter loop
         allocation free. */
      let const buffer_is_wasteful =
          existing->count() > 256 && value.length < existing->count() / 2;
      if (!buffer_is_wasteful) {
        existing->clear();
        existing->append(value);
        return;
      }
    }
    set_value(key, String{m_allocator, value});
  }

  fn erase(StringView key) throws -> void
  {
    if (m_capacity == 0) return;
    let const mask = m_capacity - 1;
    let i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      let &slot = m_slots[i];
      if (slot.state == slot::Empty) return;
      if (slot.state == slot::Occupied && slot.key == key) {
        /* Free the stored key and value but keep the slot objects alive, so a
           later place into this tombstone assigns into a live object rather
           than one whose lifetime already ended. */
        slot.key = String{m_allocator};
        slot.value = Value{};
        slot.state = slot::Tombstone;
        m_count--;
        m_tombstones++;
        return;
      }
      i = (i + 1) & mask;
    }
  }

  /* Visit each key and value in unspecified order. */
  template <class Fn>
  fn for_each(Fn callback) const throws -> void
  {
    for (usize i = 0; i < m_capacity; i++) {
      if (m_slots[i].state == slot::Occupied)
        callback(m_slots[i].key.view(), m_slots[i].value);
    }
  }

  fn clear() wontthrow -> void { destroy_all(); }

private:
  struct slot
  {
    enum State : u8
    {
      Empty,
      Occupied,
      Tombstone,
    };
    State state{Empty};
    PackedStringKey packed{};
    String key{};
    Value value{};
  };

  fn set_value(StringView key, Value value) throws -> void
  {
    /* Tombstones count toward the load, so the table rehashes before a probe
       chain fills with deleted slots. That keeps an Empty slot reachable on
       every chain and an insert is never dropped. */
    if (m_count + m_tombstones + 1 > (m_capacity >> 1) + (m_capacity >> 2))
      rehash(m_capacity == 0 ? 16 : m_capacity * 2);

    let const wanted = PackedStringKey::from_view(key);
    let const mask = m_capacity - 1;
    let i = hash_bytes(key) & mask;
    let first_tombstone = m_capacity;
    for (usize probe = 0; probe < m_capacity; probe++) {
      let &slot = m_slots[i];
      if (slot.state == slot::Occupied && slot.packed == wanted &&
          slot.key == key)
      {
        slot.value = steal(value);
        return;
      }
      if (slot.state == slot::Empty) {
        let const target = first_tombstone != m_capacity ? first_tombstone : i;
        if (first_tombstone != m_capacity) m_tombstones--;
        place(target, key, wanted, steal(value));
        return;
      }
      if (slot.state == slot::Tombstone && first_tombstone == m_capacity)
        first_tombstone = i;
      i = (i + 1) & mask;
    }

    /* The chain held no Empty slot. The tombstone-aware load above prevents
       this, but reuse a found tombstone rather than lose the insertion. */
    if (first_tombstone != m_capacity) {
      m_tombstones--;
      place(first_tombstone, key, wanted, steal(value));
    }
  }

  /* The caller already computed the packed key for its probe, so it is passed
     in rather than recomputed from the key bytes here. */
  fn place(usize index, StringView key, const PackedStringKey &packed,
           Value value) throws -> void
  {
    let &slot = m_slots[index];
    slot.key = String{m_allocator, key};
    slot.packed = packed;
    slot.value = steal(value);
    slot.state = slot::Occupied;
    m_count++;
  }

  fn rehash(usize new_capacity) throws -> void
  {
    let old_slots = m_slots;
    let const old_capacity = m_capacity;

    m_slots = m_allocator.alloc_array<slot>(new_capacity);
    for (usize i = 0; i < new_capacity; i++)
      new (&m_slots[i]) slot{};
    m_capacity = new_capacity;
    m_count = 0;
    m_tombstones = 0;

    for (usize i = 0; i < old_capacity; i++) {
      if (old_slots[i].state == slot::Occupied)
        set_value(old_slots[i].key.view(), steal(old_slots[i].value));
      old_slots[i].~slot();
    }
    if (old_slots != nullptr) m_allocator.free_array(old_slots, old_capacity);
  }

  fn destroy_all() wontthrow -> void
  {
    for (usize i = 0; i < m_capacity; i++)
      m_slots[i].~slot();
    if (m_slots != nullptr) m_allocator.free_array(m_slots, m_capacity);
    m_slots = nullptr;
    m_capacity = 0;
    m_count = 0;
    m_tombstones = 0;
  }

  Allocator m_allocator;
  slot *m_slots{nullptr};
  usize m_capacity{0};
  usize m_count{0};
  usize m_tombstones{0};
};

} /* namespace shit */
