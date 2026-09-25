# 技術者向け再現チェックリスト

このチェックリストは、別のIoT/Web技術者が資料を先頭からたどり、同じ構成を再現できるかを確認するためのものです。

## 構成を説明できる

- Unit QRCodeのTRIG入力、端末の不揮発キュー、HTTPS送信、Apps Script、Scans、ProductMaster、Inventoryの関係を説明できる。
- Scansが在庫履歴、Inventoryが履歴から集計された表示、ProductMasterが商品情報であることを区別できる。
- 端末の`SAVED`が、HTTP成功だけでなくApps Scriptの書込みと読戻し確認を意味すると説明できる。

## 同じ環境を作れる

- `platformio.ini`、`Code.gs`、`appsscript.json`、`secrets.example.h`の配置先が分かる。
- `SPREADSHEET_ID`をURLから取得できる。
- macOS / LinuxではOpenSSL、Windowsでは標準PowerShellを使って`DEVICE_KEY`を生成し、Apps Scriptと`secrets.h`へ同じ値を設定できる。
- `/exec` URLを発行し、GETの死活応答を確認できる。
- 両Googleホストの証明書チェーンを確認し、信頼済みルートCAをPEMで設定できる。
- AtomS3は`atoms3-send-check`、StickS3は`sticks3-send-check`をビルド、書込みし、115200 bpsのログを確認できる。

## 保存仕様を検証できる

- 新規イベント、同じIDの再送、新しいIDで同じコード、異なるコードで同じIDを試験できる。
- Wi-Fi切断、再起動、復旧後の再送でも二重加算されないことを確認できる。
- ContentServiceの302/303では転送先をGETし、共有キー付きPOSTを転送しない理由を理解できる。
- `UNAUTHORIZED`、`NOT_CONFIGURED`、`PENDING`、`SCHEMA_MISMATCH`の確認箇所が分かる。

## 運用上の境界を理解できる

- 数量セルやScansの既存行を直接編集せず、状態変更と初期化機能を使える。
- 商品情報の自動取得に失敗した場合、ProductMasterを手動補完できる。
- 個人の少量データ向けであり、大量履歴の性能は未検証だと分かる。
- 公開前に`secrets.h`、実際のキー、Wi-Fi情報、デプロイURLを除外し、製品写真の再配布条件とライセンスを確認できる。
