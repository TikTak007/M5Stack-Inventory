# Arduino IDE版

このフォルダーは、PlatformIO版と同じAtomS3ファームウェアをArduino IDEでビルドするためのスケッチです。`M5Stack_Inventory.ino`と`InventoryDisplay.h`はPlatformIO版から生成されます。

## 対応範囲

- 対象：M5Stack AtomS3 + Unit QRCode（SKU: U173、I2Cモード）
- 操作：Unit QRCodeのTRIGを押している間だけ読み取り
- 通信：2.4GHz Wi-FiからApps Script WebアプリへHTTPS送信
- 表示：M5Unified + M5GFX Sprite
- StickS3：このArduino IDE版では未検証。PlatformIOの`sticks3`環境はコンパイル確認のみ

## 1. Arduino IDEとボードを準備

1. Arduino IDE 2.xをインストールします。
2. `ファイル` → `基本設定` → `追加のボードマネージャのURL`へ次を追加します。

   ```text
   https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
   ```

3. ボードマネージャから`M5Stack`をインストールします。
4. `ツール` → `ボード`で`M5AtomS3`を選択します。
5. `USB CDC On Boot`を`Enabled`、シリアルモニタを`115200 bps`にします。

検証済みボードパッケージは`M5Stack 3.3.7`です。

## 2. ライブラリをインストール

Arduino IDEの`ツール` → `ライブラリを管理`から次をインストールします。再現性のため、表のバージョンを選択してください。

| ライブラリ | バージョン |
|---|---:|
| M5Unified | 0.2.22 |
| M5GFX | 0.2.29 |
| M5UnitQRCode | 1.0.0 |
| ArduinoJson | 7.2.1 |

`WiFi`、`WiFiClientSecure`、`HTTPClient`、`Preferences`はM5Stack ESP32ボードパッケージに含まれます。

`M5UnitQRCode` 1.0.0はArduino IDEのライブラリマネージャーから導入でき、このスケッチでコンパイル確認済みです。PlatformIO版はGitHubの現行ソースに合わせて1.0.1を使用していますが、本ファームウェアが使用するAPIは1.0.0と互換です。

## 3. スケッチを設定

1. この`M5Stack_Inventory`フォルダー全体をArduinoのスケッチブックへコピーします。フォルダー名と`.ino`名を同じにします。
2. `secrets.example.h`を同じフォルダー内で`secrets.h`という名前で複製します。
3. `secrets.h`へWi-Fi、Apps Scriptの`/exec` URL、Google側と同じ`DEVICE_KEY`、現在有効なルートCAを設定します。各値の入手とApps Script側の配置は[セットアップ手順](../../docs/apps-script-setup.md)を参照してください。
4. 送信を有効にする場合は、`secrets.h`の末尾へ次を追加するか、既存定義を`false`へ変更します。

   ```cpp
   #define INVENTORY_CAPTURE_ONLY false
   ```

認証情報を設定していない段階では`true`のままにし、読取りと画面表示だけを確認してください。`secrets.h`はGitへ登録しません。

## 4. 検証と書込み

1. AtomS3をUSBで接続し、対応するポートを選択します。
2. `スケッチ` → `検証・コンパイル`を実行します。
3. エラーがなければ`マイコンボードに書き込む`を実行します。
4. シリアルモニタを115200 bpsで開き、`PORT.A I2C pins`、Wi-Fi接続、送信結果を確認します。
5. Unit QRCodeのTRIGを押し、`SAVED / READY FOR NEXT`とSheetsへの1件追加を確認します。

## よくある問題

| 症状 | 確認すること |
|---|---|
| `M5Unified.h`が見つからない | ライブラリマネージャで指定バージョンを導入したか |
| `M5UnitQRCode.h`が見つからない | `M5UnitQRCode` 1.0.0を導入したか |
| シリアルポートが表示されない | `USB CDC On Boot`とUSBケーブル、ダウンロードモードを確認 |
| 読取りだけで送信されない | `INVENTORY_CAPTURE_ONLY`が`false`か |
| `UNAUTHORIZED` | Google側と`secrets.h`の`DEVICE_KEY`が一致するか |
| `PENDING`が続く | Wi-Fi、`/exec` URL、ルートCA、Apps Script実行履歴を確認 |

## PlatformIO版と内容を揃える

PlatformIO版を変更した場合は、リポジトリのルートで次を実行してArduino IDE版へ反映します。

```sh
python3 tools/sync_arduino_sketch.py
python3 tools/sync_arduino_sketch.py --check
```

両方の開発環境へ同じ変更を反映するため、通常はPlatformIO版を編集してからこの処理を実行します。
