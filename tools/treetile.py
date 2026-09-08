#!/usr/bin/env python3
"""Compose 96px terrain tiles from hand-placed 32px sprites.

    python3 tools/treetile.py <layout.json> <out-dir>

The layout file is the record of how every tile was made:

  {
    "sprites": "build/art/pixellab/o32_trees",     # tile_NN.png, 32x32, alpha
    "grass":   "build/art/pixellab/t16_water/tile_06.png",   # 16px ground
    "tiles": {
      "forest_edge_12": [[36, 0, 0], [39, 32, 0], ...]    # [sprite, x, y] in draw order
    }
  }

Ground is the 16px grass repeated 6x6. Sprites are drawn in the order listed,
so list rows from the back (top) to the front (bottom); a sprite is placed
with its top-left at (x, y); one that crosses the right border is drawn again
96 px to the left so the tile repeats along a row, and one that crosses the
bottom border is drawn again 96 px up, behind everything, so the tile repeats
down a column. Nothing is random.
Writes one PNG per tile and a 3x mock strip of each tile repeated three
times over a row of grass, to check the horizontal seam and the south edge.
"""
import json, os, sys
from PIL import Image

lay = json.load(open(sys.argv[1]))
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
sd = lay["sprites"]
grass16 = Image.open(lay["grass"]).convert("RGBA")
S = grass16.width
ground = Image.new("RGBA", (96, 96))
for b in range(96 // S):
    for a in range(96 // S):
        ground.paste(grass16, (a * S, b * S))
ground.save(os.path.join(out, "grass.png"))
cache = {}


def blit(im, spr, x, y):
    """alpha_composite that accepts a negative or overhanging position."""
    sx, sy = max(0, -x), max(0, -y)
    dx, dy = max(0, x), max(0, y)
    if sx >= spr.width or sy >= spr.height or dx >= im.width or dy >= im.height:
        return
    im.alpha_composite(spr, dest=(dx, dy), source=(sx, sy))


def sprite(i):
    if i not in cache:
        cache[i] = Image.open(os.path.join(sd, f"tile_{i:02d}.png")).convert("RGBA")
    return cache[i]


for name, entry in lay["tiles"].items():
    # A tile is a list of placements, or {"wrap": "hv", "sprites": [...]}
    # where wrap says which borders continue into a tile of the same kind:
    # h for the right edge, v for the bottom. A plain list wraps both ways.
    places = entry["sprites"] if isinstance(entry, dict) else entry
    wrap = entry.get("wrap", "hv") if isinstance(entry, dict) else "hv"
    im = ground.copy()
    # A sprite that crosses the bottom border continues at the top, drawn
    # first so it sits behind everything: that is what makes a tile repeat
    # downwards with its bottom edge matching its top.
    for i, x, y in places:
        if "v" in wrap and y + sprite(i).height > 96:
            blit(im, sprite(i), x, y - 96)
            if "h" in wrap and x + sprite(i).width > 96:
                blit(im, sprite(i), x - 96, y - 96)
    for i, x, y in places:
        blit(im, sprite(i), x, y)
        # A sprite that crosses the right border continues on the left, so the
        # tile repeats along a row without a gap or a cut tree.
        if "h" in wrap and x + sprite(i).width > 96:
            blit(im, sprite(i), x - 96, y)
    im.save(os.path.join(out, name + ".png"))
    mock = Image.new("RGBA", (288, 288))
    for c in range(3):
        mock.paste(im, (c * 96, 0))
        mock.paste(im, (c * 96, 96))     # a second forest row, to see the cross-row overlap
        mock.paste(ground, (c * 96, 192))
    mock.resize((864, 864), Image.NEAREST).save(os.path.join(out, name + "_mock.png"))
    print(name, len(places), "sprites")
