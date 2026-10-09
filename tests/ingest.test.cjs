/*
 * Apps ScriptをNode.jsのvmへ読み込み、Googleサービスをメモリ上の偽物へ差し替えて試験する。
 * 外部のGoogle Sheetsを変更せず、認証・冪等性・状態遷移・初期化を再現できる。
 */

const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const fs = require('node:fs');
const source = fs.readFileSync('apps-script/Code.gs', 'utf8');
const key = 'test-only-key-0000000000000000000000';

// 1試験ごとに独立したScans、ProductMaster、InventoryとGoogle APIモックを作る。
function fixture() {
  const rows = [[
    'event_id', 'code', 'delta', 'received_at',
    'unused_delta', 'in_use_delta', 'disposed_delta', 'action'
  ]];
  const productRows = [['コード', '製品名', '画像URL', '参照元URL']];
  let locked = false, busy = false, failFlush = false, dropScanAppend = false;
  let confirmationRead = null;
  const calls = {reads: [], flushes: 0, fetches: 0};
  // Apps Scriptが使うSheet APIだけを最小限実装する。
  const makeSheet = (data, literalColumn, name) => ({
    getLastRow: () => data.length,
    getRange: (r, c, n, m) => ({
      getValues: () => {
        calls.reads.push({sheet: name, r, c, n, m});
        if (name === 'Scans' && r > 1 && c === 1 && n === 1 && m === 2 && confirmationRead)
          return confirmationRead();
        return data.slice(r - 1, r - 1 + n).map(x => x.slice(c - 1, c - 1 + m));
      },
      setValues: values => values.forEach((row, ri) => row.forEach((value, ci) => {
        data[r - 1 + ri][c - 1 + ci] = value;
      }))
    }),
    appendRow: row => data.push(row.map((x, i) => i === literalColumn ? x.slice(1) : x)),
  });
  const book = {
    getSheetByName: name => name === 'Scans' ? sheet : name === 'ProductMaster' ? products : name === 'Inventory' ? inventory : null
  };
  const sheet = makeSheet(rows, 1, 'Scans');
  sheet.appendRow = row => {
    if (!dropScanAppend) rows.push(row.map((x, i) => i === 1 ? x.slice(1) : x));
  };
  const products = makeSheet(productRows, 0, 'ProductMaster');
  const inventory = makeSheet([[
    'コード', '保有数', '未使用', '使用中', '廃棄済み',
    '製品名', '代表画像', '参照元', '操作'
  ]], -1, 'Inventory');
  inventory.setRowHeights = () => {};
  sheet.getParent = () => book;
  // Code.gsから見えるGoogle Apps Scriptグローバルをテスト用に置き換える。
  const context = vm.createContext({
    ContentService: {MimeType: {JSON: 'json'}, createTextOutput: text => ({setMimeType: () => JSON.parse(text)})},
    PropertiesService: {getScriptProperties: () => ({getProperty: n => n === 'DEVICE_KEY' ? key : 'test-sheet'})},
    SpreadsheetApp: {openById: () => book, flush: () => {
      calls.flushes++;
      if (failFlush) throw Error('lost response');
    }},
    UrlFetchApp: {fetch: url => {
      calls.fetches++;
      const isExample = url.endsWith('/C999');
      const isTab5 = url.endsWith('/C145');
      return {
        getResponseCode: () => isExample || isTab5 ? 200 : 404,
        getContentText: () => isTab5
          ? '<h1>Tab5</h1><span class="product-sku">SKU:C145/K145</span>' +
            '<div class="carousel-images"><img src="https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/C145.webp"></div>'
          : '<h1>Example Unit</h1><span class="product-sku">SKU:C999</span>' +
            '<div class="carousel-images"><img src="https://static-cdn.m5stack.com/example.webp"></div>'
      };
    }},
    LockService: {getScriptLock: () => ({tryLock: () => locked = !busy, hasLock: () => locked, releaseLock: () => {locked = false;}})},
  });
  vm.runInContext(source, context);
  const send = body => context.doPost({postData: {contents: JSON.stringify(body)}});
  const request = {version: 1, key, eventId: 'scan-0000000000000001', code: '0012345678905'};
  return {rows, productRows, send, request, context, calls, locked: () => locked,
    busy: () => {busy = true;}, failFlush: value => {failFlush = value;},
    dropScanAppend: value => {dropScanAppend = value;},
    confirmationRead: reader => {confirmationRead = reader;}};
}
// -----------------------------------------------------------------------------
// 端末POST、重複防止、製品情報補完
// -----------------------------------------------------------------------------

test('new code, retry, and intentional second scan', () => {
  const f = fixture();
  const first = f.send(f.request);
  assert.equal(first.ok, true);
  assert.equal(first.verified, true);
  assert.equal(f.rows[1][1], '0012345678905');
  assert.equal(typeof f.rows[1][3].getTime, 'function');
  assert.deepEqual(Array.from(f.rows[1]).slice(4), [1, 0, 0, 'REGISTER']);
  const retry = f.send(f.request);
  assert.equal(retry.duplicate, true);
  assert.equal(retry.verified, true);
  assert.equal(f.rows.length, 2);
  assert.equal(f.send({...f.request, eventId: 'scan-0000000000000002'}).ok, true);
  assert.equal(f.rows.slice(1).reduce((n, r) => n + r[2], 0), 2);
  assert.equal(f.locked(), false);
});
test('reject conflicting event id', () => {
  const f = fixture(); f.send(f.request);
  assert.equal(f.send({...f.request, code: 'other'}).error, 'EVENT_CONFLICT');
  assert.equal(f.rows.length, 2);
});
test('auth and malformed input cannot mutate storage', () => {
  const f = fixture();
  assert.equal(f.send({...f.request, key: 'bad'}).error, 'UNAUTHORIZED');
  for (const code of ['', 'x'.repeat(513), 'a\nb', 123])
    assert.equal(f.send({...f.request, code}).error, 'INVALID_REQUEST');
  assert.equal(f.context.doPost({postData: {contents: '{'}}).error, 'INVALID_REQUEST');
  assert.equal(f.rows.length, 1);
});
test('formula-like codes retained as literal text', () => {
  const f = fixture();
  assert.equal(f.send({...f.request, code: '=1+1'}).ok, true);
  assert.equal(f.rows[1][1], '=1+1');
});
test('product master gets one row per code and known products get official metadata', () => {
  const f = fixture();
  assert.equal(f.send({...f.request, code: 'U173'}).ok, true);
  assert.equal(f.productRows.length, 2);
  assert.equal(f.productRows[1][0], 'U173');
  assert.match(f.productRows[1][1], /Unit QRCode/);
  assert.match(f.productRows[1][2], /^https:\/\/static-cdn\.m5stack\.com\//);
  assert.match(f.productRows[1][3], /^https:\/\/docs\.m5stack\.com\//);
  assert.equal(f.send({...f.request, eventId: 'scan-0000000000000002', code: 'U173'}).ok, true);
  assert.equal(f.productRows.length, 2);
});
test('unknown M5Stack SKU is completed from the official SKU route', () => {
  const f = fixture();
  assert.equal(f.send({...f.request, code: 'C999'}).ok, true);
  assert.deepEqual(Array.from(f.productRows[1]), [
    'C999',
    'Example Unit',
    'https://static-cdn.m5stack.com/example.webp',
    'https://docs.m5stack.com/en/products/sku/C999'
  ]);
});
test('multi-SKU official page accepts an exact listed variant', () => {
  const f = fixture();
  assert.equal(f.send({...f.request, code: 'C145'}).ok, true);
  assert.deepEqual(Array.from(f.productRows[1]), [
    'C145',
    'Tab5',
    'https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/C145.webp',
    'https://docs.m5stack.com/en/products/sku/C145'
  ]);
});
test('existing blank product row is completed after synchronization', () => {
  const f = fixture();
  f.productRows.push(['C145', '', '', '']);
  assert.equal(f.send({...f.request, code: 'C145'}).ok, true);
  assert.equal(f.productRows.length, 2);
  assert.deepEqual(Array.from(f.productRows[1]).slice(1), [
    'Tab5',
    'https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/C145.webp',
    'https://docs.m5stack.com/en/products/sku/C145'
  ]);
});
test('busy lock returns retryable error without append', () => {
  const f = fixture(); f.busy();
  assert.equal(f.send(f.request).error, 'BUSY');
  assert.equal(f.rows.length, 1);
});
test('lost acknowledgement after append does not duplicate on retry', () => {
  const f = fixture(); f.failFlush(true);
  assert.equal(f.send(f.request).error, 'STORAGE_ERROR');
  assert.equal(f.locked(), false);
  f.failFlush(false);
  assert.equal(f.send(f.request).duplicate, true);
  assert.equal(f.rows.length, 2);
});
test('success is never returned when the ledger row cannot be read back', () => {
  const f = fixture(); f.dropScanAppend(true);
  assert.equal(f.send(f.request).error, 'STORAGE_ERROR');
  assert.equal(f.rows.length, 1);
});
for (const duplicate of [false, true]) {
  for (const failure of ['event id mismatch', 'code mismatch', 'missing event id', 'missing code',
    'empty values', 'empty row', 'read error']) {
    test(`${duplicate ? 'retry' : 'new scan'} requires matching persisted id and code: ${failure}`, () => {
      const f = fixture();
      if (duplicate) assert.equal(f.send(f.request).verified, true);
      f.confirmationRead(() => {
        if (failure === 'read error') throw Error('read failed');
        if (failure === 'empty values') return [];
        if (failure === 'empty row') return [[]];
        if (failure === 'event id mismatch') return [['scan-0000000000000002', f.request.code]];
        if (failure === 'missing event id') return [[undefined, f.request.code]];
        if (failure === 'missing code') return [[f.request.eventId]];
        return [[f.request.eventId, 'other-code']];
      });
      assert.deepEqual(f.send(f.request), {ok: false, error: 'STORAGE_ERROR'});
      assert.equal(f.locked(), false);
      assert.equal(f.rows.length, 2);
      // 確認できない応答でも同じIDで再送し、確認後にだけ成功にする。
      f.confirmationRead(null);
      const retry = f.send(f.request);
      assert.equal(retry.ok, true);
      assert.equal(retry.duplicate, true);
      assert.equal(retry.verified, true);
      assert.equal(retry.eventId, f.request.eventId);
      assert.equal(f.rows.length, 2);
    });
  }
}
test('id confirmation reuses the existing two-column read without added flush or HTTP calls', () => {
  const f = fixture();
  assert.equal(f.send(f.request).verified, true);
  assert.deepEqual(f.calls.reads, [
    {sheet: 'Scans', r: 1, c: 1, n: 1, m: 8},
    {sheet: 'ProductMaster', r: 1, c: 1, n: 1, m: 4},
    {sheet: 'Inventory', r: 1, c: 1, n: 1, m: 9},
    {sheet: 'Scans', r: 2, c: 1, n: 1, m: 2}
  ]);
  assert.equal(f.calls.flushes, 1);
  assert.equal(f.calls.fetches, 0);
  f.calls.reads.length = 0;
  assert.equal(f.send(f.request).verified, true);
  assert.deepEqual(f.calls.reads, [
    {sheet: 'Scans', r: 1, c: 1, n: 1, m: 8},
    {sheet: 'ProductMaster', r: 1, c: 1, n: 1, m: 4},
    {sheet: 'Scans', r: 2, c: 1, n: 1, m: 8},
    {sheet: 'ProductMaster', r: 2, c: 1, n: 1, m: 1},
    {sheet: 'ProductMaster', r: 2, c: 2, n: 1, m: 3},
    {sheet: 'Scans', r: 2, c: 1, n: 1, m: 2}
  ]);
  assert.equal(f.calls.flushes, 2);
  assert.equal(f.calls.fetches, 0);
});
test('unexpected sheet headers block writes', () => {
  const f = fixture(); f.rows[0][0] = 'changed';
  assert.equal(f.send(f.request).error, 'SCHEMA_MISMATCH');
  assert.equal(f.rows.length, 1);
});

// -----------------------------------------------------------------------------
// Inventory上の状態変更
// -----------------------------------------------------------------------------

test('inventory status actions produce balanced ledger deltas', () => {
  const f = fixture();
  assert.deepEqual(
    Object.fromEntries(Object.entries(f.context.transitionForAction_(
      '使用開始', {unused: 1, inUse: 0, disposed: 0}
    ))),
    {delta: 0, unused: -1, inUse: 1, disposed: 0, eventType: 'START_USE'}
  );
  assert.deepEqual(
    Object.fromEntries(Object.entries(f.context.transitionForAction_(
      '使用中を廃棄', {unused: 0, inUse: 1, disposed: 0}
    ))),
    {delta: -1, unused: 0, inUse: -1, disposed: 1, eventType: 'DISPOSE_IN_USE'}
  );
  assert.deepEqual(
    Object.fromEntries(Object.entries(f.context.transitionForAction_(
      '未使用を廃棄', {unused: 1, inUse: 0, disposed: 0}
    ))),
    {delta: -1, unused: -1, inUse: 0, disposed: 1, eventType: 'DISPOSE_UNUSED'}
  );
});

test('inventory status action is rejected when its source stock is empty', () => {
  const f = fixture();
  assert.equal(f.context.transitionForAction_(
    '使用開始', {unused: 0, inUse: 0, disposed: 0}
  ), null);
  assert.equal(f.context.transitionForAction_(
    '使用中を廃棄', {unused: 1, inUse: 0, disposed: 0}
  ), null);
});

// 初期化試験用にデータ入り3シートと再構築処理の監視点を用意する。
function resetFixture() {
  const f = fixture();
  const scansRows = [
    ['event_id', 'code', 'delta', 'received_at', 'unused_delta', 'in_use_delta', 'disposed_delta', 'action'],
    ['scan-0000000000000001', 'C126', 1, new Date(), 1, 0, 0, 'REGISTER']
  ];
  const productRows = [
    ['コード', '製品名', '画像URL', '参照元URL'],
    ['C126', 'M5Stack AtomS3R', 'https://example.test/image.png', 'https://example.test/product']
  ];
  const inventoryRows = [
    ['コード', '保有数', '未使用', '使用中', '廃棄済み', '製品名', '代表画像', '参照元', '操作'],
    ['C126', 1, 1, 0, 0, 'M5Stack AtomS3R', '', '', '']
  ];
  const makeResetSheet = rows => ({
    getLastRow: () => {
      for (let i = rows.length - 1; i >= 0; i--)
        if (rows[i].some(value => value !== '' && value != null)) return i + 1;
      return 0;
    },
    getRange: (r, c, n, m) => ({
      getValues: () => Array.from({length: n}, (_, ri) =>
        Array.from({length: m}, (_, ci) => (rows[r - 1 + ri] || [])[c - 1 + ci])
      ),
      clearContent: () => {
        for (let ri = 0; ri < n; ri++)
          for (let ci = 0; ci < m; ci++) rows[r - 1 + ri][c - 1 + ci] = '';
      }
    })
  });
  const scans = makeResetSheet(scansRows);
  const products = makeResetSheet(productRows);
  const inventory = makeResetSheet(inventoryRows);
  const book = {getSheetByName: name => ({Scans: scans, ProductMaster: products, Inventory: inventory})[name]};
  let configuredProductMaster = false;
  let configuredInventoryCount = null;
  f.context.configureProductMaster_ = sheet => { configuredProductMaster = sheet === products; };
  f.context.configureInventory_ = (sheet, count) => {
    assert.equal(sheet, inventory);
    configuredInventoryCount = count;
  };
  return {f, book, scansRows, productRows, inventoryRows,
    configuredProductMaster: () => configuredProductMaster,
    configuredInventoryCount: () => configuredInventoryCount};
}

// -----------------------------------------------------------------------------
// 通常初期化・完全初期化・管理ボタン
// -----------------------------------------------------------------------------

test('inventory reset clears scans and inventory while preserving ProductMaster', () => {
  const r = resetFixture();
  r.f.context.resetData_(r.book, false);
  assert.ok(r.scansRows[1].every(value => value === ''));
  assert.equal(r.productRows[1][0], 'C126');
  assert.equal(r.configuredProductMaster(), true);
  assert.equal(r.configuredInventoryCount(), 0);
});

test('full reset also clears ProductMaster', () => {
  const r = resetFixture();
  r.f.context.resetData_(r.book, true);
  assert.ok(r.scansRows[1].every(value => value === ''));
  assert.ok(r.productRows[1].every(value => value === ''));
  assert.equal(r.configuredProductMaster(), true);
  assert.equal(r.configuredInventoryCount(), 0);
});

test('reset validates every sheet before clearing any data', () => {
  const r = resetFixture();
  r.productRows[0][0] = 'changed';
  assert.throws(() => r.f.context.resetData_(r.book, true), /SCHEMA_MISMATCH/);
  assert.equal(r.scansRows[1][0], 'scan-0000000000000001');
  assert.equal(r.productRows[1][0], 'C126');
});

test('reset button installation is repeatable and assigns both public functions', () => {
  const f = fixture();
  let removedOwnImage = false;
  let removedOtherImage = false;
  const inserted = [];
  f.context.Utilities = {
    base64Decode: value => Buffer.from(value, 'base64'),
    newBlob: (bytes, type, name) => ({bytes, type, name})
  };
  const inventory = {
    getImages: () => [
      {
        getAltTextTitle: () => 'M5Stack Inventory - Reset Inventory',
        remove: () => { removedOwnImage = true; }
      },
      {
        getAltTextTitle: () => 'Unrelated image',
        remove: () => { removedOtherImage = true; }
      }
    ],
    insertImage: (blob, column, row) => {
      const state = {blob, column, row};
      const image = {
        setAltTextTitle: value => (state.title = value, image),
        setAltTextDescription: value => (state.description = value, image),
        setWidth: value => (state.width = value, image),
        setHeight: value => (state.height = value, image),
        setAnchorCellXOffset: value => (state.x = value, image),
        setAnchorCellYOffset: value => (state.y = value, image),
        assignScript: value => (state.script = value, image)
      };
      inserted.push(state);
      return image;
    }
  };
  f.context.installResetButtons_(inventory);
  assert.equal(removedOwnImage, true);
  assert.equal(removedOtherImage, false);
  assert.deepEqual(inserted.map(item => ({
    column: item.column, row: item.row, width: item.width, height: item.height, script: item.script
  })), [
    {column: 10, row: 2, width: 210, height: 51, script: 'resetInventoryData'},
    {column: 10, row: 3, width: 210, height: 51, script: 'resetAllData'}
  ]);
  assert.ok(inserted.every(item => item.blob.type === 'image/png' && item.blob.bytes.length > 1000));
});
