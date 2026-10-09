#pragma once
#include <cstdint>
#include <cstring>

// A batch snapshots only the queued events at its first dispatch. Later scans
// join the following batch; only a verified, durably removed head advances it.
struct SaveBatchProgress {
  uint8_t total = 0;
  uint8_t saved = 0;
  void beginIfNeeded(uint8_t queued) {
    if (queued && (total == 0 || saved == total)) { total = queued; saved = 0; }
  }
  void confirmedRemoval() { if (saved < total) ++saved; }
};

inline bool isVerifiedSaveAck(int httpCode, bool okIsBool, bool ok,
                              const char* responseId, const char* requestId,
                              bool verifiedIsBool, bool verified) {
  return httpCode == 200 && okIsBool && ok && verifiedIsBool && verified &&
    responseId && requestId && requestId[0] && !std::strcmp(responseId, requestId);
}

// The same typed extraction is shared by firmware and host JSON fixtures.
template <typename Json>
bool isVerifiedSaveAckJson(int httpCode, const Json& response, const char* requestId) {
  return isVerifiedSaveAck(httpCode, response["ok"].template is<bool>(),
    response["ok"].template as<bool>(),
    response["eventId"].template is<const char*>()
      ? response["eventId"].template as<const char*>() : nullptr,
    requestId, response["verified"].template is<bool>(),
    response["verified"].template as<bool>());
}

inline bool allSavesConfirmed(uint8_t queued, bool inFlight) {
  return queued == 0 && !inFlight;
}

inline bool foregroundScanOwnsDisplay(bool scanning, bool rejected,
                                     uint32_t now, uint32_t started, uint32_t holdMs) {
  return scanning || rejected || static_cast<uint32_t>(now - started) < holdMs;
}

// Presentation ownership persists across background ACKs. A rejected read
// requires a fresh TRIG even after an older event frees a queue slot.
struct ScanSavePresentation {
  SaveBatchProgress batch;
  bool foregroundActive = false;
  bool rejected = false;
  bool allSavedPending = false;
  uint32_t startedAt = 0;

  void holdForeground(uint32_t now) { foregroundActive = true; startedAt = now; }
  void trigger(uint32_t now) { rejected = false; holdForeground(now); }
  void accepted(uint32_t now) { rejected = false; allSavedPending = false; holdForeground(now); }
  void reject(uint32_t now) { rejected = true; holdForeground(now); }
  bool ownsDisplay(bool scanning, uint32_t now, uint32_t holdMs) const {
    return foregroundActive && foregroundScanOwnsDisplay(scanning, rejected, now, startedAt, holdMs);
  }
  void dispatched(uint8_t queued) { batch.beginIfNeeded(queued); }
  void confirmedRemoval(uint8_t queued, bool inFlight) {
    batch.confirmedRemoval();
    allSavedPending = allSavesConfirmed(queued, inFlight);
  }
  void ready(uint8_t queued, bool inFlight) {
    if (allSavesConfirmed(queued, inFlight)) batch = {};
  }
};
