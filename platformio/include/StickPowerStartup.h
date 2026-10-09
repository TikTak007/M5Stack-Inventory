#pragma once

#include <cstdint>

namespace inventory {

enum class StickPowerStartupResult : std::uint8_t {
  ready,
  boardMismatch,
  irHold,
  initialization,
  irRelease,
};

// StickS3のIR LEDを消灯に保持してから、本体とGroveの電源を初期化する。
// 保持できない場合は、外部給電を伴う初期化へ進まない。
template <class Board>
StickPowerStartupResult beginStickWithIrOff(Board& board) {
  if (!board.detectStick()) return StickPowerStartupResult::boardMismatch;
  if (!board.holdIrOff()) return StickPowerStartupResult::irHold;
  if (!board.begin()) return StickPowerStartupResult::initialization;
  if (!board.releaseIrOff()) return StickPowerStartupResult::irRelease;
  return StickPowerStartupResult::ready;
}

inline const char* stickPowerStartupError(StickPowerStartupResult result) {
  switch (result) {
    case StickPowerStartupResult::boardMismatch: return "BOARD MISMATCH";
    case StickPowerStartupResult::irHold: return "IR HOLD";
    case StickPowerStartupResult::initialization: return "DEVICE INIT";
    case StickPowerStartupResult::irRelease: return "IR RELEASE";
    default: return "DEVICE READY";
  }
}

}  // namespace inventory
