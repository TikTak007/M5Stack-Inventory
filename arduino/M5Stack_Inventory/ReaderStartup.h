#pragma once

#include <cstdint>

namespace inventory {

enum class ReaderStartupResult : std::uint8_t {
  ready,
  power,
  bus,
  i2c,
  configuration,
  recoveryHold,
  storage,
};

constexpr std::uint8_t kReaderBootAddress = 0x54;
constexpr std::uint8_t kReaderBootExitCommand = 0x77;
constexpr unsigned kReaderConnectAttempts = 10;
constexpr unsigned kReaderResumeAttempts = 10;
constexpr std::uint32_t kReaderResumeTimeoutMs = 2000;

// 通常モードの設定まで確認してから、次の障害での復帰を許可する。
template <class Port, class Guard>
ReaderStartupResult confirmReader(Port& port, Guard& guard, bool pending) {
  const auto result = port.configure();
  if (result != ReaderStartupResult::ready) return result;
  if (pending && !guard.writePending(false)) return ReaderStartupResult::storage;
  return ReaderStartupResult::ready;
}

template <class Port, class Guard>
ReaderStartupResult waitForReaderResume(Port& port, Guard& guard) {
  const std::uint32_t started = port.now();
  for (unsigned attempt = 0; attempt < kReaderResumeAttempts; ++attempt) {
    port.pause(attempt ? 200 : 20);
    if (static_cast<std::uint32_t>(port.now() - started) >= kReaderResumeTimeoutMs) break;
    if (port.normalAvailable()) return confirmReader(port, guard, true);
  }
  return ReaderStartupResult::recoveryHold;
}

template <class Port, class Guard>
ReaderStartupResult connectReader(Port& port, Guard& guard) {
  bool pending = false;
  if (!guard.readPending(pending)) return ReaderStartupResult::storage;
  if (pending) {
    // ジャンプ失敗後の0x54は空のACK確認でも前の命令を再処理することがある。
    // 本体が再起動した場合も、通常アドレスだけを確認する。
    port.showResume();
    return waitForReaderResume(port, guard);
  }
  for (unsigned attempt = 0; attempt < kReaderConnectAttempts; ++attempt) {
    if (port.normalAvailable()) return confirmReader(port, guard, false);
    if (port.bootAvailable()) {
      // 送信直後に本体が再起動しても再送しないよう、命令より先に保存する。
      if (!guard.writePending(true)) return ReaderStartupResult::storage;
      port.showResume();
      port.requestBootExit();
      // ACKの成否にかかわらず、以後は0x21の応答と設定だけで復帰を判定する。
      return waitForReaderResume(port, guard);
    }
    if (attempt + 1 < kReaderConnectAttempts) port.pause(400);
  }
  return ReaderStartupResult::i2c;
}

inline bool readerVoltageAcceptable(bool valid, std::uint16_t millivolts) {
  return valid && millivolts >= 4500 && millivolts <= 5500;
}

// 異常時は診断結果を返して停止する。読取器の電源は入れ直さない。
template <class Port>
ReaderStartupResult startReader(Port& port, bool checkPower) {
  auto result = port.prepare(checkPower);
  if (result == ReaderStartupResult::ready) result = port.connect();
  if (result != ReaderStartupResult::ready) port.failed(result);
  return result;
}

inline const char* readerStartupError(ReaderStartupResult result) {
  switch (result) {
    case ReaderStartupResult::power: return "READER POWER";
    case ReaderStartupResult::bus: return "READER BUS";
    case ReaderStartupResult::configuration: return "READER CONFIG";
    case ReaderStartupResult::recoveryHold: return "RECOVERY HOLD";
    case ReaderStartupResult::storage: return "READER STORAGE";
    default: return "READER I2C";
  }
}

}  // namespace inventory
