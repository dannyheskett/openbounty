#!/usr/bin/env python3
"""Composite the 48 terrain edge tiles from the finished base tiles.

For each terrain T in water/forest/mountain/desert and variant 01..12, the
reference edge 
(the original 48x34 tiles, kept under art/reference/edges/ at the repo root, water numbered 00-11 and the rest 01-12 as the pack codes them) is read as a shape: each pixel is terrain or grass by which base's colours it
is nearest. That mask is resized to the pack tile size and filled with the
new T base where it is terrain and the new grass base elsewhere, so the
edges seam with their bases by construction (ART-PIPELINE, terrain edges;
OPENBOUNTY-SPEC REQ-229). No generation.

    python3 tools/tileedges.py [pack-dir] [tile-set]

Default pack assets/glory-of-rome; with a tile set the bases are read from
and the edges written to art/tiles/<set>/.
"""
import os
import sys
from PIL import Image

PACK = sys.argv[1] if len(sys.argv) > 1 else "assets/glory-of-rome"
SET = sys.argv[2] if len(sys.argv) > 2 else ""
REF = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "art", "reference", "edges")
TILES = os.path.join(PACK, "art", "tiles", SET)
TERRAINS = ("water", "forest", "mountain", "desert")


def colours(path):
    return set(Image.open(path).convert("RGB").getdata())


def nearest(c, cols):
    return min((r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2 for r, g, b in cols)


def mask_from(ref_path, t_cols, g_cols, size):
    ref = Image.open(ref_path).convert("RGB")
    m = Image.new("L", ref.size, 0)
    px, mp = ref.load(), m.load()
    for y in range(ref.height):
        for x in range(ref.width):
            mp[x, y] = 255 if nearest(px[x, y], t_cols) <= nearest(px[x, y], g_cols) else 0
    return m.resize(size, Image.NEAREST)


def main():
    grass = Image.open(os.path.join(TILES, "grass.png")).convert("RGBA")
    g_cols = colours(os.path.join(REF, "grass.png"))
    n = 0
    for t in TERRAINS:
        base = Image.open(os.path.join(TILES, f"{t}.png")).convert("RGBA")
        t_cols = colours(os.path.join(REF, f"{t}.png"))
        for name in sorted(os.listdir(REF)):
            if not name.startswith(f"{t}_edge_"):
                continue
            m = mask_from(os.path.join(REF, name), t_cols, g_cols, base.size)
            out = grass.copy()
            out.paste(base, (0, 0), m)
            out.save(os.path.join(TILES, name))
            n += 1
    print(f"wrote {n} edge tiles to {TILES}")


if __name__ == "__main__":
    main()
