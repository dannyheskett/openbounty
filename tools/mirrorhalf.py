#!/usr/bin/env python3
"""Make a tile symmetric by mirroring one half over the other.

    python3 tools/mirrorhalf.py in.png out.png bottom|top|left|right

The named half is kept and its mirror replaces the opposite half, so the
result is exactly symmetric about the tile's centre line. Used on the Rome
bridge tiles (2026-09-07) whose generated kerbs were thicker on one side."""
import sys
from PIL import Image
src, dst, keep = sys.argv[1], sys.argv[2], sys.argv[3]
im = Image.open(src)
w, h = im.size
if keep in ("bottom", "top"):
    half = im.crop((0, h // 2, w, h)) if keep == "bottom" else im.crop((0, 0, w, h // 2))
    mirror = half.transpose(Image.FLIP_TOP_BOTTOM)
    im.paste(half, (0, h // 2) if keep == "bottom" else (0, 0))
    im.paste(mirror, (0, 0) if keep == "bottom" else (0, h // 2))
else:
    half = im.crop((w // 2, 0, w, h)) if keep == "right" else im.crop((0, 0, w // 2, h))
    mirror = half.transpose(Image.FLIP_LEFT_RIGHT)
    im.paste(half, (w // 2, 0) if keep == "right" else (0, 0))
    im.paste(mirror, (0, 0) if keep == "right" else (w // 2, 0))
im.save(dst)
