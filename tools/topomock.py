#!/usr/bin/env python3
"""Lay terrain tiles as the engine would for a set of cell shapes, at 1x.

    python3 tools/topomock.py <tiles-dir> <terrain> <out.png>

Cells are F (terrain) or G (grass). The edge code per cell follows
OPENBOUNTY-SPEC REQ-229a: which of the eight neighbours are grass, cardinals
before diagonals; outside the shape counts as grass. Three or more open
cardinals has no code and draws the plain tile.
"""
import sys
from PIL import Image, ImageDraw

d, terrain, out = sys.argv[1], sys.argv[2], sys.argv[3]
water = terrain == "water"
TABLE = {"N": 11, "S": 12, "E": 9, "W": 10, "NE": 3, "NW": 1, "SW": 2, "SE": 4,
         "ne": 6, "se": 5, "sw": 7, "nw": 8}
if water:
    TABLE = {"N": 10, "S": 11, "E": 8, "W": 9, "NE": 0, "NW": 1, "SW": 2, "SE": 3,
             "ne": 5, "se": 4, "sw": 6, "nw": 7}


def code(cells, x, y):
    def g(dx, dy):
        yy, xx = y + dy, x + dx
        return not (0 <= yy < len(cells) and 0 <= xx < len(cells[0]) and cells[yy][xx] == "F")
    card = "".join(s for s, (dx, dy) in (("N", (0, -1)), ("S", (0, 1)), ("E", (1, 0)), ("W", (-1, 0))) if g(dx, dy))
    if card:
        if card in TABLE: return TABLE[card]
        if len(card) == 2:
            k = "".join(sorted(card, key="NSEW".index))
            k = {"NE": "NE", "NW": "NW", "SE": "SE", "SW": "SW"}.get(k, k)
            return TABLE.get(k)
        return None
    for s, (dx, dy) in (("ne", (1, -1)), ("se", (1, 1)), ("sw", (-1, 1)), ("nw", (-1, -1))):
        if g(dx, dy): return TABLE[s]
    return None


def render(cells):
    H, W = len(cells), len(cells[0])
    m = Image.new("RGBA", (W * 96, H * 96))
    for y in range(H):
        for x in range(W):
            if cells[y][x] != "F":
                n = "grass"
            else:
                c = code(cells, x, y)
                n = terrain if c is None else f"{terrain}_edge_{c:02d}"
            m.paste(Image.open(f"{d}/{n}.png").convert("RGBA"), (x * 96, y * 96))
    return m


TOPOS = {
    "single cell": ["GGG", "GFG", "GGG"],
    "horizontal strip": ["GGGGG", "GFFFG", "GGGGG"],
    "vertical strip": ["GGG", "GFG", "GFG", "GFG", "GGG"],
    "square block": ["GGGG", "GFFG", "GFFG", "GGGG"],
    "diagonal": ["GGFFG", "GFFFG", "FFFGG", "FFGGG"],
    "L shape": ["GGGG", "GFGG", "GFGG", "GFFG", "GGGG"],
    "inner corner": ["FFFF", "FFFF", "FFGG", "FFGG"],
    "holes": ["FFFFF", "FFGFF", "FFFFF", "FGFFF", "FFFFF"],
}
ims = [(n, render(v)) for n, v in TOPOS.items()]
W = max(im.width for _, im in ims) + 16
H = sum(im.height + 28 for _, im in ims)
sheet = Image.new("RGB", (W, H), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
y = 0
for n, im in ims:
    dr.text((8, y + 4), n, fill=(230, 230, 230))
    sheet.paste(im, (8, y + 18))
    y += im.height + 28
sheet.save(out)
print(out, sheet.size)
