#!/usr/bin/env python3
"""Write the forest layout file: the interior tile and the twelve edge codes,
in several interchangeable variants, from the two hand-settled tiles.

    python3 tools/forestlayout.py <out.json> [--variants 3] [--size 32|64|96] [--terrain forest|mountain]

The mechanism is the one settled by hand on forest_edge_12 and forest:
crown rows 6 px apart, four trees per row on a 24 px pitch, alternate rows
offset 12, the x 84 tree always sprite 44 so any two tiles butt together,
and a front line of trunk trees on one y with a fixed -1/0/+1 stagger where
the wood faces grass to the south.

Where the wood is, per code, follows the engine's rule (REQ-229a): a code
says which neighbours are grass. The wood fills the cell; an open side is
the border itself (trunk line on the south, crowns starting at the edge on
the others), two open sides meet in a rounded outer corner, and a grass cell
touching only at a corner point is an inner corner: solid wood, the
neighbours' edges meet at the point. The trunk line follows
the wood's south-facing boundary, straight or along the arc. Borders that
continue into more wood wrap; the others do not.

Variants differ by which crown fills each slot and by the front-tree order,
nothing else, so every variant of a code has the same outline and lines up
with every variant of its neighbours.
"""
import json, math, sys

out = sys.argv[1]
NV = int(sys.argv[sys.argv.index("--variants") + 1]) if "--variants" in sys.argv else 3
SIZE = int(sys.argv[sys.argv.index("--size") + 1]) if "--size" in sys.argv else 32
TERRAIN = sys.argv[sys.argv.index("--terrain") + 1] if "--terrain" in sys.argv else "forest"
if TERRAIN == "mountain":
    # rocks, same mechanism: overhead forms for the interior, pieces with a
    # cliff face on the south side for the front line
    SPRITES = "build/art/pixellab/o96_rocks"
    CROWNS = [0, 5, 6, 7, 1]
    BORDER = 0
    FRONT = [2]                         # the ledge only: one continuous cliff face along a south edge
    ARC_FRONT = [1, 7]                  # narrower crags follow the curve of a rounded corner
    BASE = None                         # base read from the sprite
elif SIZE == 32:
    SPRITES = "build/art/pixellab/o32_trees"
    CROWNS = [60, 39, 36, 7, 15, 44]    # overhead crowns
    BORDER = 44                         # the crossing tree, shared by every tile
    FRONT = [22, 3, 5]                  # trunk trees for the front line
    BASE = 32                           # where a front tree's base sits in its canvas
elif SIZE == 64:
    SPRITES = "build/art/pixellab/o64_trees"
    CROWNS = [1, 5, 6, 9, 12, 15]
    BORDER = 5
    FRONT = [0, 4, 13]
    BASE = 59
else:
    SPRITES = "build/art/pixellab/o96_trees"
    CROWNS = [3, 7]                      # the two true overhead crowns; 5 and 6 show trunks, 0/1/2 carry ground shadows
    BORDER = 3
    FRONT = [0, 7, 1]
    BASE = 87
ARC_FRONT = FRONT if "ARC_FRONT" not in dir() else ARC_FRONT
STAGGER = [0, 1, -1, 0, 1, 0]


def base_of(i):
    """Where a sprite's lowest opaque row is, so its base lands on a line."""
    from PIL import Image
    return Image.open(f"{SPRITES}/tile_{i:02d}.png").convert("RGBA").getbbox()[3]


if SIZE == 96:
    # one tree per row: the even rows hold it at x 0, the odd rows at x 48,
    # where it crosses the border and wraps, so every row is one crown and
    # the shoulders of the next
    PITCH, ROW, EVEN, ODD, CROSS = 48, 24, [0, 48], [24, 72], 72   # 24 divides 96: rows line up tile to tile
else:
    PITCH = SIZE * 3 // 4               # 24 for 32 px trees, 48 for 64
    ROW = SIZE * 3 // 16                # 6 for 32, 12 for 64
    EVEN = list(range(0, 96, PITCH))
    ODD = [x + PITCH // 2 for x in EVEN]
    CROSS = [x for x in ODD if x + SIZE > 96][0]   # the border tree's column
WIDE = ([7, 3] if TERRAIN == "forest" else [0, 6, 5]) if SIZE == 96 else CROWNS   # widest crowns, for tucking against an open side
INSET = SIZE // 3 if TERRAIN == "mountain" else SIZE // 4   # how much of a form must lie inside the terrain line
EDGE_IN = SIZE // 8                     # how far a crown on an open west/east side tucks past the border
FRONT_PITCH = SIZE // 3 if TERRAIN == "mountain" else SIZE // 2   # ledges overlap: 32; trees 16, 32, 48
# Engine edge codes (OPENBOUNTY-SPEC REQ-229a): which neighbours of a forest
# cell are grass. Cardinals: N E S W. Diagonal-only: a grass cell touching at
# one corner point, with the cardinals forest.
CODES = {1: "NW", 2: "SW", 3: "NE", 4: "SE",          # two open sides, outer corner
         5: "se", 6: "ne", 7: "sw", 8: "nw",          # diagonal only, inner bite
         9: "E", 10: "W", 11: "N", 12: "S"}            # one open side
CORNER_R = 72 if SIZE == 32 else (80 if TERRAIN == "mountain" else 56)    # rounded outer corner where two open sides meet


def in_wood(code, x, y):
    k = CODES.get(code, "")
    if k == "NW": return not (x < CORNER_R and y < CORNER_R and math.hypot(CORNER_R - x, CORNER_R - y) > CORNER_R)
    if k == "NE": return not (x > 96 - CORNER_R and y < CORNER_R and math.hypot(x - (96 - CORNER_R), CORNER_R - y) > CORNER_R)
    if k == "SW": return not (x < CORNER_R and y > 96 - CORNER_R and math.hypot(CORNER_R - x, y - (96 - CORNER_R)) > CORNER_R)
    if k == "SE": return not (x > 96 - CORNER_R and y > 96 - CORNER_R and math.hypot(x - (96 - CORNER_R), y - (96 - CORNER_R)) > CORNER_R)
    # a grass cell touching at a NORTH corner point needs no bite: the crowns
    # there are overlapped by the rows in front, and a bite only opens a notch
    # beside the neighbour's trunk line
    # A grass cell touching only at a corner point is an INNER corner of the
    # wood: the wood is solid there and the neighbours' edges meet at the
    # point, so no bite at all. A bite made the wood recede where it should
    # be fullest.
    if k in ("nw", "ne", "sw", "se"): return True
    return True


def open_side(code, side):
    return side in CODES.get(code, "")


def south_boundary(code, x):
    """y where the wood meets grass to the south in column x: the bottom
    border on an open south side, the arc of a corner or bite, or None
    where the wood continues into the cell below."""
    ys = [y for y in range(0, 97) if in_wood(code, x, y)]
    if not ys: return None
    if max(ys) >= 96:
        return 96 if open_side(code, "S") else None
    return max(ys)


def tile(code, v):
    north_open, east_open = open_side(code, "N"), open_side(code, "E")
    rows = []
    rot = v * 2
    if north_open and not open_side(code, "W") and SIZE == 96:
        # the west neighbour's crossing tree one row up still reaches into
        # this tile's top-left corner; without it that corner shows grass
        # inside the wood
        rows.append([BORDER, CROSS - 96, -ROW])
    for r in range(-(SIZE // ROW), 96 // ROW):
        y = r * ROW
        xs = EVEN if r % 2 == 0 else ODD
        for k, x in enumerate(xs):
            crossing = x + SIZE > 96
            # crown per slot from a fixed scramble, so the sequence does not
            # run in diagonal stripes; crossing slots ignore the variant so
            # neighbours agree at the border
            # keyed by the row's position within the tile, so the rows drawn
            # above the top edge are the same trees the tile above draws at
            # its bottom, and the canopy continues across the border
            rr = r % (96 // ROW)
            slot = ((rr + 1) * 73856093) ^ ((k + 1) * 19349663) ^ ((0 if crossing else rot + 1) * 83492791)
            slot = (slot ^ (slot >> 13)) * 2654435761 % 2**32
            if x == CROSS: i = BORDER
            else: i = CROWNS[(slot >> 7) % len(CROWNS)]
            if y < 0 and north_open: continue
            # crowns have transparent margins, so on an open west or east
            # side the outermost crown tucks past the border a little rather
            # than leaving a strip of grass inside it
            if x == 0 and open_side(code, "W"):
                x = -EDGE_IN
                i = WIDE[(r + k + rot) % len(WIDE)]
            if crossing and east_open:
                # nothing may cross an open east border, but the wood must
                # still reach it: one wide crown stands flush with the
                # border, BEHIND the west neighbour's crossing trees, which
                # continue in from the left as the wrap would have drawn
                # them (they must stay in front, or the border shows a cut)
                if x == CROSS:
                    rows.append([WIDE[(r + k + rot) % len(WIDE)], 96 - SIZE + EDGE_IN, y])
                rows.append([i, x - 96, y])
                continue
            # the whole crown must be in the wood, not just its centre, or a
            # crown at the tile corner fills in a rounded corner
            if not all(in_wood(code, x + dx, y + dy) for dx in (INSET, SIZE - INSET) for dy in (INSET, SIZE - INSET)): continue
            sbs = [south_boundary(code, min(max(x + d, 0), 96)) for d in range(INSET, SIZE - INSET + 1, 8)]
            sbs = [b for b in sbs if b is not None]
            if sbs and y + SIZE > min(sbs) - 16: continue
            rows.append([i, x, y])
    rows.sort(key=lambda p: p[2])        # back to front
    n = 0
    arc_pitch = FRONT_PITCH // 2
    for x in range(0, 96, arc_pitch):
        sb = south_boundary(code, x + arc_pitch // 2)
        if sb is None: continue
        on_arc = sb < 96
        if not on_arc and x % FRONT_PITCH: continue     # straight edge: the full pitch
        if x + SIZE > 96 and east_open and not on_arc: continue
        if not in_wood(code, x + SIZE // 2, sb - 8): continue
        pieces = ARC_FRONT if on_arc else FRONT
        f = pieces[(n + v) % len(pieces)]
        rows.append([f, x, sb - (BASE if BASE else base_of(f)) + STAGGER[n % len(STAGGER)]])
        n += 1
    # A wrapped copy stands in for the neighbour's tree crossing into this
    # tile: the west neighbour's on the left, the north neighbour's at the
    # top. So wrap only where that neighbour is wood.
    wrap = ("" if open_side(code, "W") else "h") + ("" if north_open else "v")
    return {"wrap": wrap, "sprites": rows}


lay = {"sprites": SPRITES,
       "grass": "build/art/pixellab/t16_water/tile_06.png",
       "tiles": {}}
suffix = ["", "_b", "_c", "_d", "_e", "_f"]
for v in range(NV):
    lay["tiles"][TERRAIN + suffix[v]] = tile(0, v)
    for code in range(1, 13):
        lay["tiles"][f"{TERRAIN}_edge_{code:02d}{suffix[v]}"] = tile(code, v)
json.dump(lay, open(out, "w"), indent=1)
print(len(lay["tiles"]), "tiles in", out)
