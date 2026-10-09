#include "ReaderStartup.h"

#include <cassert>
#include <cstring>
#include <initializer_list>

using inventory::ReaderStartupResult;

struct FakePort {
  ReaderStartupResult preparation;
  ReaderStartupResult connection;
  unsigned prepared = 0;
  unsigned connected = 0;
  unsigned failedCount = 0;
  bool checkedPower = false;
  ReaderStartupResult reported = ReaderStartupResult::ready;

  ReaderStartupResult prepare(bool checkPower) {
    ++prepared;
    checkedPower = checkPower;
    return preparation;
  }
  ReaderStartupResult connect() { ++connected; return connection; }
  void failed(ReaderStartupResult result) { ++failedCount; reported = result; }
};

int main() {
  const auto ready = ReaderStartupResult::ready;
  FakePort normal{ready, ready};
  assert(inventory::startReader(normal, true) == ready);
  assert(normal.checkedPower && normal.prepared == 1 && normal.connected == 1);
  assert(normal.failedCount == 0);

  // 電源・バス異常では読取器へアクセスしない。異常理由を保持して停止する。
  for (const auto failure : {ReaderStartupResult::power, ReaderStartupResult::bus}) {
    FakePort port{failure, ready};
    assert(inventory::startReader(port, true) == failure);
    assert(port.prepared == 1 && port.connected == 0);
    assert(port.failedCount == 1 && port.reported == failure);
  }

  // ブートモードや設定失敗も、再給電して繰り返さない。
  for (const auto failure : {ReaderStartupResult::recoveryHold, ReaderStartupResult::configuration,
                             ReaderStartupResult::i2c}) {
    FakePort port{ready, failure};
    assert(inventory::startReader(port, true) == failure);
    assert(port.prepared == 1 && port.connected == 1);
    assert(port.failedCount == 1 && port.reported == failure);
  }

  FakePort atom{ready, ready};
  assert(inventory::startReader(atom, false) == ready);
  assert(!atom.checkedPower && atom.prepared == 1 && atom.connected == 1);
  assert(std::strcmp(inventory::readerStartupError(ReaderStartupResult::recoveryHold), "RECOVERY HOLD") == 0);
  assert(!inventory::readerVoltageAcceptable(true, 4499));
  assert(inventory::readerVoltageAcceptable(true, 4500));
  assert(inventory::readerVoltageAcceptable(true, 5500));
  assert(!inventory::readerVoltageAcceptable(true, 5501));
  assert(!inventory::readerVoltageAcceptable(false, 5000));
  assert(!inventory::readerVoltageAcceptable(false, 0));
}
