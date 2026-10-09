#!/usr/bin/env python3
"""通信や実機を使わず、保存応答・FIFO・再送進捗の状態試験を実行する。"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[3]
    candidates = [root / "platformio/include"]
    include = next((p for p in candidates if (p / "SaveStatus.h").is_file()), None)
    if include is None:
        raise SystemExit("Firmware headers not found")
    json_candidates = [Path(os.environ["ARDUINOJSON_INCLUDE"])] if os.environ.get("ARDUINOJSON_INCLUDE") else []
    json_candidates += [root / "tmp/test-deps/ArduinoJson/src"]
    json_candidates += list(root.glob("**/.pio/libdeps/*/ArduinoJson/src"))
    json_include = next((p for p in json_candidates if (p / "ArduinoJson.h").is_file()), None)
    if json_include is None:
        raise SystemExit("ArduinoJson 7.2.1 is required; set ARDUINOJSON_INCLUDE to its src directory")
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("Install a C++17 compiler or set CXX to its executable path")
    with tempfile.TemporaryDirectory(prefix="inventory-state-") as temporary:
        for source in sorted(Path(__file__).parent.glob("*.test.cpp")):
            binary = Path(temporary) / (source.stem + (".exe" if os.name == "nt" else ""))
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(include), "-I", str(json_include), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
