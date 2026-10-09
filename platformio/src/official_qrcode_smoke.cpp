/*
 * Unit QRCode単体の診断用ファームウェア。
 * 通常運用の送信・キュー処理を通さず、M5Stack公式I2C APIでデコードできるかを確認する。
 * platformio.ini の atoms3-official-qrcode 環境だけでビルドされる。
 */

#include <M5Unified.h>
#include <M5UnitQRCode.h>

#include "InventoryDisplay.h"

M5UnitQRCodeI2C qrcode;
InventoryDisplay inventoryDisplay;
uint32_t lastDiagnostic = 0;
bool captured = false;

// 通常版と同じ画面部品で診断状態を表示し、シリアルにも残す。
void showDiagnostic(InventoryScreen screen, const char* message) {
  InventoryView view;
  view.screen = screen;
  view.wifi = WifiVisualState::offline;
  view.detail = message;
  inventoryDisplay.render(view);
  Serial.println(message);
}

// 読取内容そのものを漏らさず、種類と不可逆な指紋で取得成功を確認する。
void printMetadata(const uint8_t* data, uint16_t length) {
  bool numeric = true;
  bool printable = true;
  uint32_t fingerprint = 2166136261u;
  for (uint16_t i = 0; i < length; ++i) {
    numeric &= data[i] >= '0' && data[i] <= '9';
    printable &= data[i] >= 32 && data[i] < 127;
    fingerprint = (fingerprint ^ data[i]) * 16777619u;
  }
  const bool url = length >= 7 &&
    (!memcmp(data, "http://", 7) || (length >= 8 && !memcmp(data, "https://", 8)));
  Serial.printf("Captured bytes: %u, type: %s, fingerprint: %08lx\n", length,
                url ? "url" : numeric ? "numeric" : printable ? "text" : "binary",
                static_cast<unsigned long>(fingerprint));
}

// 公式i2c_mode例と同じ順序でUnit QRCodeを初期化する。
void setup() {
  auto config = M5.config();
  config.output_power = true;
  M5.begin(config);
  Serial.begin(115200);
  inventoryDisplay.begin();

  const int8_t sda = M5.getPin(m5::pin_name_t::port_a_sda);
  const int8_t scl = M5.getPin(m5::pin_name_t::port_a_scl);
  Serial.printf("Official M5UnitQRCode sample path, SDA=%d SCL=%d\n", sda, scl);

  while (!qrcode.begin(&Wire, UNIT_QRCODE_ADDR, sda, scl, 100000U)) {
    showDiagnostic(InventoryScreen::error, "QR READER");
    delay(1000);
  }
  Wire.setBufferSize(512);
  qrcode.setTriggerMode(AUTO_SCAN_MODE);
  showDiagnostic(InventoryScreen::ready, "Official API ready");
}

// ready状態とデータ長を監視し、最初の有効な1件だけを取得する。
void loop() {
  M5.update();
  const uint8_t ready = qrcode.getDecodeReadyStatus();
  const uint16_t length = qrcode.getDecodeLength();

  // 実機では成功後にready=2となるため、ready==1へ限定しない。
  // 誤検出を避けるため、同時に有効なデータ長も要求する。
  if (!captured && ready != 0 && length > 0) {
    if (length == 0 || length > 512) {
      Serial.printf("Official API returned invalid length: %u (ready=%u)\n",
                    length, ready);
      showDiagnostic(InventoryScreen::error, "INVALID LENGTH");
      return;
    }
    uint8_t buffer[513] = {};
    qrcode.getDecodeData(buffer, length);
    printMetadata(buffer, length);
    qrcode.setTriggerMode(MANUAL_SCAN_MODE);
    qrcode.setDecodeTrigger(0);
    captured = true;
    showDiagnostic(InventoryScreen::captured, "Official API captured");
    return;
  }

  if (!captured && millis() - lastDiagnostic >= 1000) {
    lastDiagnostic = millis();
    Serial.printf("Unit QRCode mode=%u ready=%u key=%u length=%u\n",
                  qrcode.getTriggerMode(), ready,
                  qrcode.getTriggerKeyStatus(), length);
  }
  delay(10);
}
