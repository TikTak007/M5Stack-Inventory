# M5Stack StickS3＋Unit QRCode（U173）の起動時再起動・I2C認識不良と対策

StickS3にUnit QRCodeを接続した際、起動時に本体が再起動し、読取器のI2C認識が不安定になる症状と対策をまとめています。電源変動とブートモードの関係、復帰処理、設定応答による`CONTROL BYTES`表示の対処を説明します。

## English summary

A StickS3 connected to a Unit QRCode (U173) showed unexpected restarts and intermittent I2C detection failures during reader startup. The same development setup with AtomS3 did not show this symptom. Power transients are a hypothesis, not a measured brownout. The firmware holds IR TX low during initialization and implements a guarded bootloader return. A separate fix filters exact reader command acknowledgements. Actual bootloader return and voltage transients remain unverified.

## 使用構成と確認範囲

| 項目 | 構成・版 |
|---|---|
| 本体 | M5Stack StickS3（K150） |
| 読取器 | M5Stack Unit QRCode（U173）、切替スイッチはI2C |
| 接続 | 本体Groveポートから5Vを供給、SDA=G9、SCL=G10、100kHz |
| 確認時の給電 | MacにUSB接続したStickS3 |
| 開発環境 | PlatformIO、`sticks3-send-check`、espressif32 6.12.0 |
| 主なライブラリ | M5Unified 0.2.22、M5GFX 0.2.29、M5UnitQRCode 1.0.1、M5PM1 1.0.7 |
| Unitの版確認 | 通常アプリのFWレジスタ値は3。接続Unitのブートローダー版は未確認 |

電源・信号線の波形、電池単独での安定性、多数の実機での再現性は未確認です。

## 起きていた症状

一組の実機で、次の症状を確認しました。

| 観測 | 状況 |
|---|---|
| 画面暗転と再起動 | `STARTING`表示後、約1秒以内に一度暗転し、再び`STARTING`から認識エラーへ進むことがあった |
| 不安定な認識 | リセットを繰り返すと、ときどきUnitを認識し、その後は通常利用できた |
| Grove未接続との違い | Unitを外して起動すると、暗転・再起動は起きなかった |
| Grove再接続 | 起動中の挿し直しで本体が再起動する。一度通常起動した後は、再接続でも最終的に`READY`へ進むことがあった |
| 発生する段階 | Unit起動時だけに発生し、スキャン中の再起動は報告されていない |
| 別機種との比較 | 同じ開発環境のAtomS3では、この起動症状は発生しなかった |

起動音・白色照明・赤色照準光が動いても、I2C制御用STM32の通常アプリが動いている証拠にはなりません。Unitの読取エンジンとI2C制御は別の回路です。[Unit QRCode仕様・回路図](https://docs.m5stack.com/en/unit/Unit-QRCode)

開発中のI2C診断では、通常アドレス`0x21`が応答せず、`0x54`だけが応答する状態も確認しました。

## 考えられる原因：電源変動とブートモード

Grove接続の有無と再起動の関係から、Unit起動時の負荷が本体側の電源へ影響する可能性を疑いました。ただし、Groveの脱着は信号線も変えるため、この比較だけで電圧降下を確定できません。

StickS3ではGrove出力とIR TX/RXの給電が`EXT_5V_EN`で制御されます。Groveの公式最大負荷表記は4.88V・0.38Aですが、これは今回の起動時電流の測定値や、380mAで必ず遮断するしきい値ではありません。[StickS3公式仕様・給電上の注意](https://docs.m5stack.com/en/core/StickS3)

公式Unitファームウェアの固定版`2c44fe0`では、起動処理が300ms待った後、SDA/SCLが両方LowならI2Cブートモードへ入り、`0x54`で応答します。通常アプリは`0x21`です。300ms間ずっとLowかを監視する実装ではありません。[公式ブートローダーの起動判定](https://github.com/m5stack/M5Unit-QRCode-Internal-FW/blob/2c44fe0fd85d3346bd6484fcd59e5cb333dd7937/code/bootloader/IAPTest_LL/IAPTest_LL/Core/Src/main.c#L388)

考えられる連鎖は「Unitの起動負荷 → 本体の電源変動・再起動 → 起動判定時の両信号線Low → Unitのブートモード」です。電源・SDA/SCLの同時波形は測定しておらず、この連鎖は未実証です。**`0x54`は電源不足を表すエラー値ではありません。**

## 実装した対策

修正コードは[公開版の最新コード](https://github.com/TikTak007/M5Stack-Inventory/tree/main)で参照できます。

| 対策 | 動作と目的 |
|---|---|
| 不要なIR負荷の抑制 | StickS3と判定した場合だけGPIO46をLOWに保持してM5Unifiedを初期化し、LOWを再設定してから保持を解除する。IR消灯による実消費電流の変化は未測定 |
| 起動直後の通信待機 | StickS3は800ms、I2C通信をせず信号線を入力状態にする。給電・両線を確認してから通信開始。リセット中まで信号線Highを保証したり、電源の供給能力を増やしたりする対策ではない |
| ブートモードからの復帰 | 通常`0x21`が応答せず`0x54`が応答した場合だけ、確認待ちを保存してから`0x77`を一度送る。以後は最大10回・約2秒で`0x21`だけを確認 |
| 設定応答と製品コードの区別 | 受信全体が既知の成功応答だけの場合に限り消費する。未知・失敗・不完全な応答やコード混在は従来の検査へ渡す |

通常モードのFW・手動モード・TRIG状態を読み戻し、通信初期化まで成功してから`READY`へ進みます。`0x77`の送信ACKだけで成功とはしません。この命令による実機のブートモードからの復帰は未確認です。復帰未確認の記録が残る場合は、本体をリセットしても`0x54`の再確認・命令再送を行いません。Unitへの自動OFF/ON、本体の自動再起動ループ、UnitのFW消去・書換えを行う実装にはしていません。

実装は[本体初期化と読取器制御](https://github.com/TikTak007/M5Stack-Inventory/blob/main/platformio/src/main.cpp)、[IR消灯を保持する初期化](https://github.com/TikTak007/M5Stack-Inventory/blob/main/platformio/include/StickPowerStartup.h)、[ブート復帰処理](https://github.com/TikTak007/M5Stack-Inventory/blob/main/platformio/include/ReaderStartup.h)を参照してください。

## 別途見つかったCONTROL BYTESの問題

TRIGを押していない起動直後に`CONTROL BYTES`が表示される場合があります。受信データに制御文字が含まれるため在庫登録を止めた表示で、I2C認識エラーとは別の問題です。

Unit内部のモード設定・停止への応答が、読取り結果に混入した可能性があります。公式UART受信処理は単独の5バイト応答を除外しますが、複数応答がまとまった場合は読取り結果へ渡す経路があります。今回の実受信バイト列は未取得で、応答連結を確定原因とはしていません。[公式UART受信処理](https://github.com/m5stack/M5Unit-QRCode-Internal-FW/blob/2c44fe0fd85d3346bd6484fcd59e5cb333dd7937/code/qrcode/Core/Src/stm32f0xx_it.c#L226)

次の成功応答だけで受信全体が構成される場合を、製品コードとして扱わないよう修正しました。応答の形式は[公式プロトコルPDF・4～6ページ](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/770/Unit-QRCode-Protocol-EN.pdf)に基づきます。

| 応答 | 5バイトの値（16進数） |
|---|---|
| 手動モード設定成功 | `22 61 41 00 00` |
| 起動時のモード設定成功 | `22 61 41 05 00` |
| 読取り停止成功 | `33 75 02 00 00` |

[応答判定コード](https://github.com/TikTak007/M5Stack-Inventory/blob/main/platformio/include/ReaderReply.h)は読取り状態・表示・在庫キューの更新前に適用します。混在データから応答らしい部分だけを削る処理や、追加の待ち時間は入れていません。

## 同じ症状に遭遇した場合

1. 電源を外した状態でGrove接続とUnitのI2Cスイッチを確認し、本リポジトリの対策を含むファームウェアを使います。
2. 起動後の表示を記録します。`READER RESUME`は復帰確認中、`RECOVERY HOLD`は未確認のまま停止、`READER I2C`などは通信・設定の切り分けが必要です。[セットアップのエラー別対処](setup.md#sticks3の起動確認と切り分け)を参照してください。
3. 再発時は、給電方法、コード・ライブラリの版、TRIG未操作かどうか、表示・起動タイミングを整理します。必要な診断ログはコード本文、Wi-Fi名・パスワード、DEVICE_KEY、端末識別子を除いて共有してください。

シリアル接続や書込みツール自体が本体をリセットする場合があります。ツール操作による再起動と自然に発生した再起動を区別してください。音だけで起動・在庫保存を判断せず、`READY`と、読み取った後の`SAVED`をそれぞれ確認します。

本事例の対処では低電圧検出を無効化していません。未送信イベントの全消去、リセット連打、通電したGroveの抜き差しを復旧手順にしないでください。独立給電を検討する場合も、Groveの出力5Vへ別電源を並列接続しないでください。[StickS3の公式給電制約](https://docs.m5stack.com/en/core/StickS3#note)

関連する運用仕様は[保存と通信のプロトコル](protocol.md)を参照してください。
