/*
 * M5Stack Inventory - Google Apps Script
 * K Visualization Studio
 * T.KAMIKURA
 * 2026-09-20
 *
 * Scansへ在庫イベントを追記し、Inventoryはその履歴から再集計する。
 * 同じevent_idの再送を重複登録せず、通信応答が失われても安全に再試行できる。
 */

// シート構成をコード側でも固定し、手作業による列変更を検出して誤記録を防ぐ。
const LEGACY_SCAN_HEADERS = ['event_id', 'code', 'delta', 'received_at'];
const HEADERS = [
  'event_id', 'code', 'delta', 'received_at',
  'unused_delta', 'in_use_delta', 'disposed_delta', 'action'
];
const PRODUCT_HEADERS = ['コード', '製品名', '画像URL', '参照元URL'];
const LEGACY_INVENTORY_HEADERS = ['コード', '登録数量', '製品名', '代表画像', '参照元'];
const INVENTORY_HEADERS = [
  'コード', '保有数', '未使用', '使用中', '廃棄済み',
  '製品名', '代表画像', '参照元', '操作'
];
const INVENTORY_ACTIONS = ['使用開始', '使用中を廃棄', '未使用を廃棄'];
const INVENTORY_FORMULA_ROWS = 500;
const TIME_ZONE = 'Asia/Tokyo';
const DATE_FORMAT = 'yyyy/MM/dd HH:mm:ss';

// 管理ボタンはApps Script単体で配置できるよう、PNGをBase64で保持する。
const RESET_INVENTORY_PNG_BASE64 =
  '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' +
  'AAAAAElFTkSuQmCC';

const FULL_RESET_PNG_BASE64 =
  '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' +
  'Gg/e4wAAAABJRU5ErkJggg==';

function json_(value) {
  return ContentService.createTextOutput(JSON.stringify(value))
    .setMimeType(ContentService.MimeType.JSON);
}
function sheet_() {
  const id = PropertiesService.getScriptProperties().getProperty('SPREADSHEET_ID');
  if (!id) throw new Error('NOT_CONFIGURED');
  const sheet = SpreadsheetApp.openById(id).getSheetByName('Scans');
  if (!sheet || JSON.stringify(sheet.getRange(1, 1, 1, HEADERS.length).getValues()[0]) !==
      JSON.stringify(HEADERS))
    throw new Error('SCHEMA_MISMATCH');
  return sheet;
}
function productSheet_(book) {
  const sheet = book.getSheetByName('ProductMaster');
  if (!sheet || JSON.stringify(sheet.getRange(1, 1, 1, 4).getValues()[0]) !==
      JSON.stringify(PRODUCT_HEADERS)) throw new Error('SCHEMA_MISMATCH');
  return sheet;
}
function knownProduct_(code) {
  const products = {
    C126: {
      name: 'M5Stack AtomS3R',
      imageUrl: 'https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/680/C126_AtomS3R_main_pictures_03.jpg',
      sourceUrl: 'https://docs.m5stack.com/en/core/AtomS3R'
    },
    A103: {
      name: 'M5Stack Atomic CAN Base',
      imageUrl: 'https://static-cdn.m5stack.com/resource/docs/products/atom/Atomic%20CAN%20Base/img-8c8eb3c4-9657-4330-9e1a-a0f5f3290197.webp',
      sourceUrl: 'https://docs.m5stack.com/en/atom/Atomic%20CAN%20Base'
    },
    U173: {
      name: 'M5Stack Unit QRCode',
      imageUrl: 'https://static-cdn.m5stack.com/resource/docs/products/unit/Unit-QRCode/img-aa3b7064-8e0c-43c7-807e-40cea135ffa7.webp',
      sourceUrl: 'https://docs.m5stack.com/en/unit/Unit-QRCode'
    },
    U188: {
      name: 'M5Stack Unit RollerCAN',
      imageUrl: 'https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/unit/Unit-RollerCAN/4.webp',
      sourceUrl: 'https://docs.m5stack.com/en/unit/Unit-RollerCAN'
    }
  };
  return products[code] || null;
}
function decodeHtml_(value) {
  return value
    .replace(/<[^>]*>/g, '')
    .replace(/&amp;/g, '&')
    .replace(/&quot;/g, '"')
    .replace(/&#39;|&apos;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .trim();
}
// 未登録SKUをM5Stack公式SKUページだけから補完する。非公式画像URLは採用しない。
function fetchM5StackProduct_(code) {
  if (!/^[A-Za-z][A-Za-z0-9-]{2,31}$/.test(code)) return null;
  const sku = code.toUpperCase();
  const sourceUrl = 'https://docs.m5stack.com/en/products/sku/' + encodeURIComponent(sku);
  let response;
  try {
    response = UrlFetchApp.fetch(sourceUrl, {
      followRedirects: true,
      muteHttpExceptions: true,
      headers: {'User-Agent': 'M5Stack-Inventory/1.0'}
    });
  } catch (_) {
    return null;
  }
  if (response.getResponseCode() !== 200) return null;
  const html = response.getContentText();
  const skuMatch = html.match(/class="product-sku"[^>]*>\s*SKU:\s*([^<\s]+)/i);
  if (!skuMatch) return null;
  // 公式ページにはC145/K145のように複数SKUを併記する場合がある。
  // SKU全体で照合し、C145は受理するが部分一致のC14は受理しない。
  const listedSkus = skuMatch[1].toUpperCase().split(/[^A-Z0-9-]+/).filter(Boolean);
  if (!listedSkus.includes(sku)) return null;
  const nameMatch = html.match(/<h1[^>]*>([\s\S]*?)<\/h1>/i);
  const carousel = html.match(/class="carousel-images"[\s\S]*?<img[^>]+src="([^"]+)"/i);
  if (!nameMatch || !carousel) return null;
  const imageUrl = carousel[1].replace(/&amp;/g, '&').replace(/ /g, '%20');
  if (!/^https:\/\/(?:static-cdn\.m5stack\.com|m5stack(?:-doc)?\.oss-cn-shenzhen\.aliyuncs\.com)\//i.test(imageUrl))
    return null;
  return {name: decodeHtml_(nameMatch[1]), imageUrl, sourceUrl};
}
function productMetadata_(code) {
  return knownProduct_(code) || fetchM5StackProduct_(code);
}
// 手入力済みの製品名・画像・参照元は保持し、空欄だけを公式情報で補完する。
function ensureProductCode_(products, code) {
  const lastRow = products.getLastRow();
  const codes = lastRow > 1 ? products.getRange(2, 1, lastRow - 1, 1).getValues() : [];
  const found = codes.findIndex(row => String(row[0]) === code);
  if (found < 0) {
    const metadata = productMetadata_(code);
    products.appendRow([
      "'" + code,
      metadata ? metadata.name : '',
      metadata ? metadata.imageUrl : '',
      metadata ? metadata.sourceUrl : ''
    ]);
    return;
  }
  const detailRange = products.getRange(found + 2, 2, 1, 3);
  const details = detailRange.getValues()[0];
  if (details.every(Boolean)) return;
  const metadata = productMetadata_(code);
  if (!metadata) return;
  const completed = [
    details[0] || metadata.name,
    details[1] || metadata.imageUrl,
    details[2] || metadata.sourceUrl
  ];
  if (JSON.stringify(details) !== JSON.stringify(completed)) detailRange.setValues([completed]);
}
function validate_(body, key) {
  if (!key || key.length < 32) throw new Error('NOT_CONFIGURED');
  if (!body || body.key !== key) throw new Error('UNAUTHORIZED');
  if (body.version !== 1 || typeof body.eventId !== 'string' ||
      !/^[a-zA-Z0-9-]{16,80}$/.test(body.eventId) || typeof body.code !== 'string' ||
      body.code.length < 1 || body.code.length > 512 ||
      /[\u0000-\u001f\u007f]/.test(body.code)) throw new Error('INVALID_REQUEST');
  return {eventId: body.eventId, code: body.code};
}
// event_idが既存なら同じ結果を返し、端末側の再送で在庫を二重加算しない。
function doPost(e) {
  let lock;
  try {
    const raw = e && e.postData && e.postData.contents;
    if (!raw || raw.length > 4096) throw new Error('INVALID_REQUEST');
    let body;
    try { body = JSON.parse(raw); } catch (_) { throw new Error('INVALID_REQUEST'); }
    const request = validate_(body, PropertiesService.getScriptProperties().getProperty('DEVICE_KEY'));
    lock = LockService.getScriptLock();
    if (!lock.tryLock(10000)) throw new Error('BUSY');
    const sheet = sheet_();
    const products = productSheet_(sheet.getParent());
    const rows = sheet.getLastRow() > 1
      ? sheet.getRange(2, 1, sheet.getLastRow() - 1, HEADERS.length).getValues() : [];
    const priorIndex = rows.findIndex(row => row[0] === request.eventId);
    const prior = priorIndex >= 0 ? rows[priorIndex] : null;
    if (prior && prior[1] !== request.code) throw new Error('EVENT_CONFLICT');
    const confirmationRow = prior ? priorIndex + 2 : sheet.getLastRow() + 1;
    const newProductCode = !rows.some(row => row[1] === request.code);
    if (!prior) {
      // 先頭のアポストロフィで数式実行を防ぎ、先頭ゼロも含めて文字列として保存する。
      // 日時は文字列ではなく日付型にし、JST表示と時系列の並べ替えを両立する。
      sheet.appendRow([request.eventId, "'" + request.code, 1, new Date(), 1, 0, 0, 'REGISTER']);
    }
    ensureProductCode_(products, request.code);
    const inventory = sheet.getParent().getSheetByName('Inventory');
    const codeCount = new Set(rows.map(row => String(row[1])).concat(request.code)).size;
    if (newProductCode && inventory &&
        JSON.stringify(inventory.getRange(1, 1, 1, INVENTORY_HEADERS.length).getValues()[0]) ===
        JSON.stringify(INVENTORY_HEADERS)) inventory.setRowHeights(2, codeCount, 92);
    SpreadsheetApp.flush();
    // appendRowの完了だけでは成功とせず、flush後に同じeventId/codeの行を読み戻す。
    // この確認が通った場合だけ端末へverified:trueを返す。
    const confirmed = sheet.getRange(confirmationRow, 1, 1, 2).getValues()[0] || [];
    if (!confirmed || confirmed[1] !== request.code) throw new Error('STORAGE_ERROR');
    return json_({ok: true, eventId: request.eventId, duplicate: !!prior, verified: true});
  } catch (error) {
    const allowed = ['NOT_CONFIGURED', 'UNAUTHORIZED', 'INVALID_REQUEST', 'BUSY', 'SCHEMA_MISMATCH', 'EVENT_CONFLICT'];
    return json_({ok: false, error: allowed.includes(error.message) ? error.message : 'STORAGE_ERROR'});
  } finally {
    if (lock && lock.hasLock()) lock.releaseLock();
  }
}
// 公開URLのGETには稼働確認だけを返し、在庫内容は含めない。
function doGet() { return json_({ok: true, service: 'inventory-ingest', version: 1}); }

// 旧版のISO文字列を日付型へ変換し、JST表示・並べ替えを正しくする。
function normalizeReceivedAt_(scans) {
  const lastRow = scans.getLastRow();
  if (lastRow <= 1) return;
  const range = scans.getRange(2, 4, lastRow - 1, 1);
  const values = range.getValues();
  let changed = false;
  values.forEach(row => {
    if (typeof row[0] !== 'string' ||
        !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{3})?Z$/.test(row[0])) return;
    const parsed = new Date(row[0]);
    if (Number.isNaN(parsed.getTime())) return;
    row[0] = parsed;
    changed = true;
  });
  if (changed) range.setValues(values);
}

// 旧4列Scansを状態管理対応の8列へ、安全に拡張する。
function migrateScans_(scans) {
  const current = scans.getRange(1, 1, 1, HEADERS.length).getValues()[0];
  if (JSON.stringify(current) === JSON.stringify(HEADERS)) return;
  const legacy = current.slice(0, LEGACY_SCAN_HEADERS.length);
  if (JSON.stringify(legacy) !== JSON.stringify(LEGACY_SCAN_HEADERS) ||
      current.slice(LEGACY_SCAN_HEADERS.length).some(Boolean)) throw new Error('SCHEMA_MISMATCH');
  const lastRow = scans.getLastRow();
  if (lastRow > 1) {
    const deltas = scans.getRange(2, 3, lastRow - 1, 1).getValues();
    scans.getRange(2, 5, deltas.length, 4).setValues(deltas.map(row => {
      const delta = Number(row[0]) || 0;
      return [delta, 0, 0, delta >= 0 ? 'REGISTER' : 'ADJUST_UNUSED'];
    }));
  }
  scans.getRange(1, 1, 1, HEADERS.length).setValues([HEADERS]);
}

function statusCountsForCode_(scans, code) {
  const lastRow = scans.getLastRow();
  if (lastRow <= 1) return {unused: 0, inUse: 0, disposed: 0};
  return scans.getRange(2, 2, lastRow - 1, 6).getValues()
    .filter(row => String(row[0]) === code)
    .reduce((counts, row) => ({
      unused: counts.unused + (Number(row[3]) || 0),
      inUse: counts.inUse + (Number(row[4]) || 0),
      disposed: counts.disposed + (Number(row[5]) || 0)
    }), {unused: 0, inUse: 0, disposed: 0});
}

function transitionForAction_(action, counts) {
  if (action === '使用開始' && counts.unused > 0)
    return {delta: 0, unused: -1, inUse: 1, disposed: 0, eventType: 'START_USE'};
  if (action === '使用中を廃棄' && counts.inUse > 0)
    return {delta: -1, unused: 0, inUse: -1, disposed: 1, eventType: 'DISPOSE_IN_USE'};
  if (action === '未使用を廃棄' && counts.unused > 0)
    return {delta: -1, unused: -1, inUse: 0, disposed: 1, eventType: 'DISPOSE_UNUSED'};
  return null;
}

// Inventoryの「操作」列で選ばれた状態変更もScansへイベントとして残す。
function onEdit(e) {
  if (!e || !e.range || e.range.getSheet().getName() !== 'Inventory' ||
      e.range.getRow() < 2 || e.range.getColumn() !== 9 ||
      !INVENTORY_ACTIONS.includes(String(e.value || ''))) return;
  const action = String(e.value);
  const inventory = e.range.getSheet();
  const book = inventory.getParent();
  const code = String(inventory.getRange(e.range.getRow(), 1).getValue() || '');
  let lock;
  try {
    if (!code) throw new Error('NO_PRODUCT');
    lock = LockService.getScriptLock();
    if (!lock.tryLock(5000)) throw new Error('BUSY');
    const scans = book.getSheetByName('Scans');
    if (!scans || JSON.stringify(scans.getRange(1, 1, 1, HEADERS.length).getValues()[0]) !==
        JSON.stringify(HEADERS)) throw new Error('SCHEMA_MISMATCH');
    const transition = transitionForAction_(action, statusCountsForCode_(scans, code));
    if (!transition) throw new Error('NO_STOCK');
    scans.appendRow([
      'sheet-' + Utilities.getUuid(), "'" + code, transition.delta, new Date(),
      transition.unused, transition.inUse, transition.disposed, transition.eventType
    ]);
    SpreadsheetApp.flush();
    book.toast(code + ': ' + action + ' を記録しました', 'Inventory', 4);
  } catch (error) {
    const message = error.message === 'NO_STOCK' ? '移動元の在庫がありません' :
      error.message === 'BUSY' ? '処理中です。もう一度選択してください' :
      '状態を更新できませんでした';
    book.toast(message, 'Inventory', 5);
  } finally {
    e.range.clearContent();
    if (lock && lock.hasLock()) lock.releaseLock();
  }
}

function onOpen() {
  SpreadsheetApp.getUi()
    .createMenu('在庫管理')
    .addItem('在庫データを初期化', 'resetInventoryData')
    .addSeparator()
    .addItem('完全初期化', 'resetAllData')
    .addToUi();
}

function inventoryBook_() {
  const id = PropertiesService.getScriptProperties().getProperty('SPREADSHEET_ID');
  if (!id) throw new Error('NOT_CONFIGURED');
  return SpreadsheetApp.openById(id);
}

// 初期化対象3シートを先にすべて検証し、部分的な削除を防ぐ。
function resetSheets_(book) {
  const scans = book.getSheetByName('Scans');
  const inventory = book.getSheetByName('Inventory');
  const products = book.getSheetByName('ProductMaster');
  const valid = scans && inventory && products &&
    JSON.stringify(scans.getRange(1, 1, 1, HEADERS.length).getValues()[0]) === JSON.stringify(HEADERS) &&
    JSON.stringify(inventory.getRange(1, 1, 1, INVENTORY_HEADERS.length).getValues()[0]) ===
      JSON.stringify(INVENTORY_HEADERS) &&
    JSON.stringify(products.getRange(1, 1, 1, PRODUCT_HEADERS.length).getValues()[0]) ===
      JSON.stringify(PRODUCT_HEADERS);
  if (!valid) throw new Error('SCHEMA_MISMATCH');
  return {scans, inventory, products};
}

function clearDataRows_(sheet, columnCount) {
  const lastRow = sheet.getLastRow();
  if (lastRow > 1) sheet.getRange(2, 1, lastRow - 1, columnCount).clearContent();
}

function resetData_(book, clearProducts) {
  // 1枚でも列構成が違えば削除前に停止し、一部のシートだけ消える事故を防ぐ。
  const sheets = resetSheets_(book);
  clearDataRows_(sheets.scans, HEADERS.length);
  if (clearProducts) clearDataRows_(sheets.products, PRODUCT_HEADERS.length);
  configureProductMaster_(sheets.products);
  configureInventory_(sheets.inventory, 0);
  SpreadsheetApp.flush();
}

function runReset_(clearProducts) {
  const book = inventoryBook_();
  const lock = LockService.getScriptLock();
  if (!lock.tryLock(10000)) throw new Error('BUSY');
  try {
    resetData_(book, clearProducts);
  } finally {
    if (lock.hasLock()) lock.releaseLock();
  }
}

function resetInventoryData() {
  const ui = SpreadsheetApp.getUi();
  const answer = ui.alert(
    '在庫データを初期化',
    'Scansの履歴とInventoryの在庫をすべて消去します。ProductMasterは保持します。\n\n' +
      'AtomS3に未送信データがある場合は再登録されるため、送信完了を確認してください。実行しますか？',
    ui.ButtonSet.YES_NO
  );
  if (answer !== ui.Button.YES) return;
  try {
    runReset_(false);
    inventoryBook_().toast('ScansとInventoryを初期化しました', '在庫管理', 5);
  } catch (error) {
    ui.alert('初期化できませんでした', error.message === 'BUSY' ?
      '処理中です。少し待ってから再実行してください。' : 'シート構成を確認してください。', ui.ButtonSet.OK);
  }
}

function resetAllData() {
  const ui = SpreadsheetApp.getUi();
  const answer = ui.prompt(
    '完全初期化',
    'Scans、Inventory、ProductMasterの全データを消去します。元に戻せません。\n\n' +
      'AtomS3の送信完了を確認し、実行する場合は「完全初期化」と入力してください。',
    ui.ButtonSet.OK_CANCEL
  );
  if (answer.getSelectedButton() !== ui.Button.OK || answer.getResponseText() !== '完全初期化') return;
  try {
    runReset_(true);
    inventoryBook_().toast('すべての在庫データを初期化しました', '在庫管理', 5);
  } catch (error) {
    ui.alert('完全初期化できませんでした', error.message === 'BUSY' ?
      '処理中です。少し待ってから再実行してください。' : 'シート構成を確認してください。', ui.ButtonSet.OK);
  }
}

function styleHeader_(sheet, columnCount) {
  sheet.getRange(1, 1, 1, columnCount)
    .setBackground('#e8eaed')
    .setFontColor('#202124')
    .setFontWeight('bold')
    .setVerticalAlignment('middle');
  sheet.setFrozenRows(1);
}

function configureProductMaster_(products) {
  products.getRange(1, 1, 1, 4).setValues([PRODUCT_HEADERS]);
  products.getRange('A:A').setNumberFormat('@');
  styleHeader_(products, 4);
  products.setColumnWidth(1, 110);
  products.setColumnWidth(2, 260);
  products.setColumnWidth(3, 420);
  products.setColumnWidth(4, 420);
}

function syncProductMaster_(scans, products) {
  const lastRow = scans.getLastRow();
  if (lastRow <= 1) return;
  const codes = scans.getRange(2, 2, lastRow - 1, 1).getValues()
    .map(row => String(row[0]))
    .filter(code => code.length > 0);
  [...new Set(codes)].forEach(code => ensureProductCode_(products, code));
}

// 指定した代替テキストの管理ボタンだけを置き換える。
function installResetButtons_(inventory) {
  const buttons = [
    {
      title: 'M5Stack Inventory - Reset Inventory',
      data: RESET_INVENTORY_PNG_BASE64,
      row: 2,
      script: 'resetInventoryData'
    },
    {
      title: 'M5Stack Inventory - Full Reset',
      data: FULL_RESET_PNG_BASE64,
      row: 3,
      script: 'resetAllData'
    }
  ];
  const titles = new Set(buttons.map(button => button.title));
  inventory.getImages().forEach(image => {
    if (titles.has(image.getAltTextTitle())) image.remove();
  });
  buttons.forEach(button => {
    const blob = Utilities.newBlob(
      Utilities.base64Decode(button.data),
      'image/png',
      button.script + '.png'
    );
    inventory.insertImage(blob, 10, button.row)
      .setAltTextTitle(button.title)
      .setAltTextDescription('Click to run ' + button.script)
      .setWidth(210)
      .setHeight(51)
      .setAnchorCellXOffset(10)
      .setAnchorCellYOffset(20)
      .assignScript(button.script);
  });
}

function configureResetControls_(inventory) {
  inventory.getRange('J1')
    .setValue('管理')
    .setBackground('#e8eaed')
    .setFontColor('#202124')
    .setFontWeight('bold')
    .setHorizontalAlignment('center');
  inventory.setColumnWidth(10, 230);
  inventory.setRowHeights(2, 2, 92);
}

// Scansの数量集計とProductMasterの製品情報をInventoryへ数式で表示する。
function configureInventory_(inventory, codeCount) {
  inventory.getRange('A2').clearContent();
  inventory.getRange(2, 1, inventory.getMaxRows() - 1, INVENTORY_HEADERS.length).clearContent();
  inventory.getRange(1, 1, 1, INVENTORY_HEADERS.length).setValues([INVENTORY_HEADERS]);
  inventory.getRange('A:A').setNumberFormat('@');
  inventory.getRange('A2').setFormula(
    '=IFERROR(QUERY(Scans!B2:G,"select B, sum(C), sum(E), sum(F), sum(G) where B is not null group by B label B \'\', sum(C) \'\', sum(E) \'\', sum(F) \'\', sum(G) \'\'",0),"")'
  );
  const formulas = [];
  for (let row = 2; row <= INVENTORY_FORMULA_ROWS; row++) {
    formulas.push([
      `=IF(A${row}="","",IFNA(VLOOKUP(A${row},ProductMaster!A:D,2,FALSE),"未登録"))`,
      `=IF(A${row}="","",IFNA(IMAGE(VLOOKUP(A${row},ProductMaster!A:D,3,FALSE),1),""))`,
      `=IF(A${row}="","",IFNA(HYPERLINK(VLOOKUP(A${row},ProductMaster!A:D,4,FALSE),"元画像"),""))`
    ]);
  }
  inventory.getRange(2, 6, formulas.length, 3).setFormulas(formulas);
  const actionRule = SpreadsheetApp.newDataValidation()
    .requireValueInList(INVENTORY_ACTIONS, true)
    .setAllowInvalid(false)
    .build();
  inventory.getRange(2, 9, INVENTORY_FORMULA_ROWS - 1, 1).setDataValidation(actionRule);
  styleHeader_(inventory, INVENTORY_HEADERS.length);
  inventory.setColumnWidth(1, 110);
  inventory.setColumnWidth(2, 75);
  inventory.setColumnWidth(3, 75);
  inventory.setColumnWidth(4, 75);
  inventory.setColumnWidth(5, 75);
  inventory.setColumnWidth(6, 260);
  inventory.setColumnWidth(7, 130);
  inventory.setColumnWidth(8, 100);
  inventory.setColumnWidth(9, 150);
  inventory.getRange(2, 1, INVENTORY_FORMULA_ROWS - 1, INVENTORY_HEADERS.length)
    .setVerticalAlignment('middle');
  if (codeCount > 0) inventory.setRowHeights(2, codeCount, 92);
  configureResetControls_(inventory);
}

// 空のシートまたは対応済み旧形式だけを構成し、未知の既存データは上書きしない。
function setup() {
  const props = PropertiesService.getScriptProperties();
  if (!props.getProperty('DEVICE_KEY') || props.getProperty('DEVICE_KEY').length < 32)
    throw new Error('Set DEVICE_KEY (32+ characters) in Script Properties first');
  const book = SpreadsheetApp.openById(props.getProperty('SPREADSHEET_ID'));
  book.setSpreadsheetTimeZone(TIME_ZONE);
  const existingScans = book.getSheetByName('Scans');
  const existingInventory = book.getSheetByName('Inventory');
  const existingProducts = book.getSheetByName('ProductMaster');
  const scansHeader = existingScans
    ? existingScans.getRange(1, 1, 1, HEADERS.length).getValues()[0] : [];
  const scansReady = existingScans && JSON.stringify(scansHeader) === JSON.stringify(HEADERS);
  const scansLegacy = existingScans &&
    JSON.stringify(scansHeader.slice(0, LEGACY_SCAN_HEADERS.length)) ===
      JSON.stringify(LEGACY_SCAN_HEADERS) &&
    !scansHeader.slice(LEGACY_SCAN_HEADERS.length).some(Boolean);
  const inventoryLegacy = existingInventory && existingInventory.getLastRow() === 1 &&
    existingInventory.getLastColumn() === 1 &&
    existingInventory.getRange('A1').getFormula().startsWith('=QUERY(Scans!B:C,');
  const inventoryV1 = existingInventory &&
    JSON.stringify(existingInventory.getRange(1, 1, 1, 2).getValues()[0]) ===
      JSON.stringify(['コード（製品対応は未確認）', '登録数量']) &&
    existingInventory.getRange('A2').getFormula().startsWith('=IFERROR(QUERY(Scans!B2:C,');
  const inventoryReady = existingInventory &&
    JSON.stringify(existingInventory.getRange(1, 1, 1, INVENTORY_HEADERS.length).getValues()[0]) ===
      JSON.stringify(INVENTORY_HEADERS) &&
    existingInventory.getRange('A2').getFormula().startsWith('=IFERROR(QUERY(Scans!B2:G,');
  const inventoryPrevious = existingInventory &&
    JSON.stringify(existingInventory.getRange(1, 1, 1, LEGACY_INVENTORY_HEADERS.length).getValues()[0]) ===
      JSON.stringify(LEGACY_INVENTORY_HEADERS) &&
    existingInventory.getRange('A2').getFormula().startsWith('=IFERROR(QUERY(Scans!B2:C,');
  const productsReady = existingProducts &&
    JSON.stringify(existingProducts.getRange(1, 1, 1, 4).getValues()[0]) ===
      JSON.stringify(PRODUCT_HEADERS);
  if ((existingScans && !existingScans.getDataRange().isBlank() && !scansReady && !scansLegacy) ||
      (existingInventory && !existingInventory.getDataRange().isBlank() &&
       !inventoryLegacy && !inventoryV1 && !inventoryPrevious && !inventoryReady) ||
      (existingProducts && !existingProducts.getDataRange().isBlank() && !productsReady))
    throw new Error('Existing non-empty sheets found; setup never overwrites them');
  const scans = existingScans || book.insertSheet('Scans');
  if (!existingScans || existingScans.getDataRange().isBlank())
    scans.getRange(1, 1, 1, HEADERS.length).setValues([HEADERS]);
  else migrateScans_(scans);
  scans.getRange('A:B').setNumberFormat('@');
  normalizeReceivedAt_(scans);
  scans.getRange('D:D').setNumberFormat(DATE_FORMAT);
  styleHeader_(scans, HEADERS.length);
  const products = existingProducts || book.insertSheet('ProductMaster');
  configureProductMaster_(products);
  syncProductMaster_(scans, products);
  const inventory = existingInventory || book.insertSheet('Inventory');
  const codeCount = scans.getLastRow() > 1 ? new Set(
    scans.getRange(2, 2, scans.getLastRow() - 1, 1).getValues()
      .map(row => String(row[0])).filter(Boolean)
  ).size : 0;
  configureInventory_(inventory, codeCount);
  installResetButtons_(inventory);
  SpreadsheetApp.flush();
}
