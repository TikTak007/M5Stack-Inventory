/*
 * M5Stack Inventory
 * K Visualization Studio
 * T.KAMIKURA
 * 2026-09-20
 *
 * Unit QRCodeを監視し、読み取ったイベントを不揮発FIFOへ保存する。
 * 通信は別FreeRTOSタスクで行うため、HTTPS待機中もTRIG操作と画面更新を続けられる。
 * Apps Scriptが保存済みと確認したイベントだけをFIFOから削除する。
 */

#include <M5Unified.h>
#include <M5UnitQRCode.h>
#include "InventoryDisplay.h"
#include "SaveStatus.h"
#include "DurableOutbox.h"
#include "ReaderStartup.h"
#include "ReaderReply.h"
#include "StickPowerStartup.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>
#include <nvs.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <time.h>
#include "secrets.h"

namespace {
// 公開プロトコルPDFの版にある0x00F0表記ではなく、Unitの実装と一致する定義を使う。
static_assert(FIRMWARE_VERSION_REG == 0x00FE, "Check Unit QRCode firmware version register");
constexpr uint32_t kResultHoldMs = 5000;
constexpr uint32_t kNoCodeHoldMs = 1800;
constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kPendingRetryMs = 10000;
constexpr uint32_t kNextQueueSendDelayMs = 800;
constexpr uint32_t kScannerPollMs = 10;
constexpr uint32_t kReaderQuietBootMs = 800;
constexpr uint32_t kBatteryRefreshMs = 10000;
constexpr uint32_t kAnimationMs = 180;
constexpr uint32_t kCountdownRefreshMs = 250;
constexpr time_t kMinimumValidTime = 1700000000;
constexpr uint8_t kOutboxCapacity = 16;
constexpr size_t kPayloadCapacity = 1280;
}

M5UnitQRCodeI2C scanner;
InventoryDisplay inventoryDisplay;
Preferences storage;

InventoryView view;
String currentCode;
String outbox[kOutboxCapacity];
uint8_t outboxHead = 0;
uint8_t outboxCount = 0;

// メインループとネットワークタスクの間は固定長データだけを受け渡す。
struct SendJob {
  char payload[kPayloadCapacity];
};

enum class SendOutcome : uint8_t {
  saved,
  notReady,
  notConfirmed,
};

enum class ReplyState : uint8_t {
  invalid,
  storageVerified,
};

struct SendResult {
  SendOutcome outcome;
  char eventId[81];
};

QueueHandle_t sendJobs = nullptr;
QueueHandle_t sendResults = nullptr;
bool sendInFlight = false;
ScanSavePresentation presentation;
String foregroundEventId;
String sendError;

uint32_t screenStartedAt = 0;
uint32_t lastRenderAt = 0;
uint32_t lastScannerPollAt = 0;
uint32_t lastWifiAttemptAt = 0;
uint32_t nextPendingRetryAt = 0;
uint32_t lastBatterySampleAt = 0;
uint8_t animationPhase = 0;
uint8_t lastTriggerKey = 1;
bool triggerKeyKnown = false;
bool triggerHeld = false;
bool scanHandledForPress = false;
bool ntpStarted = false;
WifiVisualState renderedWifi = WifiVisualState::offline;
bool batterySupported = false;
int16_t batteryPercent = -1;

// コード本文をシリアルへ出さず、長さ・種類・不可逆な指紋だけを診断出力する。
void printCaptureMetadata(const uint8_t* data, uint16_t length) {
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

WifiVisualState wifiVisualState() {
  if (INVENTORY_CAPTURE_ONLY || strlen(INVENTORY_WIFI_SSID) == 0) {
    return WifiVisualState::offline;
  }
  if (WiFi.status() == WL_CONNECTED) return WifiVisualState::online;
  return millis() - lastWifiAttemptAt < 15000 ? WifiVisualState::connecting
                                              : WifiVisualState::offline;
}

// TLS証明書検証には正しい時刻が必要なため、Wi-FiとNTPの両方を確認する。
bool transportReady() {
  return WiFi.status() == WL_CONNECTED && time(nullptr) >= kMinimumValidTime;
}

void renderCurrent(bool force = false) {
  const uint32_t now = millis();
  view.wifi = wifiVisualState();
  view.unsent = outboxCount;
  view.batchTotal = presentation.batch.total;
  view.batchSaved = presentation.batch.saved;
  view.hasBattery = batterySupported;
  view.batteryPercent = batteryPercent;
  if (view.screen == InventoryScreen::saved || view.screen == InventoryScreen::captured) {
    const uint32_t elapsed = now - screenStartedAt;
    view.remainingMs = elapsed >= kResultHoldMs ? 0 : kResultHoldMs - elapsed;
  }
  if (!force && now - lastRenderAt < kAnimationMs) return;
  inventoryDisplay.render(view);
  renderedWifi = view.wifi;
  lastRenderAt = now;
}

void showScreen(InventoryScreen screen, const String& code = "", const String& detail = "") {
  view.screen = screen;
  view.code = code;
  view.detail = detail;
  view.phase = animationPhase;
  view.remainingMs = (screen == InventoryScreen::saved || screen == InventoryScreen::captured)
                         ? kResultHoldMs
                         : 0;
  screenStartedAt = millis();
  renderCurrent(true);
}

void showReady() {
  currentCode = "";
  presentation.ready(outboxCount, sendInFlight);
  showScreen(InventoryScreen::ready);
  Serial.println("Ready: hold QR TRIG to scan");
}

String safeDisplayText(const uint8_t* data, uint16_t length) {
  String text;
  text.reserve(length);
  for (uint16_t i = 0; i < length; ++i) {
    const uint8_t value = data[i];
    text += (value >= 32 && value != 127) ? static_cast<char>(value) : '.';
  }
  return text;
}

String codeFromPayload(const String& payload) {
  if (!payload.length()) return "";
  JsonDocument request;
  if (deserializeJson(request, payload)) return "";
  return request["code"].as<String>();
}

String eventIdFromPayload(const String& payload) {
  if (!payload.length()) return "";
  JsonDocument request;
  if (deserializeJson(request, payload)) return "";
  return request["eventId"].as<String>();
}

String& outboxFront() {
  return outbox[outboxHead];
}

// 読取りイベントをNVSへ保存してからRAM上のFIFOへ反映する。
bool enqueueOutbox(const String& payload) {
  return appendDurableOutbox(outbox, outboxHead, outboxCount, payload,
                             kOutboxCapacity, kPayloadCapacity, storage);
}

// サーバー保存確認後だけ先頭イベントを削除する。
bool dequeueOutbox() {
  return removeDurableOutbox(outbox, outboxHead, outboxCount, kOutboxCapacity, storage);
}

// 起動時にNVSから未送信イベントを復元し、旧単一イベント形式も移行する。
bool loadOutbox() {
  if (!restoreDurableOutbox(outbox, outboxHead, outboxCount, kOutboxCapacity, storage)) return false;

  // 旧版の単一pending形式も、未確認イベントを失わずFIFOへ移行する。
  const String legacy = storage.getString("pending", "");
  if (legacy.length()) {
    bool alreadyQueued = false;
    const String legacyId = eventIdFromPayload(legacy);
    for (uint8_t i = 0; i < outboxCount; ++i) {
      const uint8_t slot = (outboxHead + i) % kOutboxCapacity;
      alreadyQueued |= eventIdFromPayload(outbox[slot]) == legacyId;
    }
    if (!alreadyQueued && !enqueueOutbox(legacy)) return false;
    storage.remove("pending");
  }
  return true;
}

// Background transfers never replace a fresh read or rejected scan result.
bool foregroundOwnsDisplay() {
  return presentation.ownsDisplay(view.screen == InventoryScreen::scanning, millis(), kResultHoldMs);
}

void showForeground(InventoryScreen screen, const String& code = "", const String& detail = "") {
  presentation.holdForeground(millis());
  showScreen(screen, code, detail);
}

void beginWifi() {
  if (INVENTORY_CAPTURE_ONLY || strlen(INVENTORY_WIFI_SSID) == 0) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(INVENTORY_WIFI_SSID, INVENTORY_WIFI_PASSWORD);
  lastWifiAttemptAt = millis();
  Serial.println("WiFi: connecting");
}

void serviceWifi() {
  if (INVENTORY_CAPTURE_ONLY || strlen(INVENTORY_WIFI_SSID) == 0) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!ntpStarted) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      ntpStarted = true;
      Serial.println("WiFi: connected");
    }
  } else if (millis() - lastWifiAttemptAt >= kWifiRetryMs) {
    WiFi.begin(INVENTORY_WIFI_SSID, INVENTORY_WIFI_PASSWORD);
    lastWifiAttemptAt = millis();
    Serial.println("WiFi: reconnecting");
  }

  if (wifiVisualState() != renderedWifi) renderCurrent(true);
}

// 電池残量はPMICから低頻度で取得し、値が変わったときだけ画面を更新する。
void serviceBattery() {
  if (!batterySupported || millis() - lastBatterySampleAt < kBatteryRefreshMs) return;
  lastBatterySampleAt = millis();
  const int32_t level = M5.Power.getBatteryLevel();
  const int16_t next = level < 0 ? -1 : level > 100 ? 100 : level;
  if (next != batteryPercent) {
    batteryPercent = next;
    renderCurrent(true);
  }
}

// Apps Script応答が同じeventIdの保存確認を含むか厳密に判定する。
ReplyState replyState(int httpCode, const String& reply, const String& eventId) {
  JsonDocument response;
  if (deserializeJson(response, reply)) return ReplyState::invalid;
  return isVerifiedSaveAckJson(httpCode, response, eventId.c_str())
    ? ReplyState::storageVerified : ReplyState::invalid;
}

// Apps Script特有のPOSTリダイレクトをたどり、ContentServiceのJSONを取得する。
ReplyState postOnce(const String& body, const String& eventId) {
  WiFiClientSecure postTls;
  postTls.setCACert(INVENTORY_ROOT_CA);
  HTTPClient postHttp;
  postHttp.setTimeout(15000);
  postHttp.setReuse(false);
  postHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* headers[] = {"Location"};
  postHttp.collectHeaders(headers, 1);
  if (!postHttp.begin(postTls, INVENTORY_ENDPOINT)) return ReplyState::invalid;
  postHttp.addHeader("Content-Type", "application/json");
  int httpCode = postHttp.POST(body);
  Serial.printf("POST status: %d\n", httpCode);
  String reply;
  if (httpCode == 302 || httpCode == 303) {
    const String location = postHttp.header("Location");
    postHttp.end();
    if (!location.startsWith("https://script.googleusercontent.com/")) {
      return ReplyState::invalid;
    }

    // リダイレクトが返る時点でApps Script側の書込み処理は実行済み。
    // ESP32-S3ではPOST接続の再利用時にContentService応答を失うことがあったため、
    // 確認GETごとに新しいTLS/HTTP接続を作る。
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
      WiFiClientSecure redirectTls;
      redirectTls.setCACert(INVENTORY_ROOT_CA);
      HTTPClient redirectHttp;
      redirectHttp.setTimeout(12000);
      redirectHttp.setReuse(false);
      if (redirectHttp.begin(redirectTls, location)) {
        httpCode = redirectHttp.GET();
        Serial.printf("Confirm GET %u status: %d\n", attempt + 1, httpCode);
        if (httpCode == 200) reply = redirectHttp.getString();
        redirectHttp.end();
        const ReplyState state = replyState(httpCode, reply, eventId);
        if (state != ReplyState::invalid) return state;
        if (httpCode == 200) return ReplyState::invalid;
      }
      delay(250 * (attempt + 1));
    }
    return ReplyState::invalid;
  }
  if (httpCode == 200) reply = postHttp.getString();
  postHttp.end();
  return replyState(httpCode, reply, eventId);
}

// 保存済みのJSONへ送信時だけDEVICE_KEYを追加し、結果を単純な状態へ変換する。
SendOutcome sendPayload(const String& payload) {
  if (!transportReady()) return SendOutcome::notReady;

  JsonDocument request;
  if (deserializeJson(request, payload)) return SendOutcome::notConfirmed;
  const String eventId = request["eventId"].as<String>();
  request["key"] = INVENTORY_DEVICE_KEY;
  String body;
  serializeJson(request, body);

  const ReplyState state = postOnce(body, eventId);
  Serial.printf("Ledger confirmation: %s\n",
                state == ReplyState::storageVerified ? "verified" : "no-proof");
  return state == ReplyState::storageVerified ? SendOutcome::saved
                                               : SendOutcome::notConfirmed;
}

// HTTPSの待ち時間をメインループから分離するFreeRTOSワーカー。
void networkWorker(void*) {
  SendJob job{};
  while (true) {
    if (xQueueReceive(sendJobs, &job, portMAX_DELAY) != pdTRUE) continue;
    const String payload(job.payload);
    SendResult result{};
    result.outcome = sendPayload(payload);
    strlcpy(result.eventId, eventIdFromPayload(payload).c_str(), sizeof(result.eventId));
    xQueueSend(sendResults, &result, portMAX_DELAY);
  }
}

void showPending(const char* suffix) {
  if (foregroundOwnsDisplay()) { renderCurrent(true); return; }
  const String code = outboxCount ? codeFromPayload(outboxFront()) : "";
  if (sendError.length()) showScreen(InventoryScreen::error, code, sendError);
  else showScreen(InventoryScreen::pending, code, suffix);
}

// 再送時刻と通信準備を確認し、FIFO先頭をネットワークタスクへ渡す。
void startNextSend() {
  if (INVENTORY_CAPTURE_ONLY || sendInFlight || !outboxCount) return;
  if (static_cast<int32_t>(millis() - nextPendingRetryAt) < 0) return;
  if (!transportReady()) {
    showPending(WiFi.status() == WL_CONNECTED ? "SYNC TIME" : "WAIT WIFI");
    nextPendingRetryAt = millis() + kPendingRetryMs;
    return;
  }

  SendJob job{};
  const String payload = outboxFront();
  if (payload.length() >= sizeof(job.payload)) {
    sendError = "QUEUE DATA";
    nextPendingRetryAt = millis() + kPendingRetryMs;
    showPending("AUTO RETRY");
    return;
  }
  strlcpy(job.payload, payload.c_str(), sizeof(job.payload));
  if (xQueueSend(sendJobs, &job, 0) != pdTRUE) return;
  sendInFlight = true;
  presentation.dispatched(outboxCount);
  if (!foregroundOwnsDisplay()) {
    showScreen(InventoryScreen::sending, codeFromPayload(payload));
  } else {
    renderCurrent(true);
  }
  Serial.printf("Sending oldest queued scan; queue=%u\n", outboxCount);
}

// ワーカーの結果を受け、確認成功時だけFIFOを進める。
void serviceSendResult() {
  if (!sendInFlight) return;
  SendResult result{};
  if (xQueueReceive(sendResults, &result, 0) != pdTRUE) return;
  sendInFlight = false;
  if (!outboxCount || eventIdFromPayload(outboxFront()) != result.eventId) {
    sendError = "QUEUE ORDER";
    nextPendingRetryAt = millis() + kPendingRetryMs;
    showPending("AUTO RETRY");
    Serial.println("Queue order error; nothing removed");
    return;
  }

  const String savedCode = codeFromPayload(outboxFront());
  if (result.outcome == SendOutcome::saved) {
    if (!dequeueOutbox()) {
      sendError = "LOCAL STORAGE";
      showPending("AUTO RETRY");
      Serial.println("Acknowledged scan retained because queue metadata could not be updated");
      nextPendingRetryAt = millis() + kPendingRetryMs;
      return;
    }
    sendError = "";
    presentation.confirmedRemoval(outboxCount, sendInFlight);
    const bool currentEvent = foregroundEventId == result.eventId;
    if (currentEvent && !presentation.rejected && view.screen != InventoryScreen::scanning) {
      showForeground(InventoryScreen::saved, savedCode);
    } else if (!foregroundOwnsDisplay()) {
      showScreen(InventoryScreen::saved, savedCode);
    } else {
      renderCurrent(true);
    }
    Serial.printf("Sheet confirmed; remaining queue=%u\n", outboxCount);
    nextPendingRetryAt = millis() + (outboxCount ? kNextQueueSendDelayMs : 0);
    return;
  }

  nextPendingRetryAt = millis() + kPendingRetryMs;
  showPending(result.outcome == SendOutcome::notReady ? "WAIT WIFI" : "AUTO RETRY");
  Serial.println("Save not confirmed; oldest queued scan retained for automatic retry");
}

// デコード結果を検証し、固有eventId付きJSONとして不揮発FIFOへ登録する。
void handleDecoded(uint16_t length) {
  if (length == 0 || length > 512) return;

  uint8_t buffer[513] = {};
  scanner.getDecodeData(buffer, length);
  if (isReaderSuccessReply(buffer, length)) {
    Serial.printf("Reader command replies consumed; frames=%u\n", length / 5);
    return;
  }
  scanHandledForPress = true;

  printCaptureMetadata(buffer, length);
  currentCode = safeDisplayText(buffer, length);

  if (INVENTORY_CAPTURE_ONLY) {
    showForeground(InventoryScreen::captured, currentCode);
    Serial.println("Captured locally");
    return;
  }

  for (uint16_t i = 0; i < length; ++i) {
    if (buffer[i] < 32 || buffer[i] == 127) {
      presentation.reject(millis());
      showForeground(InventoryScreen::error, currentCode, "CONTROL BYTES");
      Serial.println("Control bytes rejected");
      return;
    }
  }

  char eventId[33];
  snprintf(eventId, sizeof(eventId), "%08lx%08lx%08lx%08lx",
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  JsonDocument request;
  request["version"] = 1;
  request["eventId"] = eventId;
  request["code"] = reinterpret_cast<char*>(buffer);
  String payload;
  serializeJson(request, payload);
  if (outboxCount >= kOutboxCapacity) {
    presentation.reject(millis());
    showForeground(InventoryScreen::full, currentCode, "NOT ACCEPTED");
    Serial.println("Queue full; decoded scan could not be accepted");
    return;
  }
  if (!enqueueOutbox(payload)) {
    presentation.reject(millis());
    showForeground(InventoryScreen::error, currentCode, "LOCAL STORAGE");
    Serial.println("Storage error; decoded scan could not be queued");
    return;
  }
  foregroundEventId = eventId;
  presentation.accepted(millis());
  showForeground(InventoryScreen::queued, currentCode,
                 transportReady() ? "AUTO RETRY" : "WAIT WIFI");
  Serial.printf("Scan stored in durable queue; queue=%u\n", outboxCount);
  nextPendingRetryAt = millis();
}

void onTriggerPressed() {
  triggerHeld = true;
  scanHandledForPress = false;
  presentation.trigger(millis());
  foregroundEventId = "";
  showForeground(InventoryScreen::scanning);
  Serial.println("Scanning while QR TRIG is held");
}

void onTriggerReleased() {
  triggerHeld = false;
  if (view.screen == InventoryScreen::scanning && !scanHandledForPress) {
    showForeground(InventoryScreen::noCode);
    Serial.println("Trigger released without decoder data");
  }
}

// TRIG状態とdecoder-readyを短周期で監視し、届いた結果を必ず消費する。
void pollScanner() {
  const uint32_t now = millis();
  if (now - lastScannerPollAt < kScannerPollMs) return;
  lastScannerPollAt = now;

  const uint8_t triggerKey = scanner.getTriggerKeyStatus();
  if (triggerKey <= 1) {
    if (!triggerKeyKnown) {
      lastTriggerKey = triggerKey;
      triggerKeyKnown = true;
      triggerHeld = triggerKey == 0;
    } else if (triggerKey != lastTriggerKey) {
      lastTriggerKey = triggerKey;
      if (triggerKey == 0) onTriggerPressed();
      else onTriggerReleased();
    }
  }

  const uint8_t ready = scanner.getDecodeReadyStatus();
  if (ready == 0 || ready > 2) return;
  const uint16_t length = scanner.getDecodeLength();
  if (length == 0 || length > 512) return;

  // 公式I2C例と同様に、readyになった結果は毎回取得する。データ読取りで
  // decoder-readyが解除されるため、前回のTRIG状態による読み捨ては行わない。
  handleDecoded(length);
}

void serviceScreen() {
  const uint32_t now = millis();
  if (presentation.foregroundActive && !foregroundOwnsDisplay()) {
    presentation.foregroundActive = false;
    if (outboxCount) {
      if (sendInFlight) showScreen(InventoryScreen::sending, codeFromPayload(outboxFront()));
      else showPending(transportReady() ? "AUTO RETRY" : "WAIT WIFI");
    } else if (!presentation.allSavedPending) showReady();
  }
  if (!foregroundOwnsDisplay() && presentation.allSavedPending && allSavesConfirmed(outboxCount, sendInFlight)) {
    presentation.allSavedPending = false;
    showScreen(InventoryScreen::allSaved);
    return;
  }
  if ((view.screen == InventoryScreen::saved || view.screen == InventoryScreen::captured ||
       view.screen == InventoryScreen::allSaved) && now - screenStartedAt >= kResultHoldMs) {
    if (outboxCount) showPending(transportReady() ? "AUTO RETRY" : "WAIT WIFI");
    else showReady();
    return;
  }
  if (view.screen == InventoryScreen::noCode && now - screenStartedAt >= kNoCodeHoldMs) {
    presentation.foregroundActive = false;
    if (outboxCount) showPending(transportReady() ? "AUTO RETRY" : "WAIT WIFI");
    else showReady();
    return;
  }
  const bool animated = view.screen == InventoryScreen::boot ||
                        view.screen == InventoryScreen::scanning ||
                        view.screen == InventoryScreen::sending;
  const bool countdown = view.screen == InventoryScreen::captured;
  const uint32_t refreshMs = countdown ? kCountdownRefreshMs : kAnimationMs;
  if ((animated || countdown) && now - lastRenderAt >= refreshMs) {
    ++animationPhase;
    view.phase = animationPhase;
    renderCurrent(true);
  }
}

// 起動診断には電源・バス状態だけを出力し、コード本文や認証情報を含めない。
#ifndef INVENTORY_READER_DIAGNOSTICS
#define INVENTORY_READER_DIAGNOSTICS 1
#endif

void readerDiagnostic(const char* stage, int32_t value = -1) {
#if INVENTORY_READER_DIAGNOSTICS
  Serial.printf("READER t=%lu stage=%s value=%ld\n",
                static_cast<unsigned long>(millis()), stage, static_cast<long>(value));
#else
  (void)stage;
  (void)value;
#endif
}

// GPIO46は機種によって役割が異なるため、StickS3向けビルドだけに適用する。
#if defined(INVENTORY_STICKS3_BUILD) || defined(ARDUINO_M5STACK_STICKS3)
class StickStartupBoard {
 public:
  explicit StickStartupBoard(m5::M5Unified::config_t config)
      : config_(config), brightness_(M5.Display.getBrightness()) {}

  bool detectStick() {
    // 機種判定中のバックライト負荷を増やさない。復元は電源初期化後に行う。
    M5.Display.setBrightness(0);
    return M5.Display.init() && M5.Display.getBoard() == m5::board_t::board_M5StickS3;
  }

  bool holdIrOff() {
    if (!setIrLow() || gpio_hold_en(GPIO_NUM_46) != ESP_OK) return false;
    readerDiagnostic("ir-held-off");
    return true;
  }

  bool begin() {
    // M5Unified 0.2.22はbegin内でG46をHighにする。保持中は端子のLOWを維持する。
    readerDiagnostic("m5-begin-ir-protected");
    M5.begin(config_);
    return M5.getBoard() == m5::board_t::board_M5StickS3;
  }

  bool releaseIrOff() {
    // 保持解除より先に出力レジスタをLOWへ戻し、解除時の点灯を防ぐ。
    if (!setIrLow() || gpio_hold_dis(GPIO_NUM_46) != ESP_OK) return false;
    const int level = gpio_get_level(GPIO_NUM_46);
    readerDiagnostic("ir-off-released", level);
    return level == 0;
  }

  void restoreBrightness() { M5.Display.setBrightness(brightness_); }

 private:
  m5::M5Unified::config_t config_;
  uint8_t brightness_;

  bool setIrLow() {
    if (gpio_set_level(GPIO_NUM_46, 0) != ESP_OK) return false;
    gpio_config_t pin = {};
    pin.pin_bit_mask = 1ULL << GPIO_NUM_46;
    // 入力も有効にして、消灯レベルを読み戻せるようにする。
    pin.mode = GPIO_MODE_INPUT_OUTPUT;
    pin.pull_up_en = GPIO_PULLUP_DISABLE;
    pin.pull_down_en = GPIO_PULLDOWN_DISABLE;
    pin.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&pin) == ESP_OK && gpio_get_level(GPIO_NUM_46) == 0;
  }
};
#endif

// 復帰要求が未確認の間は、再起動をまたいでも同じ要求を繰り返さない。
// inventoryの保存領域と、以前の電源試行数キーには触れない。
class ReaderResumeGuard {
 public:
  ~ReaderResumeGuard() { if (opened_) nvs_close(handle_); }

  bool readPending(bool& pending) {
    if (!opened_) opened_ = nvs_open("readerboot", NVS_READWRITE, &handle_) == ESP_OK;
    if (!opened_) { readerDiagnostic("boot-guard-read", -1); return false; }
    uint8_t value = 0;
    const auto result = nvs_get_u8(handle_, "exit_pending", &value);
    if (result == ESP_ERR_NVS_NOT_FOUND) value = 0;
    else if (result != ESP_OK || value > 1) {
      readerDiagnostic("boot-guard-read", -1);
      return false;
    }
    pending = value != 0;
    readerDiagnostic("boot-guard-read", value);
    return true;
  }

  bool writePending(bool pending) {
    const bool saved = opened_
        && nvs_set_u8(handle_, "exit_pending", pending ? 1 : 0) == ESP_OK
        && nvs_commit(handle_) == ESP_OK;
    readerDiagnostic(pending ? "boot-exit-reserved" : "boot-exit-cleared", saved ? 1 : -1);
    return saved;
  }

 private:
  nvs_handle_t handle_ = 0;
  bool opened_ = false;
};

class ReaderStartupPort {
 public:
  ReaderStartupPort(int8_t sda, int8_t scl) : sda_(sda), scl_(scl) {}

  inventory::ReaderStartupResult prepare(bool checkPower) {
    showScreen(InventoryScreen::boot, "", "READER START");
    Wire.end();
    if (sda_ < 0 || scl_ < 0 || sda_ == scl_) return inventory::ReaderStartupResult::bus;
    // 非給電中の信号線からの回り込みを避けるため、Highを強制出力しない。
    pinMode(sda_, INPUT);
    pinMode(scl_, INPUT);
    readerDiagnostic("prepare");
    // Unitの起動時モード判定中はI2C通信しない。通常運転の待ち時間には使わない。
    delay(checkPower ? kReaderQuietBootMs : 10);
    if (checkPower && !waitVoltage(1200)) return inventory::ReaderStartupResult::power;
    readerDiagnostic("sda", digitalRead(sda_));
    readerDiagnostic("scl", digitalRead(scl_));
    if (!digitalRead(sda_) || !digitalRead(scl_)) return inventory::ReaderStartupResult::bus;
    Wire.setBufferSize(512);
    Wire.setTimeOut(50);
    if (!Wire.begin(sda_, scl_, 100000U)) return inventory::ReaderStartupResult::i2c;
    readerDiagnostic("wire-ready");
    return inventory::ReaderStartupResult::ready;
  }

  inventory::ReaderStartupResult connect() {
    return inventory::connectReader(*this, guard_);
  }

  bool normalAvailable() {
    const uint8_t result = probe(UNIT_QRCODE_ADDR);
    readerDiagnostic("ack-21", result);
    return result == 0;
  }

  bool bootAvailable() {
    const uint8_t result = probe(inventory::kReaderBootAddress);
    readerDiagnostic("ack-54", result);
    return result == 0;
  }

  void showResume() { showScreen(InventoryScreen::boot, "", "READER RESUME"); }
  uint32_t now() { return millis(); }
  void pause(uint32_t milliseconds) { delay(milliseconds); }

  void requestBootExit() {
    // 公式ブートローダーの通常アプリ移行命令。電源切替・Flash更新は要求しない。
    Wire.beginTransmission(inventory::kReaderBootAddress);
    if (Wire.write(inventory::kReaderBootExitCommand) != 1) {
      readerDiagnostic("boot-exit-write", -1);
      return;
    }
    const uint8_t status = Wire.endTransmission(true);
    readerDiagnostic("boot-exit-write", status);
  }

  void failed(inventory::ReaderStartupResult result) {
    Serial.printf("QR reader startup failed: %s; automatic power cycling disabled\n",
                  inventory::readerStartupError(result));
  }

 private:
  int8_t sda_;
  int8_t scl_;
  ReaderResumeGuard guard_;
  bool waitVoltage(uint32_t timeoutMs) {
    const uint32_t start = millis();
    uint8_t stable = 0;
    while (millis() - start < timeoutMs) {
      uint8_t bytes[2] = {};
      // 戻り値のない電圧APIで0mVを通信成功と誤認しないよう、転送結果も確認する。
      const bool valid = M5.In_I2C.readRegister(0x6E, 0x26, bytes, sizeof(bytes), 100000U);
      const uint16_t mv = static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
      readerDiagnostic("power-on-mv", valid ? mv : -1);
      const bool inRange = inventory::readerVoltageAcceptable(valid, mv);
      stable = inRange ? stable + 1 : 0;
      if (stable >= 3) return true;
      delay(100);
    }
    return false;
  }

  uint8_t probe(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission();
  }

  bool readByte(uint16_t reg, uint8_t& value) {
    Wire.beginTransmission(UNIT_QRCODE_ADDR);
    Wire.write(static_cast<uint8_t>(reg));
    Wire.write(static_cast<uint8_t>(reg >> 8));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<uint8_t>(UNIT_QRCODE_ADDR), static_cast<uint8_t>(1)) != 1 ||
        Wire.available() != 1) return false;
    value = static_cast<uint8_t>(Wire.read());
    return true;
  }

  bool writeByte(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(UNIT_QRCODE_ADDR);
    Wire.write(static_cast<uint8_t>(reg));
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }

 public:
  inventory::ReaderStartupResult configure() {
    uint8_t version = 0;
    uint8_t mode = 0;
    uint8_t key = 0;
    if (!readByte(FIRMWARE_VERSION_REG, version) || version == 0 || version == 0xFF)
      return inventory::ReaderStartupResult::configuration;
    readerDiagnostic("firmware", version);
    // 初回レジスタ通信はUnit内部のUARTも初期化する。設定コマンドが重ならないよう間を置く。
    if (!writeByte(UNIT_QRCODE_TRIGGER_MODE_REG, 1)) return inventory::ReaderStartupResult::configuration;
    delay(20);
    if (!writeByte(UNIT_QRCODE_TRIGGER_MODE_REG, 1)) return inventory::ReaderStartupResult::configuration;
    delay(20);
    if (!writeByte(UNIT_QRCODE_TRIGGER_REG, 0)) return inventory::ReaderStartupResult::configuration;
    delay(20);
    if (!readByte(UNIT_QRCODE_TRIGGER_MODE_REG, mode) || mode != 1 ||
        !readByte(UNIT_QRCODE_TRIGGER_KEY_REG, key) || key > 1)
      return inventory::ReaderStartupResult::configuration;
    // ライブラリの通信先を設定する。Wireは上で初期化・検証済み。
    if (!scanner.begin(&Wire, UNIT_QRCODE_ADDR, sda_, scl_, 100000U))
      return inventory::ReaderStartupResult::i2c;
    lastTriggerKey = key;
    triggerKeyKnown = true;
    triggerHeld = key == 0;
    readerDiagnostic("manual-mode", mode);
    readerDiagnostic("trigger-key", key);
    return inventory::ReaderStartupResult::ready;
  }
};

void setup() {
  Serial.begin(115200);
  readerDiagnostic("reset-reason", esp_reset_reason());
  auto config = M5.config();
  // 本体初期化時に給電を有効にし、その後は読取器の電源を入れ直さない。
  config.output_power = true;
  config.internal_mic = false;
  config.internal_spk = false;
  config.internal_imu = false;
  config.internal_rtc = false;
  config.external_display_value = 0;
#if defined(INVENTORY_STICKS3_BUILD) || defined(ARDUINO_M5STACK_STICKS3)
  StickStartupBoard board(config);
  const auto powerResult = inventory::beginStickWithIrOff(board);
  board.restoreBrightness();
  if (powerResult != inventory::StickPowerStartupResult::ready) {
    const char* reason = inventory::stickPowerStartupError(powerResult);
    Serial.printf("Device startup failed: %s\n", reason);
    if (M5.Display.getBoard() != m5::board_t::board_unknown && inventoryDisplay.begin()) {
      showScreen(InventoryScreen::error, "", reason);
    }
    while (true) delay(100);
  }
#else
  M5.begin(config);
#endif
  readerDiagnostic("m5-ready", M5.getBoard());
  batterySupported = M5.getBoard() == m5::board_t::board_M5StickS3;
  if (batterySupported) {
    const int32_t level = M5.Power.getBatteryLevel();
    batteryPercent = level < 0 ? -1 : level > 100 ? 100 : level;
    lastBatterySampleAt = millis();
  }

  if (!inventoryDisplay.begin()) Serial.println("Display sprite allocation failed");
  showScreen(InventoryScreen::boot);

  if (!storage.begin("inventory", false)) {
    showScreen(InventoryScreen::error, "", "LOCAL STORAGE");
    while (true) delay(100);
  }
  if (!loadOutbox()) {
    showScreen(InventoryScreen::error, "", "QUEUE STORAGE");
    while (true) delay(100);
  }
  Serial.printf("Durable queue restored; count=%u\n", outboxCount);

  sendJobs = xQueueCreate(1, sizeof(SendJob));
  sendResults = xQueueCreate(1, sizeof(SendResult));
  if (!sendJobs || !sendResults ||
      xTaskCreate(networkWorker, "inventory-net", 8192, nullptr, 1, nullptr) != pdPASS) {
    showScreen(InventoryScreen::error, "", "NETWORK TASK");
    while (true) delay(100);
  }

  const int8_t sda = M5.getPin(m5::pin_name_t::port_a_sda);
  const int8_t scl = M5.getPin(m5::pin_name_t::port_a_scl);
  Serial.printf("PORT.A I2C pins: SDA=%d SCL=%d\n", sda, scl);
  ReaderStartupPort readerPort(sda, scl);
  const auto readerResult = inventory::startReader(readerPort, batterySupported);
  if (readerResult != inventory::ReaderStartupResult::ready) {
    showScreen(InventoryScreen::error, "", inventory::readerStartupError(readerResult));
    while (true) delay(100);
  }
  Serial.println("QR reader ready");

  beginWifi();
  if (outboxCount) {
    showPending("RESTORED");
    nextPendingRetryAt = millis();
  } else {
    showReady();
  }
}

void loop() {
  serviceWifi();
  serviceBattery();
  pollScanner();
  serviceSendResult();
  startNextSend();
  serviceScreen();
  delay(5);
}
