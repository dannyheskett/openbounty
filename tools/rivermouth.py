#!/usr/bin/env python3
"""A river mouth: a coast tile with a river running into its sea, the river's
water blending into the sea's across the tile.

    python3 tools/rivermouth.py <coast.png> <river_ew.png> <grass.png> <sea.png> <out.png>

The river enters from the WEST edge (a coast with its sea to the east). The
band is the river_ew piece's own shape -- every pixel where that piece differs
from the plain grass it was swept on -- so it joins the river_ew beside it
exactly. Across the tile each band pixel is mixed from the river piece's colour
at the west edge to the sea tile's colour at the east edge, on a smoothstep.

Processing generated art: Dan's one-off exception (2026-09-16), approved on the
river map mockup, because no generator gave a river-to-sea blend (two
create-tileset calls both drew a shoreline between the waters).
"""
import sys
from PIL import Image

coast_p, river_p, grass_p, sea_p, out_p = sys.argv[1:6]
S = 96
load = lambda p: Image.open(p).convert("RGBA").resize((S, S), Image.NEAREST)
coast, river, grass, sea = load(coast_p), load(river_p), load(grass_p), load(sea_p)
cw, rw, gw, sw = coast.load(), river.load(), grass.load(), sea.load()
for y in range(S):
    for x in range(S):
        if rw[x, y] == gw[x, y]:
            continue                       # outside the river's band
        t = x / (S - 1)
        t = t * t * (3 - 2 * t)
        r, s = rw[x, y], sw[x, y]
        cw[x, y] = tuple(int(r[i] * (1 - t) + s[i] * t) for i in range(3)) + (255,)
coast.save(out_p)
print("mouth ->", out_p)
