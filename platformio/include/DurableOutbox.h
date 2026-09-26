#pragma once
#include <cstdint>
#include <cstdio>

// qm remains the authoritative FIFO head/count. Payload slots are committed
// before qm on append; qm is committed before clearing an acknowledged slot.
// A failed metadata write retains the previous logical queue on restart.
inline uint16_t durableOutboxMetadata(uint8_t head, uint8_t count) {
  return static_cast<uint16_t>(head) | (static_cast<uint16_t>(count) << 8);
}

inline void durableOutboxKey(uint8_t slot, char (&key)[5]) {
  std::snprintf(key, sizeof(key), "q%02u", static_cast<unsigned>(slot));
}

template <typename Text, typename Storage>
bool appendDurableOutbox(Text* items, uint8_t& head, uint8_t& count,
                         const Text& payload, uint8_t capacity,
                         size_t payloadCapacity, Storage& storage) {
  if (!payload.length() || payload.length() >= payloadCapacity || count >= capacity) return false;
  const uint8_t slot = (head + count) % capacity;
  char key[5];
  durableOutboxKey(slot, key);
  if (storage.putString(key, payload) != payload.length()) return false;
  if (storage.putUShort("qm", durableOutboxMetadata(head, count + 1)) != sizeof(uint16_t)) {
    storage.remove(key);
    return false;
  }
  items[slot] = payload;
  ++count;
  return true;
}

template <typename Text, typename Storage>
bool removeDurableOutbox(Text* items, uint8_t& head, uint8_t& count,
                         uint8_t capacity, Storage& storage) {
  if (!count) return false;
  const uint8_t oldHead = head;
  const uint8_t nextHead = (head + 1) % capacity;
  const uint8_t nextCount = count - 1;
  if (storage.putUShort("qm", durableOutboxMetadata(nextHead, nextCount)) != sizeof(uint16_t)) return false;
  head = nextHead;
  count = nextCount;
  items[oldHead] = "";
  char key[5];
  durableOutboxKey(oldHead, key);
  storage.remove(key);
  return true;
}

template <typename Text, typename Storage>
bool restoreDurableOutbox(Text* items, uint8_t& head, uint8_t& count,
                          uint8_t capacity, Storage& storage) {
  const uint16_t metadata = storage.getUShort("qm", 0);
  head = metadata & 0xff;
  count = metadata >> 8;
  if (head >= capacity || count > capacity) return false;
  for (uint8_t i = 0; i < count; ++i) {
    char key[5];
    durableOutboxKey((head + i) % capacity, key);
    items[(head + i) % capacity] = storage.getString(key, "");
    if (!items[(head + i) % capacity].length()) return false;
  }
  return true;
}
