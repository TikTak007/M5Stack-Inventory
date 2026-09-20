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
             const char* code = "", const char* detail = "", uint8_t phase = 0) {
  InventoryView view;
  view.screen = screen;
  view.wifi = wifi;
  view.code = code;
  view.detail = detail;
  view.phase = phase;
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
    std::cerr << "Could not create the 128x128 display sprite.\n";
    *running = false;
    return 1;
  }

  capture(display, outputDir, "ready", InventoryScreen::ready, WifiVisualState::online);
  capture(display, outputDir, "scanning", InventoryScreen::scanning, WifiVisualState::online, "", "", 1);
  capture(display, outputDir, "queued", InventoryScreen::queued, WifiVisualState::online, "C145", "LOCAL SAFE");
  capture(display, outputDir, "sending", InventoryScreen::sending, WifiVisualState::online, "C145", "", 2);
  capture(display, outputDir, "saved", InventoryScreen::saved, WifiVisualState::online, "C145");
  capture(display, outputDir, "pending", InventoryScreen::pending, WifiVisualState::offline, "C145", "WAIT WIFI");
  capture(display, outputDir, "no-code", InventoryScreen::noCode, WifiVisualState::online);
  capture(display, outputDir, "queue-full", InventoryScreen::error, WifiVisualState::offline, "", "QUEUE FULL");

  std::cout << "Captured 8 states in " << outputDir << "\n";
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
