# 最小構成のセットアップ

Google SheetsとApps Scriptを初めて作る場合は、先に[Google Sheets / Apps Scriptセットアップ](apps-script-setup.md)を開いてください。この文書は実機の読取り、送信、運用、受入試験を中心に説明します。

## 1. 読取りだけ先に確認

1. 電源を外し、Unit QRCodeの切替をI2CにしてAtomS3またはStickS3のPORT.Aへ接続。
   AtomS3側は黄=G2/SDA、白=G1/SCL。StickS3では黄=G9/SDA、白=G10/SCL。
   ファームウェアはM5UnifiedからPORT.Aピンを取得する。
2. `platformio/include/secrets.example.h`を同じフォルダーの`secrets.h`へコピー（既存設定がある場合は上書きしない）。
   `INVENTORY_CAPTURE_ONLY true` のままビルドし、対象ポートを確認して書き込む。
3. 読取り切り分け中の `INVENTORY_CAPTURE_ONLY true` でもUnit QRCodeのTRIGを使う。
   TRIGを押している間だけ `SCANNING` になり、箱のコードへ向けて読取り音が鳴ったら離す。
   USBシリアル115200bpsへバイト数・文字種別・不可逆な指紋値だけが出る。
   コード本文は端末画面に約5秒表示され、初期設定では送信されない。
4. 5〜10種類のコードをローカルで確認。シリアル番号や非公開URLはGitや動画へ出さない。
   送信モードでは制御文字・長すぎるコードを拒否するため、実データに合わせて次に調整する。

## 2. Google側

1. 自分のGoogleアカウントで空の非公開スプレッドシートを作る。
2. 拡張機能 → Apps Scriptへ Code.gs の内容を配置。
   必要に応じて appsscript.json を表示し、同梱設定を使う。
3. Apps Script左側の歯車「プロジェクトの設定」を開き、
   「スクリプト プロパティ」→「スクリプト プロパティを追加」で次の2行を登録する。
   これはコード本文へ秘密値を直接書かず、実行時だけ読み出すための設定。
   - プロパティ `SPREADSHEET_ID`：スプレッドシートURLの `/d/` と `/edit` の間の文字列。
     ファイル名やシート名ではない。
   - プロパティ `DEVICE_KEY`：端末からのPOSTだけを受け付けるためのランダムな共有キー。
     32文字以上で生成し、後で端末側の `INVENTORY_DEVICE_KEY` に同じ値を設定する。
   `DEVICE_KEY` をチャット・ソース・動画に貼らない。
4. エディタから `setup()` を1回実行し、Sheetsアクセスを許可。
   旧形式のScansは状態履歴付き8列へ、Inventoryは画像・状態・操作付き9列へ安全に移行し、ProductMasterを追加する。
   既存の保有在庫はすべて未使用として移行し、使用中・廃棄済みは0から始める。
   ProductMasterに既存データがある場合は保持し、列構成が異なる場合は上書きせず停止する。
   スプレッドシートのタイムゾーンは `Asia/Tokyo`、Scansの受信日時表示は
   `yyyy/MM/dd HH:mm:ss` に設定される。旧ISO日時文字列も日付型へ安全に変換する。
5. Webアプリとして「自分として実行」、アクセス「全員」でデプロイ。
   組織設定により匿名アクセスが使えない場合は、その時点で確認する。
   `/exec` が機器用受信口。Sheet自体は非公開を保つ。
   URLのGETは死活応答のみで、在庫データを返さない。
6. 自分のMac/スマートフォンでGoogleログインし、Inventoryを表示。

Inventoryは `コード / 保有数 / 未使用 / 使用中 / 廃棄済み / 製品名 / 代表画像 / 参照元 / 操作` の9列。
製品名・画像・リンクはProductMasterの `コード / 製品名 / 画像URL / 参照元URL` を参照する。
新しいコードを受信するとProductMasterへ1行追加される。M5Stack形式のSKUは、
`https://docs.m5stack.com/en/products/sku/<SKU>` から公式製品名・先頭画像・参照元を自動取得する。
公式ページが見つからない場合や取得に失敗した場合は空欄で残るため、B〜D列へ製品名、
直接表示できるHTTPS画像URL、画像を確認できるページURLを入力する。
画像URLへ認証が必要、リダイレクトが多い、または外部表示を拒否するサイトでは画像が表示されない。
Inventoryのデータ行は新規受信時にも高さ92pxへ揃え、並び順が変わっても画像サイズを維持する。

受信口には共有キーが必要。Apps Scriptの応答はHTTP 200でも `ok:false` の場合がある。
機器はHTTPステータスだけで成功と判断せず、`ok` と `eventId` を照合する。
ContentServiceの302/303はgoogleusercontent.comへのGETで取得し、キー付きPOSTを転送しない。

## 3. Wi-Fi送信を有効化

secrets.hに2.4GHz Wi-Fi、/exec URL、同じ機器用キー、現在有効な信頼済みルートCAのPEMを設定。
CAはscript.google.comとscript.googleusercontent.comの両方を検証できるものを使う。
空のCAは送信に失敗する。証明書検証を無効化しない。時刻同期用のネット接続も必要。
AtomS3は `atoms3-send-check`、StickS3は `sticks3-send-check` で再ビルド・書込み。
`atoms3` と `sticks3` は読取り切り分け用、名前が `-send-check` で終わる環境は実機送信用。
どの環境でも本体側ボタンは使わず、Unit QRCodeのTRIGで操作する。

- 画面上部のWi-Fi表示：`ON`は接続済み、`...`は接続中、`OFF`は未接続。
- StickS3はWi-Fi表示の右に電池残量（％）を表示する。値はM5Unifiedから約10秒ごとに取得し、取得できない場合は`--%`と表示する。AtomS3には電池表示を出さない。
- `READY`：待機。`SCANNING`：TRIG押下中。`QUEUED`：端末内へ安全に保存済み。
- `SENDING`：キューの先頭を送信中。HTTPS処理は別タスクなので次の読取りを継続できる。
- `SAVED`：Apps Scriptが書込み後にScansの対象行を読み戻して確認済み。コードを約5秒表示した後に`READY`へ戻る。
- `PENDING`：保存確認待ち。同じイベントIDを自動再送する。未確認データは最大16件保持する。
- `NO CODE`：リーダー音の有無にかかわらず、端末がデコードデータを取得できなかった。
  この表示では在庫イベントを作成しておらず、もう一度読み取る。
- 未確認中も別商品を読み取れ、最大16件まで端末内へ先に保存する。再起動後も古い順に再試行する。
  16件に達した場合は `QUEUE FULL` を表示し、その読取りは受け付けない。
- 書込み時にフラッシュ全消去しない。未確認イベントの消失につながる。
- キャプチャーモードへ戻した場合、残った未確認キューは送信せず保持する。

## 4. 在庫状態の更新

Inventoryの対象行で「操作」を選ぶ。`使用開始` は未使用を1減らして使用中を1増やす。
`使用中を廃棄` は使用中を1減らし、`未使用を廃棄` は未使用を1減らして、どちらも廃棄済みを1増やす。
選択後はApps ScriptがイベントID・時刻を自動生成してScansへ追記し、操作セルを空に戻す。
移動元の在庫が0の場合は追記せず、シート右下へメッセージを表示する。

Inventoryの数量・数式セルとScansの既存行は直接編集・削除しない。履歴を消すと集計と再送時の
重複判定が崩れる。名称・画像・参照元の修正はProductMasterで行う。

## 5. 初期化

Inventory右側の管理欄、または上部の「在庫管理」メニューから実行する。

`setup()` は2つの画像ボタンをJ列へ配置し、既存の同名ボタンがあれば置き換える。
ボタン画像の手動アップロードやスクリプト割り当ては不要。

- `RESET INVENTORY`：Scansの履歴とInventoryの在庫表示を空にする。ProductMasterは保持する。
- `FULL RESET`：Scans、Inventory、ProductMasterのデータをすべて空にする。
  誤操作防止のため、確認画面へ `完全初期化` と入力した場合だけ実行する。

どちらも列見出し、数式、書式、ボタンは再構成される。実行前に端末が送信完了状態で、
未送信データが残っていないことを確認する。未送信イベントがあると、初期化後に再登録される。
初期化は元に戻せないため、必要に応じてGoogle Sheetsのダウンロードでバックアップを保存する。

## 6. 実機受入試験

- 未知コード初回=1、別の操作で同じコード=2。
- 同じeventId再送は数量不変。異なるcodeで同じIDは拒否。
- Wi-Fi切断 → Not confirmed → 復旧して再試行 → 1回だけ保存。
- 保存直後の応答喪失と再起動 → 同じIDで再送 → 数量不変。
- TRIGを押している間だけ読み取り、1回の押下で1回だけ保存。Atom側ボタンは使わない。
- スマートフォンから非公開Sheetを確認。3種類の状態変更が正しく反映される。
- 初期化の確認画面でキャンセルした場合、3シートのデータが変わらない。
- 通常初期化ではProductMasterを保持し、完全初期化ではProductMasterも空になる。
- 読取り開始、Saved表示、Sheets画面描画の時刻を別々に記録。
  2秒ポーリングや「数秒で表示」は未実装・未実測。

## 公式資料（2026-09-19確認）

- https://docs.m5stack.com/en/core/AtomS3
- https://docs.m5stack.com/en/core/StickS3
- https://docs.m5stack.com/en/unit/Unit-QRCode
- https://github.com/m5stack/M5Unit-QRCode/tree/main/example/i2c_mode
- https://developers.google.com/apps-script/guides/web
- https://developers.google.com/apps-script/guides/content

最小版は履歴全体を読んで重複確認する。個人の少量データ向けであり、大量履歴の速度は未検証。
