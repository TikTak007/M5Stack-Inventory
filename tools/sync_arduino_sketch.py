#!/usr/bin/env python3
"""Arduino IDE用スケッチを更新し、ファイル内容の一致を検査する。"""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "platformio"
SKETCH = ROOT / "arduino" / "M5Stack_Inventory"


def expected_files() -> dict[Path, bytes]:
    return {
        SKETCH / "M5Stack_Inventory.ino": (FIRMWARE / "src" / "main.cpp").read_bytes(),
        SKETCH / "InventoryDisplay.h": (
            FIRMWARE / "include" / "InventoryDisplay.h"
        ).read_bytes(),
        SKETCH / "SaveStatus.h": (FIRMWARE / "include" / "SaveStatus.h").read_bytes(),
        SKETCH / "DurableOutbox.h": (FIRMWARE / "include" / "DurableOutbox.h").read_bytes(),
        SKETCH / "ReaderStartup.h": (FIRMWARE / "include" / "ReaderStartup.h").read_bytes(),
        SKETCH / "StickPowerStartup.h": (FIRMWARE / "include" / "StickPowerStartup.h").read_bytes(),
        SKETCH / "secrets.example.h": (
            FIRMWARE / "include" / "secrets.example.h"
        ).read_bytes(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail if generated files differ")
    args = parser.parse_args()

    expected = expected_files()
    if args.check:
        mismatches = [
            str(path.relative_to(ROOT))
            for path, content in expected.items()
            if not path.exists() or path.read_bytes() != content
        ]
        if mismatches:
            print("Arduino sketch is out of sync:")
            for mismatch in mismatches:
                print(f"  {mismatch}")
            return 1
        print("Arduino sketch matches the PlatformIO source.")
        return 0

    SKETCH.mkdir(parents=True, exist_ok=True)
    for path, content in expected.items():
        path.write_bytes(content)
        print(f"updated {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
