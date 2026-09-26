#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include "DurableOutbox.h"
#include "SaveStatus.h"

struct MemoryStorage {
  std::map<std::string, std::string> slots;
  uint16_t metadata = 0;
  bool failPayload = false;
  bool failMetadata = false;
  bool failRemove = false;
  size_t putString(const char* key, const std::string& payload) {
    if (failPayload) return 0;
    slots[key] = payload;
    return payload.length();
  }
  size_t putUShort(const char*, uint16_t value) {
    if (failMetadata) return 0;
    metadata = value;
    return sizeof(value);
  }
  uint16_t getUShort(const char*, uint16_t) { return metadata; }
  std::string getString(const char* key, const char*) { return slots[key]; }
  bool remove(const char* key) { if (failRemove) return false; return slots.erase(key); }
};

int main() {
  // Existing NVS keys must remain readable across a firmware update, including wrap.
  MemoryStorage legacy;
  legacy.metadata = 15 | (2 << 8);
  legacy.slots["q15"] = "older";
  legacy.slots["q00"] = "newer";
  std::string legacyItems[16];
  uint8_t legacyHead = 0, legacyCount = 0;
  assert(restoreDurableOutbox(legacyItems, legacyHead, legacyCount, 16, legacy));
  assert(legacyHead == 15 && legacyCount == 2);
  assert(legacyItems[15] == "older" && legacyItems[0] == "newer");
  MemoryStorage storage;
  std::string items[16];
  uint8_t head = 0, count = 0;
  assert(!removeDurableOutbox(items, head, count, 16, storage));
  assert(!appendDurableOutbox(items, head, count, std::string(), 16, 1280, storage));
  storage.failPayload = true;
  assert(!appendDurableOutbox(items, head, count, std::string("lost"), 16, 1280, storage));
  assert(count == 0);
  storage.failPayload = false;
  storage.failMetadata = true;
  assert(!appendDurableOutbox(items, head, count, std::string("uncommitted"), 16, 1280, storage));
  assert(count == 0 && storage.slots.empty());
  storage.failMetadata = false;
  for (int i = 0; i < 16; ++i) {
    assert(appendDurableOutbox(items, head, count, std::string("event-") + std::to_string(i), 16, 1280, storage));
    if (count == 1 || count == 15 || count == 16) {
      std::string restored[16];
      uint8_t restoredHead = 0, restoredCount = 0;
      assert(restoreDurableOutbox(restored, restoredHead, restoredCount, 16, storage));
      assert(restoredCount == count && restoredHead == head);
      for (int j = 0; j < count; ++j) assert(restored[j] == items[j]);
    }
  }
  const auto fullMetadata = storage.metadata;
  assert(!appendDurableOutbox(items, head, count, std::string("seventeenth"), 16, 1280, storage));
  assert(count == 16 && storage.metadata == fullMetadata && items[head] == "event-0");
  storage.failMetadata = true;
  assert(!removeDurableOutbox(items, head, count, 16, storage));
  assert(count == 16 && items[head] == "event-0");
  storage.failMetadata = false;
  // If physical cleanup fails, authoritative metadata still restores exactly
  // the remaining events; stale slots outside qm are never resent.
  storage.failRemove = true;
  assert(removeDurableOutbox(items, head, count, 16, storage));
  assert(count == 15 && items[head] == "event-1");
  assert(appendDurableOutbox(items, head, count, std::string("next-batch"), 16, 1280, storage));
  std::string restored[16];
  uint8_t restoredHead = 0, restoredCount = 0;
  assert(restoreDurableOutbox(restored, restoredHead, restoredCount, 16, storage));
  assert(restoredCount == 16 && restoredHead == 1 && restored[0] == "next-batch");
  for (int i = 0; i < 16; ++i) assert(removeDurableOutbox(items, head, count, 16, storage));
  assert(allSavesConfirmed(count, false));
  assert(restoreDurableOutbox(restored, restoredHead, restoredCount, 16, storage) && restoredCount == 0);
  storage.metadata = durableOutboxMetadata(16, 0);
  assert(!restoreDurableOutbox(restored, restoredHead, restoredCount, 16, storage));
  storage.metadata = durableOutboxMetadata(0, 17);
  assert(!restoreDurableOutbox(restored, restoredHead, restoredCount, 16, storage));
  storage.metadata = durableOutboxMetadata(0, 1);
  storage.slots["q00"] = "";
  assert(!restoreDurableOutbox(restored, restoredHead, restoredCount, 16, storage));
  std::cout << "Production durable FIFO failure, capacity, wrap and restart checks passed\n";
}
