# Arduino IDE版

AtomS3またはStickS3とUnit QRCodeを使う在庫管理スケッチです。PlatformIO版と同じ読取り・保存確認・画面表示を使います。フォルダー全体をコピーしてArduino IDEで開いてください。

## 対応範囲

- 対象：M5Stack AtomS3またはStickS3 + Unit QRCode（SKU: U173、I2Cモード）
- 操作：Unit QRCodeのTRIGを押している間だけ読み取り
- 通信：2.4GHz Wi-FiからApps Script WebアプリへHTTPS送信
- 表示：M5Unified + M5GFX Sprite
- PORT.Aピンと画面サイズはM5Unifiedから取得し、機種に合わせて表示する
- 機種・開発環境ごとの今回のビルドと実機確認は[検証記録](../../docs/validation.md)を参照

## 1. Arduino IDEとボードを準備

1. Arduino IDE 2.xをインストールします。
2. `ファイル` → `基本設定` → `追加のボードマネージャのURL`へ次を追加します。

   ```text
   https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
   ```

3. ボードマネージャから`M5Stack`をインストールします。
4. `ツール` → `ボード`で、AtomS3は`M5AtomS3`、StickS3は`M5StickS3`を選択します。StickS3では`PSRAM`を`OPI PSRAM`、`Partition Scheme`を`8M with spiffs (3MB APP/1.5MB SPIFFS)`に設定します。
5. `USB CDC On Boot`を`Enabled`、シリアルモニタを`115200 bps`にします。

検証済みボードパッケージは`M5Stack 3.3.7`です。

StickS3では専用のボード定義を使ってください。`ESP32S3 Dev Module`などの汎用ボードでは、StickS3の赤外線LEDを初期化中から消灯する処理が選択されません。PlatformIOでは`sticks3`または`sticks3-send-check`環境がこの処理を有効にします。

## 2. ライブラリをインストール

Arduino IDEの`ツール` → `ライブラリを管理`から次をインストールします。再現性のため、表のバージョンを選択してください。

| ライブラリ | バージョン |
|---|---:|
| M5Unified | 0.2.22 |
| M5GFX | 0.2.29 |
| M5UnitQRCode | 1.0.0 |
| ArduinoJson | 7.2.1 |

`WiFi`、`WiFiClientSecure`、`HTTPClient`、`Preferences`はM5Stack ESP32ボードパッケージに含まれます。

`M5UnitQRCode` 1.0.0はArduino IDEのライブラリマネージャーから導入でき、過去の版でコンパイル確認済みです。今回の起動復帰候補のArduino CLI検証は1.0.1を使った限定検証です。詳しい範囲は[検証記録](../../docs/validation.md)を参照してください。PlatformIO版は1.0.1を使用しますが、本ファームウェアが使用するAPIは1.0.0と互換です。

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

1. 対象のAtomS3またはStickS3をUSBで接続し、機種と対応するポートを選択します。
2. `スケッチ` → `検証・コンパイル`を実行します。
3. エラーがなければ`マイコンボードに書き込む`を実行します。
4. シリアルモニタを115200 bpsで開き、`PORT.A I2C pins`、Wi-Fi接続、送信結果を確認します。
5. Unit QRCodeのTRIGを押し、対象コードと`SAVED`を確認します。Scansへの1件追加とInventoryの集計は別に確認します。

`UNSENT n`は送信中も含む未確認件数、`READY`は次の受付可能、`SAVED`は表示中の1件のScans保存確認、`ALL SAVED`は端末の全件完了です。通信断でも最大16件を電源再投入後まで保持し、復旧後に同じイベントIDで再送します。`2/5 SAVED`の分母は再送開始時の件数に固定し、途中の追加読取りは後続の対象になります。

`STORAGE FULL / 16/16`では今回の読取りを受け付けていません。空きができてから再スキャンしてください。`LOCAL STORAGE`では端末内保存済みと扱いません。リーダー音だけでは受付成功を判断できません。

起動時にUnitがブートモードで応答した場合は、`READER RESUME`を表示して通常モードへの復帰を一度だけ要求します。通常アドレスと設定を確認すると起動を続けます。確認前に本体が再起動しても命令は繰り返しません。`RECOVERY HOLD`で停止した場合は自動復帰を確認できなかった状態です。繰り返しリセットせず、接続・電源を確認してください。未送信の在庫データは保持します。この処理は電源供給能力を増やすものではありません。

## よくある問題

| 症状 | 確認すること |
|---|---|
| `M5Unified.h`が見つからない | ライブラリマネージャで指定バージョンを導入したか |
| `M5UnitQRCode.h`が見つからない | `M5UnitQRCode` 1.0.0を導入したか |
| シリアルポートが表示されない | `USB CDC On Boot`とUSBケーブル、ダウンロードモードを確認 |
| 読取りだけで送信されない | `INVENTORY_CAPTURE_ONLY`が`false`か |
| `UNAUTHORIZED` | Google側と`secrets.h`の`DEVICE_KEY`が一致するか |
| `UNSENT`が減らない | Wi-Fi、`/exec` URL、ルートCA、Apps Script実行履歴を確認 |

## PlatformIO版と内容を揃える

PlatformIO版を変更した場合は、リポジトリのルートで次を実行してArduino IDE版へ反映します。

```sh
python3 tools/sync_arduino_sketch.py
python3 tools/sync_arduino_sketch.py --check
```

`M5Stack_Inventory.ino`、`InventoryDisplay.h`、`SaveStatus.h`、`DurableOutbox.h`、`ReaderStartup.h`、`StickPowerStartup.h`を含む動作部分を同期します。両環境へ同じ修正を反映する場合はPlatformIO版を編集してから同期してください。
