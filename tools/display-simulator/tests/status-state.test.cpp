#include <cassert>
#include <iostream>
#include "SaveStatus.h"

int main() {
  assert(isVerifiedSaveAck(200, true, true, "evt", "evt", true, true));
  assert(!isVerifiedSaveAck(200, true, true, "evt", "evt", false, false));
  assert(!isVerifiedSaveAck(200, true, true, "evt", "evt", true, false));
  assert(!isVerifiedSaveAck(200, true, true, "other", "evt", true, true));
  assert(!isVerifiedSaveAck(200, true, true, nullptr, "evt", true, true));
  assert(!isVerifiedSaveAck(200, true, true, "", "", true, true));
  assert(!isVerifiedSaveAck(500, true, true, "evt", "evt", true, true));
  assert(!isVerifiedSaveAck(200, false, true, "evt", "evt", true, true));
  assert(!isVerifiedSaveAck(200, true, false, "evt", "evt", true, true));

  for (uint8_t count : {0, 1, 15, 16}) {
    SaveBatchProgress batch;
    batch.beginIfNeeded(count);
    assert(batch.total == count && batch.saved == 0);
    // In-flight always blocks ALL SAVED, even when RAM happens to be empty.
    assert(!allSavesConfirmed(count, true));
    assert(allSavesConfirmed(count, false) == (count == 0));
    for (uint8_t confirmed = 0; confirmed < count; ++confirmed) {
      // Retry failure/local metadata failure calls no removal, so no increment.
      batch.beginIfNeeded(count);
      assert(batch.saved == confirmed && batch.total == count);
      batch.confirmedRemoval();
    }
    assert(batch.saved == count);
  }
  SaveBatchProgress batch;
  batch.beginIfNeeded(5);
  batch.confirmedRemoval();
  batch.confirmedRemoval();
  // Two new reads while three originals remain must not change this batch.
  batch.beginIfNeeded(5);
  assert(batch.saved == 2 && batch.total == 5);
  for (int i = 0; i < 3; ++i) batch.confirmedRemoval();
  assert(!allSavesConfirmed(2, false));
  batch.beginIfNeeded(2);
  assert(batch.saved == 0 && batch.total == 2);
  SaveBatchProgress reboot;
  reboot.beginIfNeeded(2);
  assert(reboot.saved == 0 && reboot.total == 2);

  assert(foregroundScanOwnsDisplay(true, false, 9000, 0, 5000));
  assert(foregroundScanOwnsDisplay(false, true, 9000, 0, 5000));
  assert(foregroundScanOwnsDisplay(false, false, 4999, 0, 5000));
  assert(!foregroundScanOwnsDisplay(false, false, 5000, 0, 5000));
  assert(foregroundScanOwnsDisplay(false, false, 10, UINT32_MAX - 10, 5000));
  ScanSavePresentation display;
  display.trigger(10);
  display.accepted(20);
  display.dispatched(5);
  display.confirmedRemoval(4, false);
  assert(display.ownsDisplay(false, 21, 5000));
  assert(display.batch.saved == 1 && !display.allSavedPending);
  // A newer scan owns the display even when the previous head is acknowledged.
  display.trigger(30);
  display.confirmedRemoval(3, false);
  assert(display.ownsDisplay(true, 9000, 5000));
  display.accepted(9001);
  display.dispatched(4);
  assert(display.batch.total == 5 && display.batch.saved == 2);
  display.reject(9002);
  display.confirmedRemoval(2, false);
  assert(display.ownsDisplay(false, 20000, 5000) && display.rejected);
  display.confirmedRemoval(1, false);
  display.confirmedRemoval(0, false);
  assert(display.allSavedPending && display.ownsDisplay(false, 30000, 5000));
  // The rejected code was never accepted; draining old data cannot hide it.
  display.trigger(30001);
  assert(!display.rejected && display.ownsDisplay(true, 40000, 5000));
  display.accepted(40001);
  assert(!display.allSavedPending);
  display.dispatched(1);
  assert(display.batch.total == 1 && display.batch.saved == 0);
  display.confirmedRemoval(0, false);
  assert(display.allSavedPending && display.ownsDisplay(false, 40002, 5000));
  assert(!display.ownsDisplay(false, 45001, 5000));
  display.ready(0, false);
  assert(display.batch.total == 0);

  std::cout << "Save ACK, batch, drain and foreground priority checks passed\n";
}
