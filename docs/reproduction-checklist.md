# 技術者向け再現チェックリスト

このチェックリストは、別のIoT/Web技術者が資料を先頭からたどり、同じ構成を再現できるかを確認するためのものです。

## 構成を説明できる

- Unit QRCodeのTRIG入力、端末の不揮発キュー、HTTPS送信、Apps Script、Scans、ProductMaster、Inventoryの関係を説明できる。
- Scansが在庫履歴、Inventoryが履歴から集計された表示、ProductMasterが商品情報であることを区別できる。
- 端末の`SAVED`が、HTTP成功だけでなくApps Scriptの書込み後にevent_idとcodeを照合する読戻し確認を意味すると説明できる。
- SAVED（対象1件）、ALL SAVED（端末の全件完了）、READY（次の受付可能）の違いと、UNSENTが送信中も含む未確認件数であることを説明できる。

## 同じ環境を作れる

- `platformio.ini`、`Code.gs`、`appsscript.json`、`secrets.example.h`の配置先が分かる。
- `SPREADSHEET_ID`をURLから取得できる。
- macOS / LinuxではOpenSSL、Windowsでは標準PowerShellを使って`DEVICE_KEY`を生成し、Apps Scriptと`secrets.h`へ同じ値を設定できる。
- `/exec` URLを発行し、GETの死活応答を確認できる。
- 両Googleホストの証明書チェーンを確認し、信頼済みルートCAをPEMで設定できる。
- AtomS3は`atoms3-send-check`、StickS3は`sticks3-send-check`をビルド、書込みし、115200 bpsのログを確認できる。
- StickS3の初期化中のIR消灯と給電後800msの通信待機、正常`0x21`とブート`0x54`の違いを説明できる。
- ブート時だけ復帰命令`0x77`を一度送り、再給電せず最大約2秒・10回で`0x21`の版・手動モード・TRIGまで確認する仕様を説明できる。
- 復帰確認待ちが在庫キューと別に保存され、本体再起動後も`0x54`への応答確認と命令再送を行わず、通常`0x21`の設定まで正常な場合だけ解除されることを確認できる。
- `STARTING / READER RESUME`、`ERROR / RECOVERY HOLD`、`READER STORAGE / POWER / BUS / I2C / CONFIG`から[起動切り分け手順](setup.md#sticks3の起動確認と切り分け)を参照できる。Aボタンで再試行せず、未送信イベントを保持する。
- ブート復帰が実機未検証であること、Unit起動時の不調とスキャン中の異常は別であることを説明できる。原因や再発防止を未確認のまま確定と記録しない。

## 保存仕様を検証できる

- 新規イベント、同じIDの再送、新しいIDで同じコード、異なるコードで同じIDを試験できる。
- Wi-Fi切断、再起動、復旧後の再送でも二重加算されないことを確認できる。
- 別IDの成功応答、verified欠落/false、同じコード・異なるIDの読戻しを成功としないことを検証できる。
- 0/1/15/16件と17件目の拒否を確認し、満杯時は空きができてから再スキャンできる。
- 固定した再送分母、途中失敗で分子が増えないこと、途中の追加読取り、全件完了、背景通信とTRIGの表示優先を確認できる。
- 次の受付間隔とScans保存確認時間を分け、合成試験・ビルド・実機・Google確認の範囲を記録できる。
- ContentServiceの302/303では転送先をGETし、共有キー付きPOSTを転送しない理由を理解できる。
- `UNAUTHORIZED`、`NOT_CONFIGURED`、`STORED / WAIT WIFI`、`SCHEMA_MISMATCH`の確認箇所が分かる。

## 運用上の境界を理解できる

- 数量セルやScansの既存行を直接編集せず、状態変更と初期化機能を使える。
- 商品情報の自動取得に失敗した場合、ProductMasterを手動補完できる。
- 個人の少量データ向けであり、大量履歴の性能は未検証だと分かる。
- 公開前に`secrets.h`、実際のキー、Wi-Fi情報、デプロイURLを除外し、製品写真の再配布条件とライセンスを確認できる。

## 起動切り分けの資料を更新できる

`tools/update_startup_guide.py`は既存v10のPDF / PPTXの先頭22ページを保持し、23ページ目のStickS3起動付録だけを追加・差し替える。PPTXの付録は編集可能な文字と図形。クラウドのCanvaデザインはこの処理では更新しない。

依存ライブラリは`reportlab`、`pypdf`、`python-pptx`。`python3 tools/update_startup_guide.py`で再生成する。macOS以外では日本語を含むTrueTypeフォントを`--font`で指定する。PDF本文とPPTX本文の一致、元の22ページの保持、付録の描画と文字切れを確認してから配布する。
