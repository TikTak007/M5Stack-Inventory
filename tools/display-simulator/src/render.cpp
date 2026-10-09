/*
 * InventoryDisplay.hをM5GFXのSDLバックエンドで描画する。
 * 各画面状態をPPM画像として保存し、実機へ書き込まずに表示を確認できる。
 */

#include <M5GFX.h>
#include <SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "InventoryDisplay.h"

M5UnifiedHostAdapter M5;

namespace {

void writePpm(const std::filesystem::path& path) {
  const int width = M5.Display.width();
  const int height = M5.Display.height();
  std::ofstream stream(path, std::ios::binary);
  stream << "P6\n" << width << " " << height << "\n255\n";
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto pixel = M5.Display.readPixelRGB(x, y);
      const char rgb[3] = {
          static_cast<char>(pixel.R8()),
          static_cast<char>(pixel.G8()),
          static_cast<char>(pixel.B8()),
      };
      stream.write(rgb, sizeof(rgb));
    }
  }
}

void capture(InventoryDisplay& display, const std::filesystem::path& dir,
             const char* name, InventoryScreen screen, WifiVisualState wifi,
             const char* code = "", const char* detail = "", uint8_t phase = 0,
             uint8_t unsent = 0, uint8_t saved = 0, uint8_t total = 0, int16_t battery = 82) {
  InventoryView view;
  view.screen = screen;
  view.wifi = wifi;
#if defined(INVENTORY_SIMULATE_STICKS3)
  view.hasBattery = true;
  view.batteryPercent = battery;
#endif
  view.code = code;
  view.detail = detail;
  view.phase = phase;
  view.unsent = unsent;
  view.batchSaved = saved;
  view.batchTotal = total;
  view.remainingMs = 3200;
  display.render(view);
  M5.Display.waitDisplay();
  writePpm(dir / (std::string(name) + ".ppm"));
}

}  // namespace

// 端末で使う代表的な画面状態を同じ条件で連続生成する。
int user_func(bool* running) {
  const char* output = std::getenv("INVENTORY_CAPTURE_DIR");
  const std::filesystem::path outputDir = output && *output ? output : ".";
  std::filesystem::create_directories(outputDir);

  M5.Display.init();
  InventoryDisplay display;
  if (!display.begin()) {
    std::cerr << "Could not create the display sprite.\n";
    *running = false;
    return 1;
  }

  capture(display, outputDir, "reader-start", InventoryScreen::boot, WifiVisualState::connecting, "", "READER START");
  capture(display, outputDir, "reader-resume", InventoryScreen::boot, WifiVisualState::offline, "", "READER RESUME");
  capture(display, outputDir, "reader-recovery-hold", InventoryScreen::error, WifiVisualState::offline, "", "RECOVERY HOLD");
  capture(display, outputDir, "reader-power-error", InventoryScreen::error, WifiVisualState::offline, "", "READER POWER");
  capture(display, outputDir, "reader-config-error", InventoryScreen::error, WifiVisualState::offline, "", "READER CONFIG");
  capture(display, outputDir, "reader-storage-error", InventoryScreen::error, WifiVisualState::offline, "", "READER STORAGE");
  capture(display, outputDir, "ready", InventoryScreen::ready, WifiVisualState::online);
  capture(display, outputDir, "scanning", InventoryScreen::scanning, WifiVisualState::online, "", "", 1);
  capture(display, outputDir, "queued", InventoryScreen::queued, WifiVisualState::online, "C145", "AUTO RETRY", 0, 1);
  capture(display, outputDir, "sending", InventoryScreen::sending, WifiVisualState::online, "C145", "", 2, 1, 0, 1);
  capture(display, outputDir, "saved", InventoryScreen::saved, WifiVisualState::online, "C145");
  capture(display, outputDir, "pending", InventoryScreen::pending, WifiVisualState::offline, "C145", "WAIT WIFI", 0, 3);
  capture(display, outputDir, "no-code", InventoryScreen::noCode, WifiVisualState::online);
  capture(display, outputDir, "queue-full", InventoryScreen::full, WifiVisualState::offline, "C145", "NOT ACCEPTED", 0, 16);
  capture(display, outputDir, "syncing", InventoryScreen::sending, WifiVisualState::online, "C145", "", 1, 3, 2, 5);
  capture(display, outputDir, "retry-failed", InventoryScreen::pending, WifiVisualState::online, "C145", "AUTO RETRY", 0, 3, 2, 5);
  capture(display, outputDir, "scan-during-sync", InventoryScreen::scanning, WifiVisualState::online, "", "", 1, 3, 2, 5);
  capture(display, outputDir, "added-during-sync", InventoryScreen::queued, WifiVisualState::online, "U173", "AUTO RETRY", 0, 4, 2, 5);
  capture(display, outputDir, "saved-with-unsent", InventoryScreen::saved, WifiVisualState::online, "U173", "", 0, 3, 2, 5);
  capture(display, outputDir, "all-saved", InventoryScreen::allSaved, WifiVisualState::online, "", "", 0, 0, 5, 5);
  capture(display, outputDir, "local-storage-error", InventoryScreen::error, WifiVisualState::online, "U173", "LOCAL STORAGE", 0, 1);
  capture(display, outputDir, "unsent-9", InventoryScreen::pending, WifiVisualState::offline, "C145", "WAIT WIFI", 0, 9);
  capture(display, outputDir, "unsent-10", InventoryScreen::pending, WifiVisualState::connecting, "C145", "WAIT WIFI", 0, 10);
  capture(display, outputDir, "two-digit-progress", InventoryScreen::sending, WifiVisualState::online, "C145", "", 2, 6, 10, 16);
  capture(display, outputDir, "long-code", InventoryScreen::saved, WifiVisualState::online, "https://example.invalid/synthetic/long-code-1234567890", "", 0, 9);
  capture(display, outputDir, "battery-0", InventoryScreen::ready, WifiVisualState::offline, "", "", 0, 0, 0, 0, 0);
  capture(display, outputDir, "battery-100", InventoryScreen::ready, WifiVisualState::online, "", "", 0, 0, 0, 0, 100);
  capture(display, outputDir, "battery-unavailable", InventoryScreen::ready, WifiVisualState::connecting, "", "", 0, 0, 0, 0, -1);

  capture(display, outputDir, "rejected-space-freed", InventoryScreen::full, WifiVisualState::online, "C145", "NOT ACCEPTED", 0, 15);

  std::cout << "Captured 29 states in " << outputDir << "\n";
  SDL_Event quitEvent{};
  quitEvent.type = SDL_QUIT;
  SDL_PushEvent(&quitEvent);
  *running = false;
  return 0;
}

int main(int, char**) {
#if defined(__APPLE__)
  SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
#endif
  return lgfx::Panel_sdl::main(user_func, 128);
}
