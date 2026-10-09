#!/usr/bin/env python3
"""SDLシミュレータのPPM出力をPNGへ一括変換する。"""

from pathlib import Path
import sys

from PIL import Image


folder = Path(sys.argv[1] if len(sys.argv) > 1 else "../../output/display-captures")
for source in sorted(folder.glob("*.ppm")):
    target = source.with_suffix(".png")
    Image.open(source).save(target)
    print(target)
