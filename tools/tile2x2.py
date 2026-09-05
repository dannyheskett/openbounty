#!/usr/bin/env python3
"""Lay a 48x48 seamless tile 2x2 into the 96x96 pack tile (ART-PIPELINE, base terrain).

    python3 tools/tile2x2.py build/art/grass/run01/01_raw.png build/art/grass/run01/01_96.png
"""
import sys
from PIL import Image
src = Image.open(sys.argv[1]).convert("RGBA")
w, h = src.size
out = Image.new("RGBA", (w * 2, h * 2))
for y in (0, h):
    for x in (0, w):
        out.paste(src, (x, y))
out.save(sys.argv[2])
print(sys.argv[2], out.size)
