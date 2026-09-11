#!/usr/bin/env python3
"""Road tiles from a PixelLab 16 px dirt-over-grass tileset.

    python3 tools/roadtile.py <set-dir> <out-dir> [--seed N]

A road piece is a 96 px tile built the way tools/stitch96.py builds a
terrain tile: a 7x7 grid of vertices, each grass (l) or dirt (u), and the
set's corner tile for every 2x2 of vertices. The vertices come from a
pixel-space shape sampled every 16 px, so the shapes below ARE the pieces.

The contract: every side a road leaves through carries the same vertex
pattern, so any piece joins any other.
  straight exit, north/south sides: dirt at vertex columns 2..4 (x 32..64);
  straight exit, west/east sides:   dirt at vertex rows 2..4;
  diagonal exit through a corner:   dirt at the corner vertex and its two
      neighbours along the sides, a 28 px band across the corner.
A diagonal passes through the tile corner, which two side neighbours
share, so those cells take a COMPANION piece: grass with the road's
triangle in that corner (road_c_nw, _ne, _sw, _se).

Pieces (20): road_ns, road_ew; curves road_ne, road_es, road_sw, road_wn
(named by their two exits); diagonals road_nesw, road_nwse; joins from a
straight exit to a diagonal corner road_n_sw, road_n_se, road_s_nw,
road_s_ne, road_e_nw, road_e_sw, road_w_ne, road_w_se; companions.

Imperfection: an interior vertex with two or more grass neighbours flips
to grass at random (seeded), which nicks inner corners; border vertices
never change, they are the contract.
"""
import json, os, random, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 1
os.makedirs(out, exist_ok=True)

meta = json.load(open(os.path.join(src, "tiles_meta.json")))
setl = []
for i, t in enumerate(meta):
    p = t["pattern_4x4"]
    rows = [list(p[f"row_{r}"])[1:3] for r in range(4)]
    setl.append((rows, Image.open(os.path.join(src, f"tile_{i:02d}.png")).convert("RGBA")))
S = setl[0][1].width
N = 96 // S
VAL = {"l": 0, "u": 1}


def pick(rows):
    best, score = None, -1
    for prow, im in setl:
        if prow[1] != rows[1] or prow[2] != rows[2]:
            continue
        s, ok = 0, True
        for r in (0, 3):
            for a, b in zip(prow[r], rows[r]):
                if a == 255: continue
                if a == b: s += 1
                else: ok = False
        if ok and s > score:
            best, score = im, s
    if best is None:
        raise SystemExit(f"no set tile for {rows}")
    return best


HW = int(sys.argv[sys.argv.index("--width") + 1]) // 2 if "--width" in sys.argv else 16   # half width of a straight band, centred at 48
DW = 32          # |x + y - 96| <= DW: a diagonal band; 32 keeps it continuous across a corner at 16 px vertices


def ns(x, y): return abs(x - 48) <= HW
def ew(x, y): return abs(y - 48) <= HW
def d_nesw(x, y): return abs(x + y - 96) <= DW
def d_nwse(x, y): return abs(x - y) <= DW
def arc(x, y, cx, cy):
    """A quarter-ring curve: the straight band bent round the tile corner
    (cx, cy), radii 32..64, so it leaves both sides on the straight contract."""
    return 32 <= ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 <= 64


SHAPES = {
    "road_ns":   lambda x, y: ns(x, y),
    "road_ew":   lambda x, y: ew(x, y),
    "road_ne":   lambda x, y: arc(x, y, 96, 0),
    "road_es":   lambda x, y: arc(x, y, 96, 96),
    "road_sw":   lambda x, y: arc(x, y, 0, 96),
    "road_wn":   lambda x, y: arc(x, y, 0, 0),
    "road_nesw": d_nesw,
    "road_nwse": d_nwse,
    "road_n_sw": lambda x, y: (ns(x, y) and y <= 48) or (d_nesw(x, y) and x <= 48),
    "road_n_se": lambda x, y: (ns(x, y) and y <= 48) or (d_nwse(x, y) and x >= 48),
    "road_s_nw": lambda x, y: (ns(x, y) and y >= 48) or (d_nwse(x, y) and x <= 48),
    "road_s_ne": lambda x, y: (ns(x, y) and y >= 48) or (d_nesw(x, y) and x >= 48),
    "road_e_nw": lambda x, y: (ew(x, y) and x >= 48) or (d_nwse(x, y) and y <= 48),
    "road_e_sw": lambda x, y: (ew(x, y) and x >= 48) or (d_nesw(x, y) and y >= 48),
    "road_w_ne": lambda x, y: (ew(x, y) and x <= 48) or (d_nesw(x, y) and y <= 48),
    "road_w_se": lambda x, y: (ew(x, y) and x <= 48) or (d_nwse(x, y) and y >= 48),
    "road_c_nw": lambda x, y: x + y <= DW,
    "road_c_se": lambda x, y: x + y >= 192 - DW,
    "road_c_ne": lambda x, y: x - y >= 96 - DW,
    "road_c_sw": lambda x, y: y - x >= 96 - DW,
}


def vertices(shape, rng):
    v = [["u" if shape(16 * i, 16 * j) else "l" for i in range(N + 1)] for j in range(N + 1)]
    for _ in range(2):
        for j in range(1, N):
            for i in range(1, N):
                if v[j][i] == "u":
                    near = sum(v[jj][ii] == "l" for ii, jj in ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)))
                    if near >= 2 and rng.random() < 0.35:
                        v[j][i] = "l"
    return v


def build(name, rng):
    v = vertices(SHAPES[name], rng)
    im = Image.new("RGBA", (96, 96))
    for b in range(N):
        for a in range(N):
            r1 = [VAL[v[b][a]], VAL[v[b][a + 1]]]
            r2 = [VAL[v[b + 1][a]], VAL[v[b + 1][a + 1]]]
            r0 = [VAL[v[b - 1][a]], VAL[v[b - 1][a + 1]]] if b > 0 else list(r1)
            r3 = [VAL[v[b + 2][a]], VAL[v[b + 2][a + 1]]] if b + 2 <= N else list(r2)
            im.paste(pick([r0, r1, r2, r3]), (a * S, b * S))
    return im, v


def check_contract(made_v):
    """Every straight exit and every diagonal corner reads the same on its side."""
    exits = {
        "N": lambda v: tuple(v[0]), "S": lambda v: tuple(v[N]),
        "W": lambda v: tuple(v[j][0] for j in range(N + 1)), "E": lambda v: tuple(v[j][N] for j in range(N + 1)),
    }
    seen = {}
    bad = 0
    for name, v in made_v.items():
        for side, f in exits.items():
            pat = f(v)
            if "u" not in pat: continue
            key = (side, pat)
            seen.setdefault(key, []).append(name)
    for side in "NSWE":
        pats = [k for k in seen if k[0] == side]
        if len(pats) > 3:
            print("side", side, "has", len(pats), "patterns:", {"".join(p): n for (_, p), n in seen.items() if _ == side})
            bad += 1
    return bad


# --sweep: pixel-swept pieces. The same shapes as signed distances (pixels
# from the road's edge, negative inside), a periodic value noise on the
# edge for raggedness, the set's seamless dirt tile inside, grass outside.
# The noise field repeats every 96 px, so the edge is continuous across a
# tile line, and at a border the arc's distance equals the band's, so any
# piece meets any other. Curves are true quarter circles and diagonals
# true 45 degree bands, not 16 px steps.
SWEEP = "--sweep" in sys.argv
RAG = float(sys.argv[sys.argv.index("--rag") + 1]) if "--rag" in sys.argv else 3.0
RIM = float(sys.argv[sys.argv.index("--rim") + 1]) if "--rim" in sys.argv else 0.0   # px of rim just inside the edge


def rim_colour():
    """The colour the set paints where dirt meets grass: the most common
    colour in an edge tile that is in neither the plain dirt nor the plain
    grass tile."""
    from collections import Counter
    plain = set()
    for rows, im in setl:
        if rows[1] in ([0, 0], [1, 1]) and rows[2] == rows[1]:
            plain.update(im.convert("RGB").getdata())
    c = Counter()
    for rows, im in setl:
        if rows[1] == [1, 1] and rows[2] == [0, 0]:      # dirt above grass
            c.update(px for px in im.convert("RGB").getdata() if px not in plain)
    return c.most_common(1)[0][0] if c else (90, 60, 40)
import math
def sd_ns(x, y): return abs(x - 48) - HW
def sd_ew(x, y): return abs(y - 48) - HW
def sd_nesw(x, y): return abs(x + y - 96) / math.sqrt(2) - HW
def sd_nwse(x, y): return abs(x - y) / math.sqrt(2) - HW
def sd_arc(x, y, cx, cy): return abs(math.hypot(x - cx, y - cy) - 48) - HW
def seg(x, y, ax, ay, bx, by):
    """Distance from (x, y) to the segment a-b."""
    vx, vy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy)))
    return math.hypot(x - ax - t * vx, y - ay - t * vy)
def poly(*pts):
    """Signed distance to a road along a polyline: a straight leg from a
    side's middle to the centre and a diagonal leg from the centre to a
    corner join without a notch, the inner corner rounded by the width."""
    return lambda x, y: min(seg(x, y, *pts[i], *pts[i + 1]) for i in range(len(pts) - 1)) - HW
C = (48, 48)
SD = {
    "road_ns": sd_ns, "road_ew": sd_ew,
    "road_ne": lambda x, y: sd_arc(x, y, 96, 0), "road_es": lambda x, y: sd_arc(x, y, 96, 96),
    "road_sw": lambda x, y: sd_arc(x, y, 0, 96), "road_wn": lambda x, y: sd_arc(x, y, 0, 0),
    "road_nesw": sd_nesw, "road_nwse": sd_nwse,
    "road_n_sw": poly((48, 0), C, (0, 96)),  "road_n_se": poly((48, 0), C, (96, 96)),
    "road_s_nw": poly((48, 96), C, (0, 0)),  "road_s_ne": poly((48, 96), C, (96, 0)),
    "road_e_nw": poly((96, 48), C, (0, 0)),  "road_e_sw": poly((96, 48), C, (0, 96)),
    "road_w_ne": poly((0, 48), C, (96, 0)),  "road_w_se": poly((0, 48), C, (96, 96)),
    "road_c_nw": lambda x, y: sd_nesw(x + 96, y),   # the NE-SW diagonal of the cell to the west / above
    "road_c_se": lambda x, y: sd_nesw(x - 96, y),
    "road_c_ne": lambda x, y: sd_nwse(x, y + 96),   # the NW-SE diagonal of the cell below
    "road_c_sw": lambda x, y: sd_nwse(x + 96, y),   # the NW-SE diagonal of the cell to the west
}
NOISE_CELL = 8
_lat = None
def noise(x, y):
    """Value noise, bilinear on an 8 px lattice, periodic every 96 px, in -1..1."""
    global _lat
    if _lat is None:
        r = random.Random(seed * 7919 + 1)
        n = 96 // NOISE_CELL
        _lat = [[r.uniform(-1, 1) for _ in range(n)] for _ in range(n)]
    n = 96 // NOISE_CELL
    fx, fy = (x % 96) / NOISE_CELL, (y % 96) / NOISE_CELL
    i, j = int(fx) % n, int(fy) % n
    tx, ty = fx - int(fx), fy - int(fy)
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a, b = _lat[j][i], _lat[j][(i + 1) % n]
    c, d = _lat[(j + 1) % n][i], _lat[(j + 1) % n][(i + 1) % n]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


def sweep(name, dirt96, g96):
    im = g96.copy(); px = im.load(); dp = dirt96.load()
    sd = SD[name]
    rc = rim_colour() + (255,)
    for y in range(96):
        for x in range(96):
            d = sd(x + 0.5, y + 0.5) + RAG * noise(x + 0.5, y + 0.5)
            if d <= 0:
                px[x, y] = rc if d > -RIM else dp[x, y]
    return im


# --pro DIR: pieces from a PixelLab Tiles Pro "roads" set (18 tiles of
# 32 px, edge rule, mask bits N=1 E=2 S=4 W=8). A 96 px piece is a 3x3 of
# those sub-tiles on the pack's grass; the road runs down the MIDDLE
# sub-tile of a straight side, so any piece meets any other. A diagonal is
# a 32 px staircase that leaves through the bottom corner sub-tile, and
# the cell BELOW it carries the COMPANION (road_c_ne under a NW-SE run,
# road_c_nw under a NE-SW run): one sub-tile of road in its top corner
# that bends into the next diagonal cell. Two companions only.
PRO = sys.argv[sys.argv.index("--pro") + 1] if "--pro" in sys.argv else None
N_, E_, S_, W_ = 1, 2, 4, 8
PIECES = {
    "road_ns":   {(1, 0): N_|S_, (1, 1): N_|S_, (1, 2): N_|S_},
    "road_ew":   {(0, 1): E_|W_, (1, 1): E_|W_, (2, 1): E_|W_},
    "road_ne":   {(1, 0): N_|S_, (1, 1): N_|E_, (2, 1): E_|W_},
    "road_es":   {(2, 1): E_|W_, (1, 1): E_|S_, (1, 2): N_|S_},
    "road_sw":   {(1, 2): N_|S_, (1, 1): S_|W_, (0, 1): E_|W_},
    "road_wn":   {(0, 1): E_|W_, (1, 1): W_|N_, (1, 0): N_|S_},
    "road_nwse": {(0, 0): W_|S_, (0, 1): N_|E_, (1, 1): W_|S_, (1, 2): N_|E_, (2, 2): W_|S_},
    "road_nesw": {(2, 0): E_|S_, (2, 1): N_|W_, (1, 1): E_|S_, (1, 2): N_|W_, (0, 2): E_|S_},
    "road_n_se": {(1, 0): N_|S_, (1, 1): N_|E_, (2, 1): W_|S_, (2, 2): N_|S_},
    "road_n_sw": {(1, 0): N_|S_, (1, 1): N_|W_, (0, 1): E_|S_, (0, 2): N_|S_},
    "road_s_nw": {(0, 0): W_|S_, (0, 1): N_|E_, (1, 1): W_|S_, (1, 2): N_|S_},
    "road_s_ne": {(2, 0): E_|S_, (2, 1): N_|W_, (1, 1): E_|S_, (1, 2): N_|S_},
    "road_e_nw": {(0, 0): W_|S_, (0, 1): N_|E_, (1, 1): E_|W_, (2, 1): E_|W_},
    "road_e_sw": {(0, 2): N_|S_, (0, 1): E_|S_, (1, 1): E_|W_, (2, 1): E_|W_},
    "road_w_ne": {(2, 0): E_|S_, (2, 1): N_|W_, (1, 1): E_|W_, (0, 1): E_|W_},
    "road_w_se": {(2, 2): N_|S_, (2, 1): S_|W_, (1, 1): E_|W_, (0, 1): E_|W_},
    "road_c_ne": {(2, 0): N_|E_},
    "road_c_nw": {(0, 0): N_|W_},
}


def pro_build():
    meta = json.load(open(os.path.join(PRO, "meta.json")))["tile_rules"]["tiles"]
    bymask = {}
    for name, r in meta.items():
        bymask.setdefault(r["mask"], Image.open(os.path.join(PRO, name + ".png")).convert("RGBA"))
    grass = Image.open("assets/glory-of-rome/art/tiles/grass.png").convert("RGBA")
    made = {}
    for name, cells in PIECES.items():
        im = grass.copy()
        for (a, b), mask in cells.items():
            im.paste(bymask[mask], (a * 32, b * 32))
        made[name] = im
    return made, grass


if PRO:
    made, g96 = pro_build()
    for name, im in made.items():
        im.save(os.path.join(out, name + ".png"))
    g96.save(os.path.join(out, "grass.png"))
    names = list(made)
    sheet = Image.new("RGBA", (5 * 100, 4 * 100), (40, 40, 40, 255))
    for i, n in enumerate(names):
        sheet.paste(made[n], ((i % 5) * 100, (i // 5) * 100))
    sheet.save(os.path.join(out, "sheet.png"))
    MOCK = ["..f........", "..hggj.....", ".....f.....", ".....o.....", ".....wm....", "......wm...", ".......wrgg"]
    code = {"f": "road_ns", "g": "road_ew", "h": "road_ne", "i": "road_es", "j": "road_sw", "k": "road_wn",
            "l": "road_nesw", "m": "road_nwse", "n": "road_n_sw", "o": "road_n_se", "p": "road_s_nw", "q": "road_s_ne",
            "r": "road_e_nw", "s": "road_e_sw", "t": "road_w_ne", "u": "road_w_se", "v": "road_c_nw", "w": "road_c_ne"}
    W = max(len(r) for r in MOCK); H = len(MOCK)
    mock = Image.new("RGBA", (W * 96, H * 96))
    for j, row in enumerate(MOCK):
        for i in range(W):
            c = row[i] if i < len(row) else "."
            mock.paste(g96 if c == "." else made[code[c]], (i * 96, j * 96))
    mock.save(os.path.join(out, "mock1x.png"))
    print(len(made), "road pieces (pro) ->", out)
    sys.exit(0)

rng = random.Random(seed)
made, made_v = {}, {}
grass = next(im for rows, im in setl if rows[1] == [0, 0] and rows[2] == [0, 0])
dirt = next(im for rows, im in setl if rows[1] == [1, 1] and rows[2] == [1, 1])
g96 = Image.new("RGBA", (96, 96)); d96 = Image.new("RGBA", (96, 96))
for b in range(N):
    for a in range(N): g96.paste(grass, (a * S, b * S)); d96.paste(dirt, (a * S, b * S))
if SWEEP:
    # swept pieces sit on the PACK grass, whatever set the dirt came from
    # (--grass PATH to build against a staged grass instead)
    GRASS_PATH = sys.argv[sys.argv.index("--grass") + 1] if "--grass" in sys.argv else "assets/glory-of-rome/art/tiles/grass.png"
    g96 = Image.open(GRASS_PATH).convert("RGBA")
g96.save(os.path.join(out, "grass.png"))
for name in SHAPES:
    if SWEEP:
        made[name] = sweep(name, d96, g96); made_v[name] = vertices(SHAPES[name], random.Random(0))
    else:
        made[name], made_v[name] = build(name, rng)
    made[name].save(os.path.join(out, name + ".png"))

names = list(SHAPES)
sheet = Image.new("RGBA", (5 * 100, 4 * 100), (40, 40, 40, 255))
for i, n in enumerate(names):
    sheet.paste(made[n], ((i % 5) * 100, (i // 5) * 100))
sheet.save(os.path.join(out, "sheet.png"))

# A mock: a road that runs south, turns east, turns south, then goes off on
# a diagonal with its companions, joins a straight run east and ends.
MOCK = [
    "..f........",
    "..hggj.....",
    ".....f.....",
    ".....ox....",
    ".....wmx...",
    "......wrgg.",
]
W = max(len(r) for r in MOCK); H = len(MOCK)
code = {"f": "road_ns", "g": "road_ew", "h": "road_ne", "i": "road_es", "j": "road_sw", "k": "road_wn",
        "l": "road_nesw", "m": "road_nwse", "n": "road_n_sw", "o": "road_n_se", "p": "road_s_nw", "q": "road_s_ne",
        "r": "road_e_nw", "s": "road_e_sw", "t": "road_w_ne", "u": "road_w_se",
        "v": "road_c_nw", "w": "road_c_ne", "x": "road_c_sw", "y": "road_c_se"}
mock = Image.new("RGBA", (W * 96, H * 96))
for j, row in enumerate(MOCK):
    for i in range(W):
        c = row[i] if i < len(row) else "."
        mock.paste(g96 if c == "." else made[code[c]], (i * 96, j * 96))
mock.save(os.path.join(out, "mock1x.png"))
print(len(made), "road pieces ->", out, "| contract sides with extra patterns:", check_contract(made_v))
