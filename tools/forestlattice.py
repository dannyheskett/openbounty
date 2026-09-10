#!/usr/bin/env python3
"""Forest tiles from one global lattice, with terminal or interface sides.

    python3 tools/forestlattice.py <out.json> --sprites DIR --crown N [--name forest]

The contract (2026-09-10):

  Every side of every tile is TERMINAL (grass beyond) or an INTERFACE
  (forest beyond). Sprites live on ONE lattice with a 96 px period in both
  directions, the same for every tile of the terrain; a tile draws every
  lattice sprite whose ink touches it. A sprite that straddles an interface
  border is therefore drawn by both tiles, each its own part, identically,
  so any two tiles join by construction. A sprite that would straddle a
  TERMINAL border is dropped, and the edge is finished with sprites that
  sit fully inside the tile. Nothing else ever crosses a border.

Lattice: rows 24 px apart at y = -12, 12, 36, 60 (mod 96); even rows hold
sprites at x = 0 and 48, odd rows at x = 24 and 72. Ink is read from the
sprite (its opaque box), so "straddles" means ink, not canvas.

Terminal edges (ink bounds for the crown sprite: left L, right R, top T,
bottom B in its canvas):
  north: nothing to add, the y = -12 row's crown tops already sit inside;
  south: the rows whose ink would cross the bottom are gone, leaving the
         y = 12 row's crowns with their trunks on the bottom line: the tree line;
  west:  an edge crown at x = -L per lattice row (ink flush with the border);
  east:  an edge crown at x = 96 - R per lattice row;
  outer corner (two terminal sides): the edge crown nearest the corner is
         left out, which rounds it.

Codes are the engine's (OPENBOUNTY-SPEC REQ-229a): 11 N, 12 S, 9 E, 10 W,
1 NW, 3 NE, 2 SW, 4 SE, and 5..8 the diagonal-only inner corners, which
are plain lattice (all four sides are interfaces).

Writes a layout for tools/treetile.py with wrap off: every sprite is
listed explicitly, negative positions included.
"""
import json, sys
from PIL import Image

out = sys.argv[1]
SPR = sys.argv[sys.argv.index("--sprites") + 1]
CROWN = int(sys.argv[sys.argv.index("--crown") + 1])
NAME = sys.argv[sys.argv.index("--name") + 1] if "--name" in sys.argv else "forest"
GRASS = "build/art/pixellab/t16_water/tile_06.png"

im = Image.open(f"{SPR}/tile_{CROWN:02d}.png").convert("RGBA")
L, T, R, B = im.getbbox()          # ink box in the canvas

ROWS = [-12, 12, 36, 60]
COLS = [[0, 48], [24, 72]]

OPEN = {11: "N", 12: "S", 9: "E", 10: "W", 1: "NW", 3: "NE", 2: "SW", 4: "SE",
        5: "", 6: "", 7: "", 8: "", 0: ""}
# Diagonal-only codes: the one grass cell that touches at a corner point.
# A sprite whose ink enters that cell is dropped here too, because the two
# cardinal neighbours (whose sides face it) drop it as a terminal crosser,
# and a sprite straddling an interface border must be treated the same by
# both tiles or the border shows a cut.
DIAG = {5: "SE", 6: "NE", 7: "SW", 8: "NW"}


def lattice():
    """Every lattice sprite whose ink can touch a 96 tile, as (x, y)."""
    pts = []
    for oy in (-96, 0, 96):
        for k, y in enumerate(ROWS):
            for ox in (-96, 0, 96):
                for x in COLS[k % 2]:
                    pts.append((x + ox, y + oy))
    return pts


def ink(x, y):
    return (x + L, y + T, x + R, y + B)   # right/bottom exclusive


def touches(x, y):
    l, t, r, b = ink(x, y)
    return r > 0 and b > 0 and l < 96 and t < 96


def crosses(x, y, side):
    l, t, r, b = ink(x, y)
    if side == "N": return t < 0 < b
    if side == "S": return t < 96 < b
    if side == "W": return l < 0 < r
    if side == "E": return l < 96 < r
    return False


def enters_diagonal(x, y, diag):
    l, t, r, b = ink(x, y)
    if diag == "SE": return r > 96 and b > 96
    if diag == "NE": return r > 96 and t < 0
    if diag == "SW": return l < 0 and b > 96
    if diag == "NW": return l < 0 and t < 0
    return False


def tile(code):
    open_sides = OPEN[code]
    diag = DIAG.get(code, "")
    keep = []
    for (x, y) in lattice():
        if not touches(x, y): continue
        if any(crosses(x, y, s) for s in open_sides): continue
        if diag and enters_diagonal(x, y, diag): continue
        keep.append((x, y))
    # Terminal west/east: flush edge crowns. They are a lattice of their own
    # with the same 96 period (every lattice row, including the ones drawn
    # in from the tiles above and below), so where the wood's edge continues
    # into the next tile that tile draws the same crown at the same place
    # and an edge crown crossing an interface border is continued, not cut.
    edge_rows = [y + oy for oy in (-96, 0, 96) for y in ROWS]
    edge_rows = [y for y in edge_rows if y + B > 0 and y + T < 96]
    if "S" in open_sides:
        edge_rows = [y for y in edge_rows if y + B <= 96]
    if "N" in open_sides:
        edge_rows = [y for y in edge_rows if y + T >= 0]
    edge_rows.sort()
    for side, ex in (("W", -L), ("E", 96 - R)):
        if side not in open_sides: continue
        for y in edge_rows:
            if "N" in open_sides and y == edge_rows[0]: continue     # rounded outer corner
            if "S" in open_sides and y == edge_rows[-1]: continue
            # alternate rows sit 12 px back from the line, so identical crowns
            # do not stack into a straight wall; the row index is the lattice
            # row, so the stagger repeats with the 96 period
            back = 12 if ((y + 12) // 24) % 2 else 0
            keep.append((ex - back if side == "E" else ex + back, y))
    keep = sorted(set(keep), key=lambda p: (p[1], p[0]))            # back to front
    return {"wrap": "", "sprites": [[CROWN, x, y] for (x, y) in keep]}


lay = {"sprites": SPR, "grass": GRASS, "tiles": {}}
lay["tiles"][NAME] = tile(0)
for code in range(1, 13):
    lay["tiles"][f"{NAME}_edge_{code:02d}"] = tile(code)
json.dump(lay, open(out, "w"), indent=1)
print(len(lay["tiles"]), "tiles, crown", CROWN, "ink", (L, T, R, B), "->", out)
