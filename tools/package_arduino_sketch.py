#!/usr/bin/env python3
"""認証情報を含まないArduino IDE配布ZIPを作成する。"""

from __future__ import annotations

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

from sync_arduino_sketch import ROOT, SKETCH, expected_files


OUTPUT = ROOT / "output" / "arduino" / "M5Stack_Inventory_ArduinoIDE.zip"
# secrets.hは意図的に含めず、利用者がsecrets.example.hから作成する。
PACKAGE_FILES = (
    SKETCH / "M5Stack_Inventory.ino",
    SKETCH / "InventoryDisplay.h",
    SKETCH / "secrets.example.h",
    SKETCH / "README.md",
)


def main() -> int:
    expected = expected_files()
    mismatches = [
        path.relative_to(ROOT)
        for path, content in expected.items()
        if not path.exists() or path.read_bytes() != content
    ]
    if mismatches:
        print("Arduino sketch is out of sync; run tools/sync_arduino_sketch.py first:")
        for path in mismatches:
            print(f"  {path}")
        return 1

    missing = [path.relative_to(ROOT) for path in PACKAGE_FILES if not path.is_file()]
    if missing:
        print("Package input is missing:")
        for path in missing:
            print(f"  {path}")
        return 1

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    # ZIP直下にスケッチ名のフォルダーを作り、そのままArduino IDEで開ける形にする。
    with ZipFile(OUTPUT, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
        for path in PACKAGE_FILES:
            archive.write(path, Path(SKETCH.name) / path.name)

    print(f"created {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
