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
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <time.h>
#include "secrets.h"

namespace {
constexpr uint32_t kResultHoldMs = 5000;
constexpr uint32_t kNoCodeHoldMs = 1800;
constexpr uint32_t kWifiRetryMs = 10000;
constexpr uint32_t kPendingRetryMs = 10000;
constexpr uint32_t kNextQueueSendDelayMs = 800;
constexpr uint32_t kScannerPollMs = 10;
constexpr uint32_t kScannerStartupRetryMs = 400;
constexpr uint8_t kScannerStartupAttempts = 10;
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

void setup() {
  auto config = M5.config();
  config.output_power = true;
  M5.begin(config);
  Serial.begin(115200);
  batterySupported = M5.getBoard() == m5::board_t::board_M5StickS3;
  if (batterySupported) {
    const int32_t level = M5.Power.getBatteryLevel();
    batteryPercent = level < 0 ? -1 : level > 100 ? 100 : level;
    lastBatterySampleAt = millis();
  }
  bool externalPower = M5.Power.getExtOutput();
  Serial.printf("PORT.A external power after startup: %s\n", externalPower ? "ON" : "OFF");
  if (!externalPower) {
    M5.Power.setExtOutput(true);
    delay(200);
    externalPower = M5.Power.getExtOutput();
    Serial.printf("PORT.A external power after retry: %s\n", externalPower ? "ON" : "OFF");
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
  bool scannerReady = scanner.begin(&Wire, UNIT_QRCODE_ADDR, sda, scl, 100000U);
  // 外部5Vの立ち上がりが遅い場合、最初のI2C応答だけで故障と判定しない。
  for (uint8_t attempt = 1; !scannerReady && attempt < kScannerStartupAttempts; ++attempt) {
    delay(kScannerStartupRetryMs);
    Wire.beginTransmission(UNIT_QRCODE_ADDR);
    const uint8_t i2cStatus = Wire.endTransmission();
    scannerReady = i2cStatus == 0;
    Serial.printf("QR reader startup probe %u/%u: I2C status=%u\n",
                  attempt + 1, kScannerStartupAttempts, i2cStatus);
  }
  if (!scannerReady) {
    Serial.print("PORT.A responding I2C addresses:");
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", address);
    }
    Serial.println();
    Serial.println("QR reader unavailable on PORT.A; check Grove cable and I2C mode");
    showScreen(InventoryScreen::error, "", "QR READER");
    while (true) delay(100);
  }
  Serial.println("QR reader ready");
  Wire.setBufferSize(512);
  scanner.setTriggerMode(MANUAL_SCAN_MODE);
  scanner.setDecodeTrigger(false);

  const uint8_t initialTriggerKey = scanner.getTriggerKeyStatus();
  if (initialTriggerKey <= 1) {
    lastTriggerKey = initialTriggerKey;
    triggerKeyKnown = true;
    triggerHeld = initialTriggerKey == 0;
  }

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
