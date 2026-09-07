#!/usr/bin/env python3
"""Centre-crop a generated still to the pack size.

    python3 tools/cropcentre.py in.png out.png [size]

The rd_pro__default engine draws a painted frame round most 96x96 portraits.
Generating at 128x128 and keeping the centre 96x96 discards up to 16px of
frame on each side with no resampling. Approved for villain portraits only
(2026-09-07)."""
import sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
size = int(sys.argv[3]) if len(sys.argv) > 3 else 96
im = Image.open(src)
x = (im.width - size) // 2
y = (im.height - size) // 2
im.crop((x, y, x + size, y + size)).save(dst)
