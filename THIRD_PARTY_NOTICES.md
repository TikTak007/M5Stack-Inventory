# Third-party software notices

本プロジェクトは、以下のオープンソースソフトウェアをビルド時の依存関係として使用します。
各ライブラリの著作権は、それぞれの著作権者に帰属します。

ライブラリ本体はこのリポジトリへ複製せず、PlatformIOまたはArduino Library Managerが取得します。
依存バージョンは`platformio/platformio.ini`とArduino IDE用READMEで固定しています。

## Direct dependencies

| Component | Version | Copyright / author | License |
|---|---:|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) | 0.2.22 | M5Stack | [MIT](https://github.com/m5stack/M5Unified/blob/master/LICENSE) |
| [M5GFX](https://github.com/m5stack/M5GFX) | 0.2.29 | M5Stack | [MIT](https://github.com/m5stack/M5GFX/blob/master/LICENSE) |
| [M5UnitQRCode](https://github.com/m5stack/M5Unit-QRCode) | 1.0.1 | M5Stack Technology CO LTD | MIT（配布パッケージ内`LICENSE`） |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | 7.2.1 | Benoit Blanchon | [MIT](https://github.com/bblanchon/ArduinoJson/blob/v7.2.1/LICENSE.txt) |
| [M5PM1](https://github.com/m5stack/M5PM1) | 1.0.7 | M5Stack | MIT（StickS3ビルド環境のみ） |

## LovyanGFX

M5GFXは、lovyan03氏が開発した
[LovyanGFX](https://github.com/lovyan03/LovyanGFX)を基盤としています。
LovyanGFXはFreeBSDライセンスで提供されています。

- Copyright (c) 2020 lovyan03
- [LovyanGFX license](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)
- [M5GFXが示す構成要素別ライセンス](https://github.com/m5stack/M5GFX#license)

LovyanGFXのFreeBSDライセンスでは、ソースコードまたはバイナリを再配布する際に、
著作権表示、条件一覧、免責条項を保持または付属資料へ掲載することが求められます。

## Fonts used by the device UI

端末画面では、M5GFXに収録された次のフォントを使用しています。

| Font in source | Origin | License |
|---|---|---|
| `FreeSansBold12pt7b` | Adafruit GFX font | 2-clause BSD |
| `efontJA_10_b`, `efontJA_14_b`, `efontJA_16_b` | The Electronic Font Open Laboratory | 3-clause BSD |

ライセンス本文はM5GFX配布物の次の場所に含まれます。

- `src/lgfx/Fonts/GFXFF/license.txt`
- `src/lgfx/Fonts/efont/COPYRIGHT.txt`

## Binary distribution

コンパイル済みファームウェアを配布する場合は、使用したバージョンのライブラリに同梱された
ライセンス本文と著作権表示を、ファームウェアの配布物または付属資料にも収録してください。
M5GFXにはLovyanGFX以外の第三者コードも含まれるため、
[M5GFXのライセンス一覧](https://github.com/m5stack/M5GFX#license)に記載された条件も確認してください。

このファイルは第三者ソフトウェアに関する表記です。本プロジェクト自身のライセンスは、
リポジトリを一般公開する前に別途決定します。
