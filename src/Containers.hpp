#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"

#include <cstring>
#include <new>
#include <utility>

namespace shit {

/* A non-owning view of bytes, the form a function takes when it does not own the
   characters. It points into a String, a literal, or a slice. */
struct StringView
{
  const char *data{nullptr};
  usize length{0};

  StringView() = default;
  StringView(const char *bytes, usize count) : data(bytes), length(count) {}
  StringView(const char *cstr)
      : data(cstr), length(cstr != nullptr ? std::strlen(cstr) : 0)
  {}

  [[nodiscard]] usize size() const { return length; }
  [[nodiscard]] bool empty() const { return length == 0; }
  [[nodiscard]] char operator[](usize i) const { return data[i]; }

  [[nodiscard]] bool operator==(StringView other) const
  {
    return length == other.length &&
           (length == 0 || std::memcmp(data, other.data, length) == 0);
  }
  [[nodiscard]] bool operator!=(StringView other) const
  {
    return !(*this == other);
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

  String &operator=(const String &other)
  {
    if (this != &other) {
      clear();
      append(other.view());
    }
    return *this;
  }
  String &operator=(String &&other) noexcept
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

  [[nodiscard]] usize size() const { return m_length; }
  [[nodiscard]] bool empty() const { return m_length == 0; }
  [[nodiscard]] char operator[](usize i) const { return m_data[i]; }
  [[nodiscard]] StringView view() const { return StringView{m_data, m_length}; }
  [[nodiscard]] const char *c_str() const
  {
    return m_data != nullptr ? m_data : "";
  }

  void clear() { m_length = 0; if (m_data != nullptr) m_data[0] = '\0'; }

  void push(char c)
  {
    reserve(m_length + 1);
    m_data[m_length++] = c;
    m_data[m_length] = '\0';
  }
  void append(StringView other)
  {
    if (other.length == 0) return;
    reserve(m_length + other.length);
    std::memcpy(m_data + m_length, other.data, other.length);
    m_length += other.length;
    m_data[m_length] = '\0';
  }

  void reserve(usize needed)
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

  [[nodiscard]] bool operator==(StringView other) const
  {
    return view() == other;
  }

  /* Byte order, so a sort matches the C locale collating order. */
  [[nodiscard]] bool operator<(const String &other) const
  {
    usize shared = m_length < other.m_length ? m_length : other.m_length;
    int order = shared == 0 ? 0 : std::memcmp(c_str(), other.c_str(), shared);
    if (order != 0) return order < 0;
    return m_length < other.m_length;
  }

private:
  void free_storage()
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

  ArrayList &operator=(ArrayList &&other) noexcept
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

  ~ArrayList() { destroy_all(); }

  [[nodiscard]] usize size() const { return m_length; }
  [[nodiscard]] bool empty() const { return m_length == 0; }
  [[nodiscard]] T &operator[](usize i) { return m_data[i]; }
  [[nodiscard]] const T &operator[](usize i) const { return m_data[i]; }
  [[nodiscard]] T *begin() { return m_data; }
  [[nodiscard]] T *end() { return m_data + m_length; }
  [[nodiscard]] const T *begin() const { return m_data; }
  [[nodiscard]] const T *end() const { return m_data + m_length; }

  void push(T value)
  {
    reserve(m_length + 1);
    new (&m_data[m_length]) T(std::move(value));
    m_length++;
  }

  void reserve(usize needed)
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
  void destroy_all()
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

/* An open-addressing hash table from a string key to a String value over an
   explicit allocator, the std::unordered_map replacement for the variable
   store. Linear probing, power-of-two capacity, grows past a load of three
   quarters. */
struct HashMap
{
  explicit HashMap(Allocator allocator) : m_allocator(allocator) {}

  HashMap(const HashMap &other) : m_allocator(other.m_allocator)
  {
    rehash(other.m_capacity == 0 ? 16 : other.m_capacity);
    for (usize i = 0; i < other.m_capacity; i++) {
      if (other.m_slots[i].state == Slot::Occupied)
        set(other.m_slots[i].key.view(), other.m_slots[i].value.view());
    }
  }
  HashMap(HashMap &&other) noexcept
      : m_allocator(other.m_allocator), m_slots(other.m_slots),
        m_capacity(other.m_capacity), m_count(other.m_count)
  {
    other.m_slots = nullptr;
    other.m_capacity = 0;
    other.m_count = 0;
  }
  HashMap &operator=(HashMap &&other) noexcept
  {
    if (this != &other) {
      destroy_all();
      m_allocator = other.m_allocator;
      m_slots = other.m_slots;
      m_capacity = other.m_capacity;
      m_count = other.m_count;
      other.m_slots = nullptr;
      other.m_capacity = 0;
      other.m_count = 0;
    }
    return *this;
  }

  ~HashMap() { destroy_all(); }

  [[nodiscard]] usize size() const { return m_count; }

  /* The value for the key, or nullptr when absent. The pointer is stable until
     the next set that grows the table. */
  [[nodiscard]] const String *find(StringView key) const
  {
    if (m_capacity == 0) return nullptr;
    usize mask = m_capacity - 1;
    usize i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      const Slot &slot = m_slots[i];
      if (slot.state == Slot::Empty) return nullptr;
      if (slot.state == Slot::Occupied && slot.key == key) return &slot.value;
      i = (i + 1) & mask;
    }
    return nullptr;
  }

  void set(StringView key, StringView value)
  {
    if (m_count + 1 > (m_capacity >> 1) + (m_capacity >> 2))
      rehash(m_capacity == 0 ? 16 : m_capacity * 2);

    usize mask = m_capacity - 1;
    usize i = hash_bytes(key) & mask;
    usize first_tombstone = m_capacity;
    for (usize probe = 0; probe < m_capacity; probe++) {
      Slot &slot = m_slots[i];
      if (slot.state == Slot::Occupied && slot.key == key) {
        slot.value = String{m_allocator, value};
        return;
      }
      if (slot.state == Slot::Empty) {
        usize target = first_tombstone != m_capacity ? first_tombstone : i;
        place(target, key, value);
        return;
      }
      if (slot.state == Slot::Tombstone && first_tombstone == m_capacity)
        first_tombstone = i;
      i = (i + 1) & mask;
    }
  }

  void erase(StringView key)
  {
    if (m_capacity == 0) return;
    usize mask = m_capacity - 1;
    usize i = hash_bytes(key) & mask;
    for (usize probe = 0; probe < m_capacity; probe++) {
      Slot &slot = m_slots[i];
      if (slot.state == Slot::Empty) return;
      if (slot.state == Slot::Occupied && slot.key == key) {
        slot.key.~String();
        slot.value.~String();
        slot.state = Slot::Tombstone;
        m_count--;
        return;
      }
      i = (i + 1) & mask;
    }
  }

  /* Visit each key and value in unspecified order. */
  template <class Fn>
  void for_each(Fn fn) const
  {
    for (usize i = 0; i < m_capacity; i++) {
      if (m_slots[i].state == Slot::Occupied)
        fn(m_slots[i].key.view(), m_slots[i].value.view());
    }
  }

  void clear()
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
    String key{heap_allocator()};
    String value{heap_allocator()};
  };

  void place(usize index, StringView key, StringView value)
  {
    Slot &slot = m_slots[index];
    slot.key = String{m_allocator, key};
    slot.value = String{m_allocator, value};
    slot.state = Slot::Occupied;
    m_count++;
  }

  void rehash(usize new_capacity)
  {
    Slot *old_slots = m_slots;
    usize old_capacity = m_capacity;

    m_slots = m_allocator.alloc_array<Slot>(new_capacity);
    for (usize i = 0; i < new_capacity; i++)
      new (&m_slots[i]) Slot{};
    m_capacity = new_capacity;
    m_count = 0;

    for (usize i = 0; i < old_capacity; i++) {
      if (old_slots[i].state == Slot::Occupied)
        set(old_slots[i].key.view(), old_slots[i].value.view());
      old_slots[i].~Slot();
    }
    if (old_slots != nullptr) m_allocator.free_array(old_slots, old_capacity);
  }

  void destroy_all()
  {
    for (usize i = 0; i < m_capacity; i++)
      m_slots[i].~Slot();
    if (m_slots != nullptr) m_allocator.free_array(m_slots, m_capacity);
    m_slots = nullptr;
    m_capacity = 0;
    m_count = 0;
  }

  Allocator m_allocator;
  Slot *m_slots{nullptr};
  usize m_capacity{0};
  usize m_count{0};
};

} /* namespace shit */
