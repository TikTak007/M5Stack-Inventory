#include "ReaderStartup.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using inventory::ReaderStartupResult;

struct Guard {
  std::vector<std::string>& calls;
  bool pending = false;
  bool readable = true;
  bool canReserve = true;
  bool canClear = true;

  bool readPending(bool& value) {
    calls.push_back("read-guard");
    value = pending;
    return readable;
  }
  bool writePending(bool value) {
    calls.push_back(value ? "reserve" : "clear");
    if (!(value ? canReserve : canClear)) return false;
    pending = value;
    return true;
  }
};

struct Port {
  Guard& guard;
  std::vector<std::string>& calls;
  bool bootPresent = true;
  bool requestTakesEffect = true;
  bool resetDuringRequest = false;
  unsigned readyAfter = std::numeric_limits<unsigned>::max();
  unsigned normalProbes = 0;
  unsigned bootProbes = 0;
  unsigned requests = 0;
  std::uint32_t clock = 0;
  std::uint32_t probeDuration = 0;
  ReaderStartupResult configuration = ReaderStartupResult::ready;

  bool normalAvailable() {
    calls.push_back("0x21");
    ++normalProbes;
    clock += probeDuration;
    return normalProbes >= readyAfter;
  }
  bool bootAvailable() {
    // pending中の空probeも、ブートローダー側で前の0x77を再処理し得る。
    assert(!guard.pending);
    calls.push_back("0x54");
    ++bootProbes;
    return bootPresent;
  }
  void requestBootExit() {
    assert(guard.pending);
    calls.push_back("0x77");
    ++requests;
    if (resetDuringRequest) throw 1;
    if (requestTakesEffect) readyAfter = normalProbes + 2;
  }
  ReaderStartupResult configure() { calls.push_back("configure"); return configuration; }
  void showResume() { calls.push_back("resume-screen"); }
  std::uint32_t now() { return clock; }
  void pause(std::uint32_t duration) { clock += duration; }
};

struct Fixture {
  std::vector<std::string> calls;
  Guard guard{calls};
  Port port{guard, calls};
};

int main() {
  static_assert(inventory::kReaderBootAddress == 0x54);
  static_assert(inventory::kReaderBootExitCommand == 0x77);

  Fixture normal;
  normal.port.readyAfter = 1;
  assert(inventory::connectReader(normal.port, normal.guard) == ReaderStartupResult::ready);
  assert((normal.calls == std::vector<std::string>{"read-guard", "0x21", "configure"}));

  Fixture resumed;
  assert(inventory::connectReader(resumed.port, resumed.guard) == ReaderStartupResult::ready);
  assert(resumed.port.bootProbes == 1 && resumed.port.requests == 1);
  assert(!resumed.guard.pending);
  assert((resumed.calls == std::vector<std::string>{"read-guard", "0x21", "0x54", "reserve",
    "resume-screen", "0x77", "0x21", "0x21", "configure", "clear"}));

  Fixture unreadable;
  unreadable.guard.readable = false;
  assert(inventory::connectReader(unreadable.port, unreadable.guard) == ReaderStartupResult::storage);
  assert(unreadable.calls.size() == 1);

  Fixture cannotReserve;
  cannotReserve.guard.canReserve = false;
  assert(inventory::connectReader(cannotReserve.port, cannotReserve.guard) == ReaderStartupResult::storage);
  assert(cannotReserve.port.bootProbes == 1 && cannotReserve.port.requests == 0);

  Fixture timeout;
  timeout.port.requestTakesEffect = false;
  assert(inventory::connectReader(timeout.port, timeout.guard) == ReaderStartupResult::recoveryHold);
  assert(timeout.guard.pending && timeout.port.requests == 1 && timeout.port.bootProbes == 1);
  assert(timeout.port.normalProbes == 1 + inventory::kReaderResumeAttempts);
  assert(timeout.port.clock <= inventory::kReaderResumeTimeoutMs);
  // 失敗後にもう一度接続処理を呼んでも、0x54に触れず通常アドレスだけを見る。
  assert(inventory::connectReader(timeout.port, timeout.guard) == ReaderStartupResult::recoveryHold);
  assert(timeout.port.requests == 1 && timeout.port.bootProbes == 1);

  Fixture reset;
  reset.port.resetDuringRequest = true;
  bool interrupted = false;
  try { inventory::connectReader(reset.port, reset.guard); }
  catch (int) { interrupted = true; }
  assert(interrupted && reset.guard.pending);
  Port nextBoot{reset.guard, reset.calls};
  assert(inventory::connectReader(nextBoot, reset.guard) == ReaderStartupResult::recoveryHold);
  assert(nextBoot.requests == 0 && nextBoot.bootProbes == 0);
  // 遅れて通常モードへ復帰した、またはUnitの電源を入れ直した後は設定確認で解除。
  nextBoot.readyAfter = nextBoot.normalProbes + 1;
  assert(inventory::connectReader(nextBoot, reset.guard) == ReaderStartupResult::ready);
  assert(!reset.guard.pending && nextBoot.requests == 0 && nextBoot.bootProbes == 0);

  Fixture badConfig;
  badConfig.port.configuration = ReaderStartupResult::configuration;
  assert(inventory::connectReader(badConfig.port, badConfig.guard) == ReaderStartupResult::configuration);
  assert(badConfig.guard.pending);
  assert(std::find(badConfig.calls.begin(), badConfig.calls.end(), "clear") == badConfig.calls.end());

  Fixture failedClear;
  failedClear.guard.canClear = false;
  assert(inventory::connectReader(failedClear.port, failedClear.guard) == ReaderStartupResult::storage);
  assert(failedClear.guard.pending);

  Fixture disconnected;
  disconnected.port.bootPresent = false;
  assert(inventory::connectReader(disconnected.port, disconnected.guard) == ReaderStartupResult::i2c);
  assert(disconnected.port.normalProbes == inventory::kReaderConnectAttempts);
  assert(disconnected.port.requests == 0 && !disconnected.guard.pending);

  Fixture slow;
  slow.guard.pending = true;
  slow.port.probeDuration = 500;
  assert(inventory::connectReader(slow.port, slow.guard) == ReaderStartupResult::recoveryHold);
  assert(slow.port.normalProbes < inventory::kReaderResumeAttempts);

  Fixture wrap;
  wrap.guard.pending = true;
  wrap.port.clock = std::numeric_limits<std::uint32_t>::max() - 50;
  wrap.port.readyAfter = 2;
  assert(inventory::connectReader(wrap.port, wrap.guard) == ReaderStartupResult::ready);
  assert(!wrap.guard.pending && wrap.port.requests == 0 && wrap.port.bootProbes == 0);
}
