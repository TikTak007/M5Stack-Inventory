#include "StickPowerStartup.h"

#include <cassert>
#include <cstring>
#include <vector>

using inventory::StickPowerStartupResult;

struct FakeBoard {
  bool isStick = true;
  bool canHold = true;
  bool canBegin = true;
  bool canRelease = true;
  bool held = false;
  bool powerEnabled = false;
  std::vector<unsigned> calls;

  bool detectStick() { calls.push_back(1); return isStick; }
  bool holdIrOff() { calls.push_back(2); held = canHold; return canHold; }
  bool begin() {
    assert(isStick && held);
    calls.push_back(3);
    powerEnabled = true;
    return canBegin;
  }
  bool releaseIrOff() {
    assert(held && powerEnabled);
    calls.push_back(4);
    if (canRelease) held = false;
    return canRelease;
  }
};

int main() {
  FakeBoard normal;
  assert(inventory::beginStickWithIrOff(normal) == StickPowerStartupResult::ready);
  assert((normal.calls == std::vector<unsigned>{1, 2, 3, 4}));
  assert(normal.powerEnabled && !normal.held);

  // 機種を特定できないときは、IR端子や本体電源を操作しない。
  FakeBoard differentBoard;
  differentBoard.isStick = false;
  assert(inventory::beginStickWithIrOff(differentBoard) == StickPowerStartupResult::boardMismatch);
  assert((differentBoard.calls == std::vector<unsigned>{1}));
  assert(!differentBoard.powerEnabled);

  // 消灯を保持できないままGroveへ給電しない。
  FakeBoard holdFailure;
  holdFailure.canHold = false;
  assert(inventory::beginStickWithIrOff(holdFailure) == StickPowerStartupResult::irHold);
  assert((holdFailure.calls == std::vector<unsigned>{1, 2}));
  assert(!holdFailure.powerEnabled);

  FakeBoard initializationFailure;
  initializationFailure.canBegin = false;
  assert(inventory::beginStickWithIrOff(initializationFailure) == StickPowerStartupResult::initialization);
  assert((initializationFailure.calls == std::vector<unsigned>{1, 2, 3}));
  assert(initializationFailure.held);

  FakeBoard releaseFailure;
  releaseFailure.canRelease = false;
  assert(inventory::beginStickWithIrOff(releaseFailure) == StickPowerStartupResult::irRelease);
  assert(releaseFailure.held);
  assert(std::strcmp(inventory::stickPowerStartupError(StickPowerStartupResult::irHold), "IR HOLD") == 0);
}
