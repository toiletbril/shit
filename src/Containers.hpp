#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Maybe.hpp"

#include <cstring>
#include <new>
#include <utility>

namespace shit {

/* A non-owning view of bytes, the form a function takes when it does not own
   the characters. It points into a String, a literal, or a slice. */
struct StringView
{
  const char *data{nullptr};
  usize length{0};

  StringView() = default;
  StringView(const char *bytes, usize count) : data(bytes), length(count) {}
  StringView(const char *cstr)
      : data(cstr), length(cstr != nullptr ? std::strlen(cstr) : 0)
  {}

  [[nodiscard]] usize
  size() const
  {
    return length;
  }
  [[nodiscard]] bool
  empty() const
  {
    return length == 0;
  }
  [[nodiscard]] char
  operator[](usize i) const
  {
    return data[i];
  }

  [[nodiscard]] bool
  operator==(StringView other) const
  {
    return length == other.length &&
           (length == 0 || std::memcmp(data, other.data, length) == 0);
  }
  [[nodiscard]] bool
  operator!=(StringView other) const
  {
    return !(*this == other);
  }

  /* The index of the first occurrence of a byte, or nothing when it is absent.
     A Maybe keeps the absent case out of band rather than using a sentinel
     index. */
  [[nodiscard]] Maybe<usize>
  find_character(char wanted) const
  {
    for (usize i = 0; i < length; i++)
      if (data[i] == wanted) return i;
    return nothing;
  }

  /* The view from start to the end. A start past the end yields an empty view. */
  [[nodiscard]] StringView
  substring(usize start) const
  {
    if (start >= length) return StringView{data + length, 0};
    return StringView{data + start, length - start};
  }

  /* The view of count bytes from start, clamped to what remains. */
  [[nodiscard]] StringView
  substring_of_length(usize start, usize count) const
  {
    if (start >= length) return StringView{data + length, 0};
    usize remaining = length - start;
    return StringView{data + start, count < remaining ? count : remaining};
  }

  [[nodiscard]] bool
  starts_with(StringView prefix) const
  {
    if (prefix.length > length) return false;
    /* An empty prefix matches, and the guard keeps a null data pointer out of
       memcmp, which is undefined even for a zero length. */
    return prefix.length == 0 ||
           std::memcmp(data, prefix.data, prefix.length) == 0;
  }
};

/* A growable hash of a byte range, FNV-1a over the short keys a shell uses. */
inline u64
hash_bytes(StringView view)
{
  u64 hash = 14695981039346656037ull;
  for (usize i = 0; i < view.length; i++) {
    hash ^= static_cast<unsigned char>(view.data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

/* An owned, growable byte string over an explicit allocator. There is no
   small-string buffer yet, so every non-empty string is one allocation, which a
   bump arena makes cheap. The buffer keeps a trailing null so c_str is free. */
struct String
{
  /* A default String is heap-backed and empty, so it can serve as a container
     slot before its real allocator and value are assigned. */
  String() : m_allocator(heap_allocator()) {}
  explicit String(Allocator allocator) : m_allocator(allocator) {}
  String(Allocator allocator, StringView initial) : m_allocator(allocator)
  {
    append(initial);
  }

  String(const String &other) : m_allocator(other.m_allocator)
  {
    append(other.view());
  }
  String(String &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity)
  {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  String &
  operator=(const String &other)
  {
    if (this != &other) {
      clear();
      append(other.view());
    }
    return *this;
  }
  String &
  operator=(String &&other) noexcept
  {
    if (this != &other) {
      free_storage();
      m_allocator = other.m_allocator;
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;
      other.m_data = nullptr;
      other.m_length = 0;
      other.m_capacity = 0;
    }
    return *this;
  }

  ~String() { free_storage(); }

  [[nodiscard]] usize
  size() const
  {
    return m_length;
  }
  [[nodiscard]] bool
  empty() const
  {
    return m_length == 0;
  }
  [[nodiscard]] char
  operator[](usize i) const
  {
    return m_data[i];
  }
  [[nodiscard]] StringView
  view() const
  {
    return StringView{m_data, m_length};
  }
  [[nodiscard]] const char *
  c_str() const
  {
    return m_data != nullptr ? m_data : "";
  }

  void
  clear()
  {
    m_length = 0;
    if (m_data != nullptr) m_data[0] = '\0';
  }

  void
  push(char c)
  {
    reserve(m_length + 1);
    m_data[m_length++] = c;
    m_data[m_length] = '\0';
  }
  void
  append(StringView other)
  {
    if (other.length == 0) return;
    reserve(m_length + other.length);
    std::memcpy(m_data + m_length, other.data, other.length);
    m_length += other.length;
    m_data[m_length] = '\0';
  }

  void
  reserve(usize needed)
  {
    if (needed + 1 <= m_capacity) return;
    usize new_capacity = m_capacity == 0 ? 16 : m_capacity * 2;
    while (new_capacity < needed + 1)
      new_capacity *= 2;
    char *fresh = m_allocator.alloc_array<char>(new_capacity);
    if (m_length > 0) std::memcpy(fresh, m_data, m_length);
    fresh[m_length] = '\0';
    free_storage();
    m_data = fresh;
    m_capacity = new_capacity;
  }

  /* The byte buffer, always null terminated. */
  [[nodiscard]] const char *
  data() const
  {
    return c_str();
  }
  [[nodiscard]] usize
  length() const
  {
    return m_length;
  }
  [[nodiscard]] char
  back() const
  {
    return m_data[m_length - 1];
  }

  void
  append(char c)
  {
    push(c);
  }
  String &
  operator+=(StringView other)
  {
    append(other);
    return *this;
  }
  String &
  operator+=(char c)
  {
    push(c);
    return *this;
  }

  /* Search and slice forward to the view, so the owned string answers the same
     questions a std::string does without exposing its buffer. */
  [[nodiscard]] Maybe<usize>
  find_character(char wanted) const
  {
    return view().find_character(wanted);
  }
  [[nodiscard]] StringView
  substring(usize start) const
  {
    return view().substring(start);
  }
  [[nodiscard]] StringView
  substring_of_length(usize start, usize count) const
  {
    return view().substring_of_length(start, count);
  }
  [[nodiscard]] bool
  starts_with(StringView prefix) const
  {
    return view().starts_with(prefix);
  }

  [[nodiscard]] bool
  operator==(StringView other) const
  {
    return view() == other;
  }
  [[nodiscard]] bool
  operator!=(StringView other) const
  {
    return !(view() == other);
  }

  /* Byte order, so a sort matches the C locale collating order. */
  [[nodiscard]] bool
  operator<(const String &other) const
  {
    usize shared = m_length < other.m_length ? m_length : other.m_length;
    int order = shared == 0 ? 0 : std::memcmp(c_str(), other.c_str(), shared);
    if (order != 0) return order < 0;
    return m_length < other.m_length;
  }

private:
  void
  free_storage()
  {
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = nullptr;
    m_capacity = 0;
  }

  Allocator m_allocator;
  char *m_data{nullptr};
  usize m_length{0};
  usize m_capacity{0};
};

/* A growable array over an explicit allocator, the std::vector replacement. */
template <class T>
struct ArrayList
{
  /* A default list is heap-backed and empty, so it can serve as the value a
     HashMap slot holds before a real list is placed into it. */
  ArrayList() : m_allocator(heap_allocator()) {}
  explicit ArrayList(Allocator allocator) : m_allocator(allocator) {}

  ArrayList(const ArrayList &other) : m_allocator(other.m_allocator)
  {
    reserve(other.m_length);
    for (usize i = 0; i < other.m_length; i++)
      new (&m_data[i]) T(other.m_data[i]);
    m_length = other.m_length;
  }
  ArrayList(ArrayList &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity)
  {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  ArrayList &
  operator=(ArrayList &&other) noexcept
  {
    if (this != &other) {
      destroy_all();
      m_allocator = other.m_allocator;
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;
      other.m_data = nullptr;
      other.m_length = 0;
      other.m_capacity = 0;
    }
    return *this;
  }
  ArrayList &
  operator=(const ArrayList &other)
  {
    if (this != &other) {
      ArrayList copy{other};
      *this = std::move(copy);
    }
    return *this;
  }

  ~ArrayList() { destroy_all(); }

  [[nodiscard]] usize
  size() const
  {
    return m_length;
  }
  [[nodiscard]] bool
  empty() const
  {
    return m_length == 0;
  }
  [[nodiscard]] T &
  operator[](usize i)
  {
    return m_data[i];
  }
  [[nodiscard]] const T &
  operator[](usize i) const
  {
    return m_data[i];
  }
  [[nodiscard]] T *
  begin()
  {
    return m_data;
  }
  [[nodiscard]] T *
  end()
  {
    return m_data + m_length;
  }
  [[nodiscard]] const T *
  begin() const
  {
    return m_data;
  }
  [[nodiscard]] const T *
  end() const
  {
    return m_data + m_length;
  }

  void
  push(T value)
  {
    reserve(m_length + 1);
    new (&m_data[m_length]) T(std::move(value));
    m_length++;
  }

  /* Destroy the elements but keep the storage, so a reused list does not
     reallocate. */
  void
  clear()
  {
    for (usize i = 0; i < m_length; i++)
      m_data[i].~T();
    m_length = 0;
  }

  [[nodiscard]] T &
  back()
  {
    return m_data[m_length - 1];
  }
  [[nodiscard]] const T &
  back() const
  {
    return m_data[m_length - 1];
  }
  [[nodiscard]] T &
  front()
  {
    return m_data[0];
  }
  [[nodiscard]] const T &
  front() const
  {
    return m_data[0];
  }

  void
  reserve(usize needed)
  {
    if (needed <= m_capacity) return;
    usize new_capacity = m_capacity == 0 ? 8 : m_capacity * 2;
    while (new_capacity < needed)
      new_capacity *= 2;
    T *fresh = m_allocator.alloc_array<T>(new_capacity);
    for (usize i = 0; i < m_length; i++) {
      new (&fresh[i]) T(std::move(m_data[i]));
      m_data[i].~T();
    }
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = fresh;
    m_capacity = new_capacity;
  }

private:
  void
  destroy_all()
  {
    for (usize i = 0; i < m_length; i++)
      m_data[i].~T();
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = nullptr;
    m_length = 0;
    m_capacity = 0;
  }

  Allocator m_allocator;
  T *m_data{nullptr};
  usize m_length{0};
  usize m_capacity{0};
};

/* A short byte string packed into a fixed sixteen-byte block held in two machine
   words. A lookup compares two integers rather than walking bytes, which the
   compiler lowers to a couple of wide register compares, and the layout is
   contiguous so a later pass can scan many keys with one SIMD instruction. A key
   longer than sixteen bytes keeps only its first sixteen bytes here and relies on
   a full compare to disambiguate. */
struct PackedStringKey
{
  u64 low_word{0};
  u64 high_word{0};

  /* Pack a NUL-terminated literal at compile time, so a static table entry
     becomes a constant with no runtime initialization. */
  static constexpr PackedStringKey
  from_literal(const char *text)
  {
    PackedStringKey key{};
    usize i = 0;
    for (; text[i] != '\0' && i < 8; i++)
      key.low_word |= static_cast<u64>(static_cast<u8>(text[i])) << (8 * i);
    for (; text[i] != '\0' && i < 16; i++)
      key.high_word |= static_cast<u64>(static_cast<u8>(text[i]))
                       << (8 * (i - 8));
    return key;
  }

  /* Pack the first sixteen bytes of a view at lookup time. */
  static PackedStringKey
  from_view(StringView text)
  {
    PackedStringKey key{};
    usize count = text.size() < 16 ? text.size() : 16;
    for (usize i = 0; i < count && i < 8; i++)
      key.low_word |= static_cast<u64>(static_cast<u8>(text[i])) << (8 * i);
    for (usize i = 8; i < count; i++)
      key.high_word |= static_cast<u64>(static_cast<u8>(text[i]))
                       << (8 * (i - 8));
    return key;
  }

  [[nodiscard]] bool
  operator==(const PackedStringKey &other) const
  {
    return low_word == other.low_word && high_word == other.high_word;
  }
};

/* An open-addressing hash table from a string key to a Value over an explicit
   allocator, the std::unordered_map replacement. The value defaults to String
   for the variable store and the traps, and takes a pointer for the function
   table. Each slot caches a PackedStringKey of the key's first sixteen bytes, so
   a probe rejects a mismatch with a two-word compare before the full byte
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
         compare runs, and the byte compare confirms a key past sixteen bytes. */
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

/* A set of byte-string keys over the HashMap open-addressing table. The value is
   None, so a set stores only keys. It owns a copy of every key it holds, so a
   view passed to add need not outlive it. */
struct HashSet
{
  explicit HashSet(Allocator allocator) : m_map(allocator) {}

  void
  add(StringView key)
  {
    m_map.set(key, None{});
  }

  [[nodiscard]] bool
  contains(StringView key) const
  {
    return m_map.find(key) != nullptr;
  }

  [[nodiscard]] usize
  size() const
  {
    return m_map.size();
  }

  template <class Fn>
  void
  for_each(Fn fn) const
  {
    m_map.for_each([&fn](StringView key, const None &) { fn(key); });
  }

private:
  HashMap<None> m_map;
};

/* A frozen map from a short byte string to a value, stored in static storage as
   a flat array of packed-key entries. A lookup packs the query into two words
   and scans, so a tiny table resolves in a handful of integer compares with no
   hashing and no allocation. */
template <class Value>
struct StaticStringMap
{
  struct Entry
  {
    PackedStringKey key;
    Value value;
  };

  const Entry *entries;
  usize entry_count;

  [[nodiscard]] Maybe<Value>
  find(StringView text) const
  {
    if (text.size() > 16) return nothing;
    PackedStringKey wanted = PackedStringKey::from_view(text);
    for (usize i = 0; i < entry_count; i++)
      if (entries[i].key == wanted) return entries[i].value;
    return nothing;
  }
};

} /* namespace shit */
