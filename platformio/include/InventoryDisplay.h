#pragma once

#include <M5Unified.h>
#include <cstdio>
#include <cstring>

/*
 * AtomS3 / StickS3共通の画面描画。
 * 1フレームをM5Canvasへ描いてから転送し、部分更新のちらつきを防ぐ。
 * 画面高さ180px未満ではコンパクト配置、それ以上では縦長配置を使う。
 */

enum class WifiVisualState {
  offline,
  connecting,
  online,
};

enum class InventoryScreen {
  boot,
  ready,
  scanning,
  sending,
  saved,
  allSaved,
  full,
  captured,
  queued,
  pending,
  noCode,
  error,
};

// 通信・保存処理から描画を分離するため、画面に必要な情報だけを保持する。
struct InventoryView {
  InventoryScreen screen = InventoryScreen::boot;
  WifiVisualState wifi = WifiVisualState::offline;
  bool hasBattery = false;
  int16_t batteryPercent = -1;
  String code;
  String detail;
  uint8_t unsent = 0;
  uint8_t batchTotal = 0;
  uint8_t batchSaved = 0;
  uint8_t phase = 0;
  uint16_t remainingMs = 0;
};

class InventoryDisplay {
 public:
  InventoryDisplay() : sprite_(&M5.Display) {}

  bool begin() {
    const int16_t width = M5.Display.width();
    const int16_t height = M5.Display.height();
    if (width <= 0 || height <= 0) return false;

    sprite_.setColorDepth(16);
    ready_ = sprite_.createSprite(width, height) != nullptr;
    if (!ready_) return false;

    sprite_.setTextWrap(false);
    sprite_.setTextDatum(middle_center);
    return true;
  }

  void render(const InventoryView& view) {
    if (!ready_) return;

    compactCodePanel_ = view.code.length() || view.screen == InventoryScreen::full;
    stickDense_ = sprite_.height() >= 180 && (compactCodePanel_ || view.batchTotal > 0);
    beginFrame(view, accentFor(view.screen));
    switch (view.screen) {
      case InventoryScreen::boot:
        drawActivityIcon(accentFor(view.screen), view.phase);
        drawStatus("STARTING", view.detail.length() ? view.detail.c_str() : "INITIALIZING");
        break;
      case InventoryScreen::ready:
        drawScanFrame(accentFor(view.screen));
        drawStatus("READY", sprite_.height() >= 180 ? "HOLD TRIG" : "PRESS & HOLD TRIG");
        break;
      case InventoryScreen::scanning:
        drawActivityIcon(accentFor(view.screen), view.phase);
        drawStatus("SCANNING", "HOLD ON CODE");
        break;
      case InventoryScreen::sending:
        drawUploadIcon(accentFor(view.screen), view.phase);
        drawStatus(view.batchTotal > 1 ? "SYNCING" : "SENDING", "TO SCANS");
        drawCodeOrProgress(view);
        break;
      case InventoryScreen::saved:
        drawCheckIcon(accentFor(view.screen));
        drawStatus("SAVED", view.unsent < 16 ? "READY FOR NEXT" : "STORAGE FULL");
        drawCodePanel(view.code);
        break;
      case InventoryScreen::allSaved:
        drawCheckIcon(accentFor(view.screen));
        drawStatus("ALL SAVED", "READY FOR NEXT");
        break;
      case InventoryScreen::full:
        drawErrorIcon(accentFor(view.screen));
        drawStatus(view.unsent >= 16 ? "STORAGE FULL" : "SCAN REJECTED",
                   view.unsent >= 16 ? (sprite_.height() >= 180 ? "SCAN REJECTED" : "16/16 NOT ACCEPTED") : "SPACE AVAILABLE");
        drawRejectionPanel(view.unsent >= 16);
        break;
      case InventoryScreen::captured:
        drawCheckIcon(accentFor(view.screen));
        drawStatus("CAPTURED", "LOCAL MODE");
        drawCodePanel(view.code);
        drawCountdown(view.remainingMs, accentFor(view.screen));
        break;
      case InventoryScreen::queued:
        drawPendingIcon(accentFor(view.screen));
        drawStatus("STORED", sprite_.height() >= 180
          ? (view.wifi == WifiVisualState::online ? "AUTO RETRY" : "WAIT WIFI")
          : (view.wifi == WifiVisualState::online ? "DEVICE / AUTO RETRY" : "DEVICE / WAIT WIFI"));
        drawCodePanel(view.code);
        break;
      case InventoryScreen::pending:
        drawPendingIcon(accentFor(view.screen));
        drawStatus("STORED", view.detail.length() ? view.detail.c_str() : "AUTO RETRY");
        drawCodeOrProgress(view);
        break;
      case InventoryScreen::noCode:
        drawPendingIcon(accentFor(view.screen));
        drawStatus("NO CODE", sprite_.height() >= 180 ? "HOLD TRIG AGAIN" : "TRY QR TRIG AGAIN");
        break;
      case InventoryScreen::error:
        drawErrorIcon(accentFor(view.screen));
        drawStatus("ERROR", view.detail.length() ? view.detail.c_str() : "CHECK DEVICE");
        if (view.code.length()) drawCodePanel(view.code);
        break;
    }
    if (sprite_.height() >= 180 && view.batchTotal) drawProgress(view, 159);
    sprite_.pushSprite(0, 0);
  }

 private:
  uint16_t rgb(uint8_t red, uint8_t green, uint8_t blue) const {
    return sprite_.color565(red, green, blue);
  }

  uint16_t accentFor(InventoryScreen screen) const {
    switch (screen) {
      case InventoryScreen::saved:
      case InventoryScreen::allSaved:
      case InventoryScreen::captured:
        return rgb(42, 231, 166);
      case InventoryScreen::queued:
      case InventoryScreen::pending:
      case InventoryScreen::noCode:
        return rgb(255, 184, 76);
      case InventoryScreen::error:
      case InventoryScreen::full:
        return rgb(255, 83, 112);
      case InventoryScreen::boot:
      case InventoryScreen::ready:
      case InventoryScreen::scanning:
      case InventoryScreen::sending:
      default:
        return rgb(0, 207, 255);
    }
  }

  void beginFrame(const InventoryView& view, uint16_t accent) {
    const int16_t width = sprite_.width();
    const int16_t height = sprite_.height();
    const int16_t headerHeight = height >= 180 ? 31 : 24;
    const uint16_t background = rgb(5, 13, 24);
    const uint16_t grid = rgb(10, 27, 43);
    const uint16_t header = rgb(9, 24, 38);

    sprite_.fillSprite(background);
    for (int16_t x = -height; x < width; x += 18) {
      sprite_.drawLine(x, height - 1, x + height, 0, grid);
    }
    sprite_.fillRect(0, 0, width, headerHeight, header);
    sprite_.fillRect(0, headerHeight - 2, width, 2, accent);

    sprite_.setFont(&fonts::efontJA_10_b);
    sprite_.setTextSize(1);
    sprite_.setTextDatum(middle_left);
    sprite_.setTextColor(rgb(232, 242, 250), header);
    if (height >= 180) {
      drawWifiBadge(view.wifi, headerHeight, view.hasBattery);
      if (view.hasBattery) drawBatteryBadge(view.batteryPercent, headerHeight);
      sprite_.setTextDatum(middle_center);
      sprite_.setFont(&fonts::efontJA_16_b);
      sprite_.setTextColor(rgb(232, 242, 250));
      char label[16];
      snprintf(label, sizeof(label), "UNSENT %u", view.unsent);
      sprite_.drawString(label, width / 2, 47);
    } else {
      drawWifiBadge(view.wifi, headerHeight, false);
      sprite_.setTextDatum(middle_right);
      sprite_.setFont(&fonts::efontJA_12_b);
      sprite_.setTextColor(rgb(232, 242, 250), header);
      char label[16];
      snprintf(label, sizeof(label), "UNSENT %u", view.unsent);
      sprite_.drawString(label, width - 5, headerHeight / 2 - 1);
    }
    sprite_.setTextDatum(middle_center);
  }

  void drawWifiBadge(WifiVisualState state, int16_t headerHeight, bool hasBattery) {
    if (sprite_.height() >= 180) {
      const uint16_t badge = rgb(14, 35, 51);
      const uint16_t color = state == WifiVisualState::online ? rgb(42, 231, 166)
                             : state == WifiVisualState::connecting ? rgb(255, 184, 76)
                                                                      : rgb(159, 179, 195);
      sprite_.fillRoundRect(6, 4, 31, 23, 4, badge);
      sprite_.drawLine(11, 13, 15, 9, color);
      sprite_.drawLine(15, 9, 21, 7, color);
      sprite_.drawLine(21, 7, 27, 9, color);
      sprite_.drawLine(27, 9, 31, 13, color);
      sprite_.drawLine(14, 16, 17, 13, color);
      sprite_.drawLine(17, 13, 21, 12, color);
      sprite_.drawLine(21, 12, 25, 13, color);
      sprite_.drawLine(25, 13, 28, 16, color);
      sprite_.drawLine(17, 19, 21, 17, color);
      sprite_.drawLine(21, 17, 25, 19, color);
      sprite_.fillCircle(21, 22, 1, color);
      if (state == WifiVisualState::offline) {
        sprite_.drawLine(11, 24, 32, 6, rgb(235, 244, 249));
      }
      return;
    }
    const int16_t width = sprite_.width();
    const int16_t boxWidth = hasBattery ? 38 : 27;
    const int16_t x = hasBattery ? width - 94 : 4;
    const int16_t y = 2;
    const int16_t boxHeight = headerHeight - 5;
    const uint16_t badge = rgb(14, 35, 51);
    const uint16_t color = state == WifiVisualState::online
                               ? rgb(42, 231, 166)
                               : state == WifiVisualState::connecting ? rgb(255, 184, 76)
                                                                       : rgb(99, 119, 137);
    sprite_.fillRoundRect(x, y, boxWidth, boxHeight, 4, badge);
    const int16_t cx = x + boxWidth / 2;
    sprite_.drawLine(cx - 9, y + 9, cx - 6, y + 6, color);
    sprite_.drawLine(cx - 6, y + 6, cx, y + 4, color);
    sprite_.drawLine(cx, y + 4, cx + 6, y + 6, color);
    sprite_.drawLine(cx + 6, y + 6, cx + 9, y + 9, color);
    sprite_.drawLine(cx - 6, y + 12, cx - 3, y + 9, color);
    sprite_.drawLine(cx - 3, y + 9, cx, y + 8, color);
    sprite_.drawLine(cx, y + 8, cx + 3, y + 9, color);
    sprite_.drawLine(cx + 3, y + 9, cx + 6, y + 12, color);
    sprite_.drawLine(cx - 3, y + 14, cx, y + 12, color);
    sprite_.drawLine(cx, y + 12, cx + 3, y + 14, color);
    sprite_.fillCircle(cx, y + 16, 1, color);
    if (state == WifiVisualState::offline) {
      sprite_.drawLine(cx - 10, y + 18, cx + 10, y + 2, rgb(235, 244, 249));
    }
  }

  // 中身全体を残量比で塗り、白い数字に十分なコントラストを残す。
  void drawBatteryBadge(int16_t percent, int16_t headerHeight) {
    const int16_t x = sprite_.width() - 69;
    const int16_t y = (headerHeight - 22) / 2;
    const uint16_t color = percent < 0 ? rgb(99, 119, 137)
                           : percent <= 15 ? rgb(255, 83, 112)
                           : percent <= 30 ? rgb(255, 184, 76)
                                           : rgb(42, 231, 166);
    const uint16_t fill = percent <= 15 ? rgb(188, 65, 79)
                          : percent <= 30 ? rgb(144, 103, 14)
                                          : rgb(13, 128, 98);
    const uint16_t badge = rgb(14, 35, 51);
    sprite_.fillRoundRect(x, y, 61, 22, 3, badge);
    if (percent > 0) {
      const int16_t level = static_cast<int32_t>(59) * (percent > 100 ? 100 : percent) / 100;
      sprite_.fillRoundRect(x + 1, y + 1, 59, 20, 2, fill);
      if (level < 59) sprite_.fillRect(x + 1 + level, y + 1, 59 - level, 20, badge);
    }
    sprite_.drawRoundRect(x, y, 61, 22, 3, color);
    sprite_.fillRect(x + 61, y + 7, 3, 8, color);
    char label[6];
    if (percent < 0) snprintf(label, sizeof(label), "--%%");
    else snprintf(label, sizeof(label), "%d%%", percent > 100 ? 100 : percent);
    sprite_.setFont(&fonts::FreeSansBold9pt7b);
    sprite_.setTextDatum(middle_center);
    sprite_.setTextColor(rgb(242, 248, 252));
    // FreeSansBold9pt's visible glyphs sit above its datum; +12 centers the ink in the 22px body.
    sprite_.drawString(label, x + 30, y + 12);
  }

  // StickS3はヘッダー拡大分だけ本文を移動し、コードと進捗の領域を守る。
  void drawStatus(const char* title, const char* subtitle) {
    const int16_t height = sprite_.height();
    const bool hasCodePanel = height < 180 && compactCodePanel_;
    const int16_t titleY = hasCodePanel ? 60 : (height >= 180 ? (stickDense_ ? 123 : 131) : 77);
    const int16_t subtitleY = height >= 180 ? (stickDense_ ? 143 : 155) : titleY + 17;
    if (height >= 180) {
      sprite_.setFont(&fonts::FreeSansBold9pt7b);
      if (sprite_.textWidth(title) > sprite_.width() - 8) sprite_.setFont(&fonts::efontJA_16_b);
    } else {
      sprite_.setFont(&fonts::FreeSansBold9pt7b);
      if (sprite_.textWidth(title) > sprite_.width() - 8) sprite_.setFont(&fonts::efontJA_16_b);
      if (sprite_.textWidth(title) > sprite_.width() - 8) sprite_.setFont(&fonts::efontJA_14_b);
    }
    sprite_.setTextColor(rgb(242, 248, 252));
    sprite_.drawString(title, sprite_.width() / 2, titleY);
    sprite_.setFont(&fonts::efontJA_12_b);
    if (sprite_.textWidth(subtitle) > sprite_.width() - 8) sprite_.setFont(&fonts::efontJA_10_b);
    sprite_.setTextColor(rgb(145, 174, 193));
    sprite_.drawString(subtitle, sprite_.width() / 2, subtitleY);
  }

  int16_t stickIconY() const { return stickDense_ ? 84 : 91; }

  void drawScanFrame(uint16_t color) {
    const int16_t height = sprite_.height();
    const int16_t cx = sprite_.width() / 2;
    const int16_t cy = height >= 180 ? stickIconY() : 47;
    const int16_t r = height >= 180 ? 25 : 19;
    const int16_t arm = height >= 180 ? 10 : 8;
    sprite_.drawFastHLine(cx - r, cy - r, arm, color);
    sprite_.drawFastVLine(cx - r, cy - r, arm, color);
    sprite_.drawFastHLine(cx + r - arm, cy - r, arm, color);
    sprite_.drawFastVLine(cx + r, cy - r, arm, color);
    sprite_.drawFastHLine(cx - r, cy + r, arm, color);
    sprite_.drawFastVLine(cx - r, cy + r - arm, arm, color);
    sprite_.drawFastHLine(cx + r - arm, cy + r, arm, color);
    sprite_.drawFastVLine(cx + r, cy + r - arm, arm, color);
    sprite_.fillRect(cx - r + 5, cy, (r - 5) * 2, 2, rgb(31, 82, 105));
    sprite_.fillCircle(cx, cy, 3, color);
  }

  void drawActivityIcon(uint16_t color, uint8_t phase) {
    const int16_t height = sprite_.height();
    const int16_t cx = sprite_.width() / 2;
    const int16_t cy = height >= 180 ? stickIconY() : 43;
    const int16_t barWidth = height >= 180 ? 7 : 5;
    const int16_t gap = height >= 180 ? 6 : 5;
    for (int i = 0; i < 3; ++i) {
      const int16_t barHeight = 11 + ((phase + i) % 3) * 5;
      const int16_t x = cx + (i - 1) * (barWidth + gap) - barWidth / 2;
      sprite_.fillRoundRect(x, cy - barHeight / 2, barWidth, barHeight, 2,
                            i == phase % 3 ? color : rgb(25, 71, 91));
    }
  }

  void drawUploadIcon(uint16_t color, uint8_t phase) {
    const int16_t cy = sprite_.height() >= 180 ? stickIconY() : (compactCodePanel_ ? 37 : 39);
    const int16_t cx = sprite_.width() / 2;
    sprite_.drawRoundRect(cx - 20, cy - 10, 40, 21, 7, rgb(42, 79, 99));
    sprite_.drawFastVLine(cx, cy - 15, 21, color);
    sprite_.drawLine(cx, cy - 15, cx - 6, cy - 9, color);
    sprite_.drawLine(cx, cy - 15, cx + 6, cy - 9, color);
    for (int i = 0; i < 3; ++i) {
      sprite_.fillCircle(cx - 8 + i * 8, cy + (sprite_.height() >= 180 ? 17 : 7),
                         i == phase % 3 ? 2 : 1,
                         i == phase % 3 ? color : rgb(42, 79, 99));
    }
  }

  void drawCheckIcon(uint16_t color) {
    const int16_t cy = sprite_.height() >= 180 ? stickIconY() : (compactCodePanel_ ? 37 : 39);
    const int16_t cx = sprite_.width() / 2;
    const int16_t radius = sprite_.height() >= 180 ? 19 : (compactCodePanel_ ? 12 : 14);
    sprite_.fillCircle(cx, cy, radius, rgb(10, 52, 48));
    sprite_.drawCircle(cx, cy, radius, color);
    sprite_.drawLine(cx - 7, cy, cx - 2, cy + 5, color);
    sprite_.drawLine(cx - 2, cy + 5, cx + 8, cy - 6, color);
    sprite_.drawLine(cx - 7, cy + 1, cx - 2, cy + 6, color);
    sprite_.drawLine(cx - 2, cy + 6, cx + 8, cy - 5, color);
  }

  void drawPendingIcon(uint16_t color) {
    const int16_t cy = sprite_.height() >= 180 ? stickIconY() : (compactCodePanel_ ? 37 : 39);
    const int16_t cx = sprite_.width() / 2;
    const int16_t radius = sprite_.height() >= 180 ? 19 : (compactCodePanel_ ? 12 : 14);
    sprite_.drawCircle(cx, cy, radius, color);
    sprite_.drawFastVLine(cx, cy - radius + 5, radius - 3, color);
    sprite_.drawLine(cx, cy, cx + 7, cy + 4, color);
  }

  void drawErrorIcon(uint16_t color) {
    const int16_t cy = sprite_.height() >= 180 ? stickIconY() : (compactCodePanel_ ? 37 : 45);
    const int16_t cx = sprite_.width() / 2;
    sprite_.drawCircle(cx, cy, sprite_.height() >= 180 ? 15 : 13, color);
    sprite_.fillRect(cx - 1, cy - 8, 3, 11, color);
    sprite_.fillCircle(cx, cy + 8, 2, color);
  }

  String ellipsize(const String& value, int16_t maxWidth) {
    if (sprite_.textWidth(value) <= maxWidth) return value;
    String result = value;
    while (result.length() > 1 && sprite_.textWidth(result + "...") > maxWidth) {
      result.remove(result.length() - 1);
    }
    return result + "...";
  }

  // 長いコードは中央で2行に分け、各行だけを表示幅に合わせて省略する。
  void drawCodePanel(const String& code) {
    if (!code.length()) return;
    const int16_t width = sprite_.width();
    const int16_t height = sprite_.height();
    const int16_t panelY = height >= 180 ? height - 72 : height - 45;
    const int16_t panelHeight = height - panelY - 5;
    const uint16_t panel = rgb(10, 27, 43);
    const uint16_t border = rgb(26, 58, 78);
    sprite_.fillRoundRect(6, panelY, width - 12, panelHeight, 6, panel);
    sprite_.drawRoundRect(6, panelY, width - 12, panelHeight, 6, border);
    sprite_.setFont(&fonts::efontJA_10_b);
    const bool compact = height < 180;
    if (!compact) {
      sprite_.setTextDatum(top_left);
      sprite_.setTextColor(rgb(80, 129, 157), panel);
      sprite_.drawString("CODE", 11, panelY + 3);
    }

    // ビットマップフォントの拡大では輪郭が崩れるため、12ptの太字を等倍で使う。
    sprite_.setFont(&fonts::FreeSansBold12pt7b);
    sprite_.setTextSize(1);
    sprite_.setTextColor(rgb(235, 244, 249), panel);
    sprite_.setTextDatum(middle_center);
    const int16_t maxWidth = width - 24;
    const int16_t codeTop = compact ? panelY : panelY + 14;
    const int16_t codeHeight = height - 5 - codeTop;
    if (sprite_.textWidth(code) <= maxWidth) {
      sprite_.drawString(code, width / 2, codeTop + codeHeight / 2);
      sprite_.setFont(&fonts::efontJA_10_b);
      return;
    }

    const size_t split = code.length() / 2;
    const String first = ellipsize(code.substring(0, split), maxWidth);
    const String second = ellipsize(code.substring(split), maxWidth);
    sprite_.drawString(first, width / 2, codeTop + codeHeight / 4);
    sprite_.drawString(second, width / 2, codeTop + codeHeight * 3 / 4);
    sprite_.setFont(&fonts::efontJA_10_b);
  }

  void drawProgress(const InventoryView& view, int16_t y) {
    char label[24];
    snprintf(label, sizeof(label), "%u/%u SAVED", view.batchSaved, view.batchTotal);
    sprite_.setFont(sprite_.height() >= 180 ? &fonts::efontJA_16_b : &fonts::efontJA_14_b);
    sprite_.setTextDatum(middle_center);
    sprite_.setTextColor(rgb(232, 242, 250));
    sprite_.drawString(label, sprite_.width() / 2, y);
  }

  void drawCodeOrProgress(const InventoryView& view) {
    if (sprite_.height() < 180 && view.batchTotal) drawProgress(view, 103);
    else drawCodePanel(view.code);
  }

  void drawRejectionPanel(bool full) {
    const bool compact = sprite_.height() < 180;
    sprite_.setFont(&fonts::efontJA_14_b);
    sprite_.setTextColor(rgb(255, 184, 76));
    sprite_.drawString("SCAN AGAIN", sprite_.width() / 2, compact ? 98 : 184);
    sprite_.setFont(&fonts::efontJA_10_b);
    sprite_.drawString(full ? "AFTER SPACE FREES" : "PRESS & HOLD TRIG",
                       sprite_.width() / 2, compact ? 115 : 205);
  }

  void drawCountdown(uint16_t remainingMs, uint16_t color) {
    const int16_t width = sprite_.width();
    const int16_t barWidth = static_cast<int32_t>(width - 12) * remainingMs / 5000;
    sprite_.fillRoundRect(6, sprite_.height() - 3, barWidth, 2, 1, color);
  }

  M5Canvas sprite_;
  bool ready_ = false;
  bool compactCodePanel_ = false;
  bool stickDense_ = false;
};
