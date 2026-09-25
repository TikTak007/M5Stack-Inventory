#pragma once
// このファイルを secrets.h としてコピーし、実値はコピー先だけへ入力する。
// secrets.h はGit対象外。SSID、パスワード、URL、DEVICE_KEYを公開しない。

// 2.4 GHz Wi-Fiの接続情報。
#define INVENTORY_WIFI_SSID ""
#define INVENTORY_WIFI_PASSWORD ""

// Apps ScriptをWebアプリとしてデプロイした /exec URL と、
// スクリプトプロパティ DEVICE_KEY に設定したものと同じ32文字以上の値。
#define INVENTORY_ENDPOINT "https://script.google.com/macros/s/REPLACE/exec"
#define INVENTORY_DEVICE_KEY ""

// script.google.com とリダイレクト先 script.googleusercontent.com の両方を
// 検証できるPEM証明書チェーンを貼り付ける。setInsecure()は使用しない。
static const char INVENTORY_ROOT_CA[] = R"PEM(
)PEM";

// true: 読取りと画面だけを確認する。false: Wi-Fi送信を有効にする。
// PlatformIOのatoms3-send-check / sticks3-send-check環境はビルド時にfalseへ上書きする。
#ifndef INVENTORY_CAPTURE_ONLY
#define INVENTORY_CAPTURE_ONLY true
#endif
