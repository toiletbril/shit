#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "PackedStringKey.hpp"
#include "String.hpp"
#include "StringView.hpp"

#include <new>
#include <utility>

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
struct HashMap
{
  explicit HashMap(Allocator allocator) : m_allocator(allocator) {}

  HashMap(const HashMap &other) : m_allocator(other.m_allocator)
  {
    rehash(other.m_capacity == 0 ? 16 : other.m_capacity);
    for (usize i = 0; i < other.m_capacity; i++) {
      if (other.m_slots[i].state == Slot::Occupied)
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
  HashMap &
  operator=(HashMap &&other) noexcept
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
  HashMap &
  operator=(const HashMap &other)
  {
    if (this != &other) {
      HashMap copy{other};
      *this = std::move(copy);
    }
    return *this;
  }

  ~HashMap() { destroy_all(); }

  [[nodiscard]] usize
  size() const
  {
    return m_count;
  }

  /* The value for the key, or nullptr when absent. The pointer is stable until
     the next set that grows the table. */
  [[nodiscard]] const Value *
  find(StringView key) const
  {
    if (m_capacity == 0) return nullptr;
    PackedStringKey wanted = PackedStringKey::from_view(key);
    usize mask = m_capacity - 1;
    usize i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      const Slot &slot = m_slots[i];
      if (slot.state == Slot::Empty) return nullptr;
      /* The packed compare rejects a mismatch in two words before the byte
         compare runs, and the byte compare confirms a key past sixteen bytes.
       */
      if (slot.state == Slot::Occupied && slot.packed == wanted &&
          slot.key == key)
        return &slot.value;
      i = (i + 1) & mask;
    }
    return nullptr;
  }

  /* Store a value the table owns by move. */
  void
  set(StringView key, Value value)
  {
    set_value(key, std::move(value));
  }

  /* The value for a key, inserting the supplied default when the key is absent,
     then returning a mutable reference. The caller passes the default already
     built with the right allocator. The reference is valid until the next set
     that grows the table. */
  Value &
  get_or_create(StringView key, Value default_value)
  {
    if (const Value *existing = find(key))
      return *const_cast<Value *>(existing);
    set_value(key, std::move(default_value));
    return *const_cast<Value *>(find(key));
  }

  /* Store a String value built from a view, the form the variable store and the
     traps use. Only instantiated when Value is String. */
  void
  set(StringView key, StringView value)
  {
    set_value(key, String{m_allocator, value});
  }

  void
  erase(StringView key)
  {
    if (m_capacity == 0) return;
    usize mask = m_capacity - 1;
    usize i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      Slot &slot = m_slots[i];
      if (slot.state == Slot::Empty) return;
      if (slot.state == Slot::Occupied && slot.key == key) {
        /* Free the stored key and value but keep the slot objects alive, so a
           later place into this tombstone assigns into a live object rather
           than one whose lifetime already ended. */
        slot.key = String{m_allocator};
        slot.value = Value{};
        slot.state = Slot::Tombstone;
        m_count--;
        m_tombstones++;
        return;
      }
      i = (i + 1) & mask;
    }
  }

  /* Visit each key and value in unspecified order. */
  template <class Fn>
  void
  for_each(Fn fn) const
  {
    for (usize i = 0; i < m_capacity; i++) {
      if (m_slots[i].state == Slot::Occupied)
        fn(m_slots[i].key.view(), m_slots[i].value);
    }
  }

  void
  clear()
  {
    destroy_all();
  }

private:
  struct Slot
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

  void
  set_value(StringView key, Value value)
  {
    /* Tombstones count toward the load, so the table rehashes before a probe
       chain fills with deleted slots. That keeps an Empty slot reachable on
       every chain and an insert is never dropped. */
    if (m_count + m_tombstones + 1 > (m_capacity >> 1) + (m_capacity >> 2))
      rehash(m_capacity == 0 ? 16 : m_capacity * 2);

    PackedStringKey wanted = PackedStringKey::from_view(key);
    usize mask = m_capacity - 1;
    usize i = hash_bytes(key) & mask;
    usize first_tombstone = m_capacity;
    for (usize probe = 0; probe < m_capacity; probe++) {
      Slot &slot = m_slots[i];
      if (slot.state == Slot::Occupied && slot.packed == wanted &&
          slot.key == key)
      {
        slot.value = std::move(value);
        return;
      }
      if (slot.state == Slot::Empty) {
        usize target = first_tombstone != m_capacity ? first_tombstone : i;
        if (first_tombstone != m_capacity) m_tombstones--;
        place(target, key, std::move(value));
        return;
      }
      if (slot.state == Slot::Tombstone && first_tombstone == m_capacity)
        first_tombstone = i;
      i = (i + 1) & mask;
    }

    /* The chain held no Empty slot. The tombstone-aware load above prevents
       this, but reuse a found tombstone rather than lose the insertion. */
    if (first_tombstone != m_capacity) {
      m_tombstones--;
      place(first_tombstone, key, std::move(value));
    }
  }

  void
  place(usize index, StringView key, Value value)
  {
    Slot &slot = m_slots[index];
    slot.key = String{m_allocator, key};
    slot.packed = PackedStringKey::from_view(key);
    slot.value = std::move(value);
    slot.state = Slot::Occupied;
    m_count++;
  }

  void
  rehash(usize new_capacity)
  {
    Slot *old_slots = m_slots;
    usize old_capacity = m_capacity;

    m_slots = m_allocator.alloc_array<Slot>(new_capacity);
    for (usize i = 0; i < new_capacity; i++)
      new (&m_slots[i]) Slot{};
    m_capacity = new_capacity;
    m_count = 0;
    m_tombstones = 0;

    for (usize i = 0; i < old_capacity; i++) {
      if (old_slots[i].state == Slot::Occupied)
        set_value(old_slots[i].key.view(), std::move(old_slots[i].value));
      old_slots[i].~Slot();
    }
    if (old_slots != nullptr) m_allocator.free_array(old_slots, old_capacity);
  }

  void
  destroy_all()
  {
    for (usize i = 0; i < m_capacity; i++)
      m_slots[i].~Slot();
    if (m_slots != nullptr) m_allocator.free_array(m_slots, m_capacity);
    m_slots = nullptr;
    m_capacity = 0;
    m_count = 0;
    m_tombstones = 0;
  }

  Allocator m_allocator;
  Slot *m_slots{nullptr};
  usize m_capacity{0};
  usize m_count{0};
  usize m_tombstones{0};
};

} /* namespace shit */
