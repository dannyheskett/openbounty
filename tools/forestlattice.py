#!/usr/bin/env python3
"""Forest and mountain tiles from one lattice, under the border contract.

    python3 tools/forestlattice.py <out.json> --sprites DIR --crown N --name forest
    python3 tools/forestlattice.py <out.json> --sprites DIR --terrain mountain --name mountain

The contract (2026-09-10), checked mechanically by tools/seamcheck.py:

  Every side of a tile is TERMINAL (grass beyond) or an INTERFACE (the
  same terrain beyond). A sprite may straddle at most ONE border, never a
  corner, so whether it exists depends only on that border's two cells,
  which both tiles know. A straddler across an interface is drawn by both
  tiles, each its own part, from the same lattice, so any two tiles join
  by construction. Nothing crosses a terminal border. A terminal side is
  finished with sprites that sit fully inside the tile, so they belong to
  that tile alone.

Lattice, per 96 px period, ink read from each sprite:
  top row: a sprite at x = 0 with its ink top on the north line, and a
      straddler across the east line placed so its topmost ink point sits
      on that line; bottom row: the same with ink bottoms on the south
      line and the straddler's lowest point on the east line. A straddler
      crosses the east line and nothing else. Its extreme point on the
      line is what closes the tile corners: round sprites cannot cover a
      corner from inside, so the four corner pixels are taken by the
      straddlers' tops and bottoms meeting there.
  lower row 1 (y = 36): two sprites flush to the west and east lines by
      ink box; lower row 2: two sprites whose widest ink row sits on the
      south line, flush at that row. Both straddle the south border only.
  The upper bottom row's ink ends 1 px above the south line, so no flat
  rock bottom lies along the line. Point contacts only.
The generator counts grass pixels left showing in the plain tile and
prints the count; it must be 0 (forest with crown 3: 3 pixels).

Terminal sides:
  north: the rows drawn in from the tile above are gone;
  south: the y = 36 and 60 rows are gone, the y = 12 row's bases sit on
         the line (the tree line);
  west:  the straddler copies from the west are gone; one edge sprite,
         flush to the line and fully inside, fills the upper rows;
  east:  the x = 48 straddlers are gone; one flush edge sprite fills them.
  Mountains, south: one ledge fully inside, behind the rocks, is the
         cliff face the row 12 rocks stand on.
Codes 5..8 (diagonal-only) are plain lattice: nothing touches a corner.

Codes are the engine's (OPENBOUNTY-SPEC REQ-229a). Output is a layout for
tools/treetile.py with wrap off, every sprite listed, negatives included.
"""
import json, sys
from PIL import Image

out = sys.argv[1]
SPR = sys.argv[sys.argv.index("--sprites") + 1]
NAME = sys.argv[sys.argv.index("--name") + 1] if "--name" in sys.argv else "forest"
TERRAIN = sys.argv[sys.argv.index("--terrain") + 1] if "--terrain" in sys.argv else "forest"
GRASS = "build/art/pixellab/t16_water/tile_06.png"

_bbox = {}
def bbox(i):
    if i not in _bbox:
        _bbox[i] = Image.open(f"{SPR}/tile_{i:02d}.png").convert("RGBA").getbbox()
    return _bbox[i]


if TERRAIN == "forest":
    CROWN = int(sys.argv[sys.argv.index("--crown") + 1])
    UPPER = [[CROWN, CROWN], [CROWN, CROWN]]   # rows -12, 12: x 0 and x 48
    LOWER = [[CROWN, CROWN], [CROWN, CROWN]]   # rows 36, 60: flush west and east
    EDGE = CROWN
    LEDGE = None
else:
    UPPER = [[6, 0], [7, 0]]      # rocks per slot, the same in every tile; the
    LOWER = [[6, 5], [5, 7]]      # boulder (0) straddles east: its top and bottom points close the corners
    EDGE = 3                      # the tooth: a narrow spire flush to a west/east terminal line
    LEDGE = 2                     # the ledge: the cliff face along a south terminal side, behind the rocks

OPEN = {11: "N", 12: "S", 9: "E", 10: "W", 1: "NW", 3: "NE", 2: "SW", 4: "SE",
        5: "", 6: "", 7: "", 8: "", 0: ""}


def ink(i, x, y):
    l, t, r, b = bbox(i)
    return (x + l, y + t, x + r, y + b)   # right/bottom exclusive


def crossings(i, x, y):
    l, t, r, b = ink(i, x, y)
    s = set()
    if t < 0 < b: s.add("N")
    if t < 96 < b: s.add("S")
    if l < 0 < r: s.add("W")
    if l < 96 < r: s.add("E")
    return s


def touches(i, x, y):
    l, t, r, b = ink(i, x, y)
    return r > 0 and b > 0 and l < 96 and t < 96


_alpha = {}
def alpha(i):
    if i not in _alpha: _alpha[i] = Image.open(f"{SPR}/tile_{i:02d}.png").convert("RGBA").split()[3].load()
    return _alpha[i]
_prof = {}
def profile(i):
    """Per ink row: (leftmost, rightmost) ink column."""
    if i not in _prof:
        out = {}
        for y in range(96):
            xs = [x for x in range(96) if alpha(i)[x, y] > 0]
            if xs: out[y] = (xs[0], xs[-1])
        _prof[i] = out
    return _prof[i]
def extreme_col(i, top):
    """Centre column of the sprite's topmost (or lowest) ink row."""
    l, t, r, b = bbox(i); row = t if top else b - 1
    xs = [x for x in range(l, r) if alpha(i)[x, row] > 0]
    return (xs[0] + xs[-1]) // 2
def widest_row(i, left):
    """The ink row reaching furthest left (or right), nearest the middle on ties."""
    p = profile(i)
    if left: return min(p, key=lambda y: (p[y][0], abs(y - 48)))
    return max(p, key=lambda y: (p[y][1], -abs(y - 48)))

LOWER1_Y = 36


def period():
    """One 96 period of the lattice: (sprite, x, y, layer). Layer 0 is the
    lower rows (drawn first), 1 the upper rows."""
    pts = []
    # lower row 1: flush to the west and east lines by ink box
    for j in range(2):
        i = LOWER[0][j]; l, t, r, b = bbox(i)
        pts.append((i, -l if j == 0 else 96 - r, LOWER1_Y, 0))
    # lower row 2: the row of widest ink sits on the south line, flush to the
    # west/east line at that row, so the sprite's widest point closes the
    # corner from inside while touching the line only there
    for j in range(2):
        i = LOWER[1][j]; yr = widest_row(i, j == 0); l, r = profile(i)[yr]
        pts.append((i, -l if j == 0 else 95 - r, 95 - yr, 0))
    # upper top row: ink tops on the north line; the straddler's topmost
    # point sits on the east line, closing the corner from below
    for j in range(2):
        i = UPPER[0][j]; l, t, r, b = bbox(i)
        pts.append((i, 0 if j == 0 else 96 - extreme_col(i, True), -t, 1))
    # upper bottom row: ink bottoms 1 px above the south line; the straddler
    # sits at x = 24 so its middle (a trunk, on trees) is behind the flush
    # lower-row sprite and not in the notch where two of those meet at the line
    for j in range(2):
        i = UPPER[1][j]; l, t, r, b = bbox(i)
        pts.append((i, 0 if j == 0 else 48, 95 - b, 1))
    return pts


JITTER = int(sys.argv[sys.argv.index("--jitter") + 1]) if "--jitter" in sys.argv else 1


def jittered():
    """The period with every sprite moved -1, 0 or +1 px in y at random,
    the same for every tile (seeded per slot) so straddlers still match. A
    move that would make a sprite cross another line is not taken."""
    import random
    rng = random.Random(JITTER)
    out = []
    for (i, x, y, lay) in period():
        d = rng.choice((-1, 0, 1)) if JITTER else 0
        if len(crossings(i, x, y + d)) > len(crossings(i, x, y)): d = 0
        out.append((i, x, y + d, lay))
    return out


def lattice():
    """Every lattice sprite that can touch a tile: (sprite, x, y, layer)."""
    return [(i, x + ox, y + oy, lay) for oy in (-96, 0, 96) for ox in (-96, 0, 96)
            for (i, x, y, lay) in jittered()]


def check_lattice():
    for (i, x, y, _) in lattice():
        if not touches(i, x, y): continue
        c = crossings(i, x, y)
        assert len(c) <= 1, f"sprite {i} at {x},{y} crosses {sorted(c)}: one border only"


def tile(code):
    open_sides = OPEN[code]
    keep = []
    for (i, x, y, lay) in lattice():
        if not touches(i, x, y): continue
        if crossings(i, x, y) & set(open_sides): continue   # would cross a terminal line
        keep.append((i, x, y, lay))
    eL, eT, eR, eB = bbox(EDGE)
    ey = -eT                                   # ink top on the line, fully inside
    if ey + eB > 96: ey = 96 - eB
    if "W" in open_sides: keep.append((EDGE, -eL, ey, 2))
    if "E" in open_sides: keep.append((EDGE, 96 - eR, ey, 2))
    if LEDGE is not None and "S" in open_sides:
        lL, lT, lR, lB = bbox(LEDGE)
        ly = 96 - lB
        # one slab, fully inside, drawn BEHIND the rocks (it sorts first):
        # the row 12 rocks stand on it and hide the slab ends at the lines.
        # Nothing may be drawn in front of a straddler that the neighbour
        # does not draw too, so the face cannot be in front.
        keep.append((LEDGE, (96 - (lR - lL)) // 2 - lL, ly, -1))
    for (i, x, y, _) in keep:
        assert not (crossings(i, x, y) & set(open_sides))
    # Draw order, the same in every tile: the face, the lower rows (which
    # straddle south), then the upper rows (inside or straddling east),
    # then the edge sprites. So nothing that is only drawn on one side of
    # a line ever sits in front of a straddler with an edge on the line.
    keep = sorted(set(keep), key=lambda p: (min(p[3], 0), p[2], p[1]))
    return {"wrap": "", "sprites": [[i, x, y] for (i, x, y, _) in keep]}


check_lattice()
lay = {"sprites": SPR, "grass": GRASS, "tiles": {}}
lay["tiles"][NAME] = tile(0)
for code in range(1, 13):
    lay["tiles"][f"{NAME}_edge_{code:02d}"] = tile(code)
json.dump(lay, open(out, "w"), indent=1)


def holes():
    """Grass pixels left showing in a plain tile surrounded by plain tiles."""
    can = Image.new("RGBA", (288, 288))
    for oy in (0, 96, 192):
        for ox in (0, 96, 192):
            for (i, x, y) in lay["tiles"][NAME]["sprites"]:
                can.alpha_composite(Image.open(f"{SPR}/tile_{i:02d}.png").convert("RGBA"), (x + ox, y + oy))
    a = can.crop((96, 96, 192, 192)).split()[3].load()
    return sum(1 for y in range(96) for x in range(96) if a[x, y] == 0)


print(len(lay["tiles"]), "tiles,", TERRAIN, "->", out, "| grass showing in the plain tile:", holes(), "px")
