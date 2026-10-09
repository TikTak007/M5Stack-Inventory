#pragma once

// SDL画面シミュレータ専用の最小M5Unified互換層。
// 実機用InventoryDisplay.hを変更せず、PC上のM5GFXでコンパイルするために使う。

#include <M5GFX.h>
#include <cstdint>
#include <cstring>
#include <string>

// InventoryDisplay.hが使用するArduino Stringの機能だけをstd::stringで再現する。
class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  String(const std::string& value) : value_(value) {}

  std::size_t length() const { return value_.length(); }
  const char* c_str() const { return value_.c_str(); }
  operator const char*() const { return value_.c_str(); }

  void remove(std::size_t index) {
    if (index < value_.size()) value_.erase(index);
  }

  String substring(std::size_t begin) const {
    return begin < value_.size() ? value_.substr(begin) : std::string();
  }

  String substring(std::size_t begin, std::size_t end) const {
    if (begin >= value_.size() || end <= begin) return String();
    return value_.substr(begin, end - begin);
  }

  friend String operator+(const String& left, const char* right) {
    return left.value_ + (right ? right : "");
  }

 private:
  std::string value_;
};

// 実機側の M5.Display と同じ参照形式を提供する。
struct M5UnifiedHostAdapter {
  M5GFX Display;
};

extern M5UnifiedHostAdapter M5;
