#pragma once

#include <stddef.h>
#include <stdint.h>

// Unit内部のUART設定応答が連結されると、I2Cの読取り結果に現れることがある。
// 全体が既知の成功応答だけの場合に限り除外する。製品コードとの混在や
// 不完全な応答は加工せず、呼出し側のデータ検査へ渡す。
inline bool isReaderSuccessReply(const uint8_t* data, size_t length) {
  if (!data || length == 0 || length % 5 != 0) return false;
  for (size_t offset = 0; offset < length; offset += 5) {
    const uint8_t* frame = data + offset;
    const bool mode = frame[0] == 0x22 && frame[1] == 0x61 && frame[2] == 0x41 &&
                      (frame[3] == 0x00 || frame[3] == 0x05) && frame[4] == 0x00;
    const bool stop = frame[0] == 0x33 && frame[1] == 0x75 && frame[2] == 0x02 &&
                      frame[3] == 0x00 && frame[4] == 0x00;
    if (!mode && !stop) return false;
  }
  return true;
}
