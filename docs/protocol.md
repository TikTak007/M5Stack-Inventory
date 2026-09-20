# 受信プロトコル v1（暫定）

HTTPS POST /exec、Content-Type: application/json。

```json
{
  "version": 1,
  "key": "設定した機器用キー（32文字以上）",
  "eventId": "test-event-0000000001",
  "code": "TEST-M5-001"
}
```

eventIdは16〜80文字の英数字・ハイフン。新しい操作ごとに新しいIDを作り、再試行では変更しない。
codeは1〜512文字のテキスト。機器側は512バイトまで。制御文字は禁止。
受信側はcodeを整形・trimせず、未知コードも文字列のまま保存する。
端末から数量を指定できず、1イベント=+1のみ。

保存・読戻し成功：`{"ok":true,"eventId":"test-event-0000000001","duplicate":false,"verified":true}`。
同じeventIdの再送ではduplicate=true。加算せず、読戻し成功時は同じくverified=trueを返す。
端末はokとeventIdに加えてverified=trueを確認した場合だけSAVEDにする。
失敗：`{"ok":false,"error":"BUSY"}` 等。HTTP 200でも失敗の場合がある。
UNAUTHORIZED / NOT_CONFIGURED / INVALID_REQUEST / SCHEMA_MISMATCH / EVENT_CONFLICT は設定やデータを修正する。
BUSY / STORAGE_ERROR / 通信タイムアウトでは同じIDで再試行する。
エラーは機器に詳細表示しない初版なので、設定確認はGoogle側とソースで行う。

公開前に、テスト用シートで上記の合成コードを使い、初回・同じID・別IDの順に送り、
保有数が1→1→2になることを確認する。個人の実在庫に混ぜない。

ScansのA〜H列は event_id / code / delta / received_at / unused_delta /
in_use_delta / disposed_delta / action。
端末からの新規登録は `delta=1, unused_delta=1, in_use_delta=0, disposed_delta=0, action=REGISTER`。
既存のA〜D形式から移行する際は、各行のdeltaをunused_deltaへ移して全保有分を未使用として扱う。
received_atは日付型のサーバー受信日時。読取り開始時刻ではない。
スプレッドシートのタイムゾーンは `Asia/Tokyo`、表示形式は `yyyy/MM/dd HH:mm:ss` とする。

受信した新しいcodeはProductMasterのA列へ文字列として1回だけ追加する。
ProductMasterは `コード / 製品名 / 画像URL / 参照元URL`。InventoryはScansの数量集計と
ProductMasterの完全一致検索を組み合わせ、代表画像を `IMAGE`、参照元を `HYPERLINK` で表示する。
ProductMasterへの登録失敗は保存確認できない状態として扱い、同じeventIdの再送で補完する。
M5Stack形式の新規SKUは、共有キー検証後にM5Stack公式SKU検索ページへSKUだけを送り、
製品名・先頭画像・参照元を取得する。認証情報、eventId、数量、日時は外部へ送らない。

Apps Scriptは `SpreadsheetApp.flush()` 後に対象行を直接読み戻し、同じeventIdとcodeの行が
存在しない場合は `STORAGE_ERROR` を返す。端末は未確認イベントを不揮発キューから削除しない。

Inventoryの操作欄による状態変更もScansへ1行追記する。event_idは `sheet-` とUUID、
received_atは操作時刻としてApps Scriptが自動生成する。保有数を変えない使用開始は
`delta=0, unused_delta=-1, in_use_delta=1`。廃棄は移動元を-1、disposed_deltaを+1、deltaを-1とする。
