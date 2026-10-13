"""The Italia set at 48 px native, enlarged x2 to the pack's 96 px files. Every pixel is Retro Diffusion's.

Deterministic, and reads only files in the repo:
  art/primitives/italia48/wang/<kind>.png      RD Tileset Advanced corner sets, 24 px (jobs art/jobs/italia/wang_*):
                                               sea, river, plough, wheat, road -- each on the master reference's grass
  art/primitives/italia48/sprites/*.png        trees and peaks (jobs probe48_pine/cypress/oak, probe96_peak), the
                                               reference's stone bridge, the jetties
  art/primitives/italia48/objects/<name>.png   the picked stills of objects, heroes and boat (jobs obj_*, probe48_*)

  tiles    every terrain name Italia's map uses (art/tiles/<name>.png): four RD corner tiles to a cell for the
           grass, sea, fields, roads and rivers; forest and mountains packed from RD sprites on that grass
  objects  the objects, the four heroes (still + 4 walk frames) and the boat (4 frames), on one 48 px cell

A shared border is the same in every tile that shares it: corner tiles meet on the cell's corner and edge
points, which both neighbours read alike; a packed sprite that crosses a closed side is the same sprite in every
tile. So any two neighbours join, whatever their shapes and variants.
"""
import os, re, glob, random
from collections import Counter
import numpy as np
from PIL import Image

PRIM = "art/primitives/italia48"
PACK = "assets/glory-of-rome/art"
T = 48


# ---------------------------------------------------------------- sprites and packing

def half(im):
    """an exact halving: a pixel is opaque when 2 of its 4 are, in their commonest colour"""
    a = np.array(im.convert("RGBA"))
    H, W = a.shape[:2]
    out = np.zeros((H // 2, W // 2, 4), np.uint8)
    for y in range(H // 2):
        for x in range(W // 2):
            b = a[2 * y:2 * y + 2, 2 * x:2 * x + 2].reshape(-1, 4)
            op = [tuple(p[:3]) for p in b if p[3] > 127]
            if len(op) >= 2:
                out[y, x, :3] = Counter(op).most_common(1)[0][0]
                out[y, x, 3] = 255
    return Image.fromarray(out)


def trim(im):
    return im.crop(im.getbbox())


class Pack:
    """sprites packed into a cell: the same ones cross each closed side in every tile, the middle varies"""

    def __init__(self, sprites, small, big, top_spec, left_ys, n_inner, seed, n_vars=4):
        self.P = sprites
        R = random.Random(seed)
        self.TOP = []
        for kind, x0, dy in top_spec:
            p = R.choice(big if kind == "big" else small)
            h, w = self.P[p].shape[:2]
            self.TOP.append((p, min(T - w, max(0, x0 + R.randint(-2, 2))), -h + dy + R.randint(-2, 2)))
        self.LEFT = []
        for y0 in left_ys:
            p = R.choice(small)
            h, w = self.P[p].shape[:2]
            self.LEFT.append((p, -w // 2 + R.randint(-2, 2), min(T - h, max(0, y0 + R.randint(-2, 2)))))
        p = R.choice(small)
        h, w = self.P[p].shape[:2]
        self.CORNER = [(p, -w // 2, -h // 2)]
        self.VARS = []
        for _ in range(n_vars):
            k = []
            for _ in range(n_inner + R.randint(0, 1)):
                p = R.choice(small)
                h, w = self.P[p].shape[:2]
                k.append((p, R.randint(0, T - w), R.randint(max(0, T // 3 - h // 2), T - h)))
            self.VARS.append(k)

    def tile(self, open_sides, open_diag=(), v=0, seed=0):
        on, oe, os_, ow = ("n" in open_sides), ("e" in open_sides), ("s" in open_sides), ("w" in open_sides)
        sp = list(self.VARS[v % len(self.VARS)])
        if not on:
            sp += self.TOP
        if not os_:
            sp += [(p, x, y + T) for p, x, y in self.TOP]
        if not ow:
            sp += self.LEFT
        if not oe:
            sp += [(p, x + T, y) for p, x, y in self.LEFT]
        for (dx, dy), a, b, d in (((0, 0), on, ow, "nw" in open_diag), ((T, 0), on, oe, "ne" in open_diag),
                                  ((0, T), os_, ow, "sw" in open_diag), ((T, T), os_, oe, "es" in open_diag)):
            if not (a or b or d):
                sp += [(p, x + dx, y + dy) for p, x, y in self.CORNER]
        can = Image.new("RGBA", (3 * T, 3 * T))
        can.paste(Image.fromarray(np.tile(grass_cell(), (3, 3, 1))).convert("RGBA"), (0, 0))
        for p, x, y in sorted(sp, key=lambda s: s[2] + self.P[s[0]].shape[0]):
            can.alpha_composite(Image.fromarray(self.P[p]), (T + x, T + y))
        return np.array(can.convert("RGB"))[T:2 * T, T:2 * T]


_PACKS = {}


def packs():
    if _PACKS:
        return _PACKS
    S = f"{PRIM}/sprites"
    sp = lambda f: np.array(trim(half(Image.open(f"{S}/{f}").convert("RGBA"))))
    trees = [sp(f) for f in sorted(os.listdir(S)) if re.match(r"(pine|oak|cypress)_\d\.png", f)
             and f != "cypress_3.png"]                     # cypress 3 came back with its white background
    big = [np.array(trim(half(Image.open(f"{S}/peak_{i}.png").convert("RGBA")))) for i in range(1, 5)]
    small = [np.array(trim(half(half(Image.open(f"{S}/peak_{i}.png").convert("RGBA"))))) for i in range(1, 5)]
    _PACKS["forest"] = Pack(trees, list(range(len(trees))), list(range(len(trees))),
                            [("s", 0, 8), ("s", 13, 14), ("s", 26, 6), ("s", 36, 13), ("s", 6, 20), ("s", 30, 21)],
                            [4, 17, 30], 5, 61)
    mp, nb = big + small, len(big)
    # big peaks only (small ones read as a field of cones): one standing on the top line, shared, and one in each
    # variant's middle wherever it falls
    m = Pack(mp, list(range(nb)), list(range(nb)), [("big", 5, 22)], [], 0, 57, n_vars=6)
    R = random.Random(58)
    for k in m.VARS:
        p = R.randrange(nb)
        h, w = mp[p].shape[:2]
        k.append((p, R.randint(0, T - w), T - h - R.randint(0, 3)))
    _PACKS["mountain"] = m
    return _PACKS


# ---------------------------------------------------------------- names

def _shape(name, prefix):
    """(open sides, open corners, variant) from <prefix>, <prefix>_edge_<sides>, _inner_<corner>, _v<N>"""
    rest = name[len(prefix):].lstrip("_")
    v = 0
    m = re.search(r"_?v(\d)$", rest)
    if m:
        v, rest = int(m.group(1)), rest[:m.start()]
    if rest.startswith("edge_"):
        return list(rest[5:]), (), v
    if rest.startswith("inner_"):
        return [], [rest[6:]], v
    return [], (), v


def make_tile(name, seed=0):
    """the 48 px RGB (RGBA for a dock) tile of a terrain name, or None for a name this set does not draw"""
    if name == "grass":
        return grass_cell()
    if name.startswith("water"):
        o, d, _ = _shape(name, "water")
        return area("sea", o, d)
    for k in ("fields_plough", "fields_wheat"):
        if name.startswith(k):
            o, d, _ = _shape(name, k)
            return area(k.split("_")[1], o, d)
    if name.startswith("forest") or name.startswith("mountain"):
        k = "forest" if name.startswith("forest") else "mountain"
        o, d, v = _shape(name, k)
        return packs()[k].tile(o, d, v, seed)
    if name.startswith("road_"):
        return line("road", name[5:])
    if name.startswith("river_mouth_"):
        k = name[12:]                       # drawn for ne: the river in from the west, the sea along n and e
        out = quarters("sea", (0, 1, 0, 0), (1, 1, 0, 1))
        if k in ("nw", "sw"):
            out = out[:, ::-1]
        if k in ("es", "sw"):
            out = out[::-1]
        return np.ascontiguousarray(out)
    if name.startswith("river_forest_"):
        return over(packs()["forest"].tile([], (), 0, seed), "river", line("river", name[13:]))
    if name.startswith("river_mountain_"):
        return over(packs()["mountain"].tile([], (), 0, seed), "river", line("river", name[15:]))
    if name.startswith("river_"):
        return line("river", name[6:])
    if name.startswith("bridge_river_"):
        d = name[13:]
        return bridge(line("river", "ns" if d == "ew" else "ew"), d)
    if name.startswith("bridge_"):
        d = name[7:]
        return bridge(area("sea", ()), d)
    if name.startswith("dock_"):
        return np.array(on48(half(Image.open(f"{PRIM}/sprites/dock_{name[5:]}.png").convert("RGBA")), foot=0))
    return None


def bridge(base, d):
    """the master reference's own stone bridge (art/primitives/italia48/sprites/bridge_key.png) across the cell:
    as drawn for a road running east-west, turned a quarter for north-south"""
    b = Image.open(f"{PRIM}/sprites/bridge_key.png").convert("RGBA")
    if d == "ns":
        b = b.rotate(90, expand=True)
    can = Image.fromarray(base).convert("RGBA")
    can.alpha_composite(b, ((T - b.width) // 2, (T - b.height) // 2))
    return np.array(can.convert("RGB"))


def x2(im):
    return im.resize((im.width * 2, im.height * 2), Image.NEAREST)


def build_tiles(names, out=f"{PACK}/tiles"):
    made, skipped = [], []
    for n in sorted(names):
        t = make_tile(n)
        if t is None:
            skipped.append(n)
            continue
        x2(Image.fromarray(t)).save(os.path.join(out, n + ".png"))
        made.append(n)
    return made, skipped


# ---------------------------------------------------------------- objects, heroes, boat

SMALL = {"chest", "artifact_chest", "artifact_ring", "sign"}      # half a cell, standing mid-cell
HEROES = ("legatus", "praetorianus", "sibylla", "dux")


def on48(im, foot=1):
    s = trim(im)
    can = Image.new("RGBA", (T, T))
    can.alpha_composite(s, ((T - s.width) // 2, max(0, T - s.height - foot)))
    return can


def lifted(still, offs):
    """frames of the still lifted by a pixel on alternate beats (a walk or a swell until RD animates them)"""
    out = []
    for o in offs:
        f = Image.new("RGBA", still.size)
        f.alpha_composite(still, (0, o) if o >= 0 else (0, 0), (0, 0) if o >= 0 else (0, -o))
        out.append(f)
    return out


def build_objects():
    done = []
    O = f"{PRIM}/objects"
    for p in sorted(glob.glob(f"{O}/*.png")):
        n = os.path.basename(p)[:-4]
        im = Image.open(p).convert("RGBA")
        if n in HEROES:
            still = on48(im)
            x2(still).save(f"{PACK}/classes/{n}_hero.png")
            for i, f in enumerate(lifted(still, [0, -1, 0, -1])):
                x2(f).save(f"{PACK}/classes/{n}_walk_{i:02d}.png")
        elif n == "boat":
            for i, f in enumerate(lifted(on48(im), [0, -1, 0, 1])):
                x2(f).save(f"{PACK}/sprites/boat_{i:02d}.png")
        else:
            x2(on48(half(im), foot=10) if n in SMALL else on48(im)).save(f"{PACK}/objects/{n}.png")
        done.append(n)
    return done


# ---------------------------------------------------------------- generated transitions (RD Tileset Advanced, 24 px)
#
# Each set (art/primitives/italia48/wang/<kind>.png, 4x5 tiles of 24 px) is RD's corner set: every tile is one
# pattern of inside (the terrain) and outside (the grass) at its four corners. A 48 px cell is four of them: their
# corners sit on the cell's corners, edge midpoints and centre, and each of those points is inside when every cell
# that meets there is that terrain (an area: sea, fields) or when the line runs through it (a road, a river).
# Neighbouring cells read the same points, so every quarter joins; all pixels are RD's.

W = 24
_WANG = {}


def wang(kind):
    """{(tl, tr, bl, br): 24 px tile} for a set, each tile's corners read from its pixels"""
    if kind in _WANG:
        return _WANG[kind]
    a = np.array(Image.open(f"{PRIM}/wang/{kind}.png").convert("RGB")).astype(int)
    tiles = [a[r * W:(r + 1) * W, c * W:(c + 1) * W] for r in range(a.shape[0] // W) for c in range(a.shape[1] // W)]
    grass = tiles[0]                                         # the set's outside fill sits top left
    inside = tiles[4 * 1 + 2]                                # the pond's centre: all inside
    g, i = grass.reshape(-1, 3).mean(0), inside.reshape(-1, 3).mean(0)
    def corner(t, y, x):
        p = t[y:y + 4, x:x + 4].reshape(-1, 3).mean(0)
        return int(np.abs(p - i).sum() < np.abs(p - g).sum())
    out = {}
    for t in tiles:
        sig = (corner(t, 0, 0), corner(t, 0, W - 4), corner(t, W - 4, 0), corner(t, W - 4, W - 4))
        out.setdefault(sig, t.astype(np.uint8))
    _WANG[kind] = out
    return out


def quarters(kind, c, m, centre=1):
    """a cell from its 3x3 points: c = corners (nw, ne, sw, se), m = midpoints (n, e, s, w)"""
    nw, ne, sw, se = c
    n, e, s, w = m
    q = {(0, 0): (nw, n, w, centre), (0, 1): (n, ne, centre, e), (1, 0): (w, centre, sw, s), (1, 1): (centre, e, s, se)}
    ws = wang(kind)
    out = np.zeros((T, T, 3), np.uint8)
    for (r, k), sig in q.items():
        t = ws.get(sig)
        if t is None:
            raise KeyError(f"{kind}: RD's set has no tile with corners {sig}")
        out[r * W:(r + 1) * W, k * W:(k + 1) * W] = t
    return out


def area(kind, open_sides, open_diag=()):
    """an area cell (sea, fields): a midpoint is outside on an open side, a corner outside when a side or the
    corner beside it is open"""
    m = tuple(0 if s in open_sides else 1 for s in "nesw")
    corners = (("nw", "n", "w"), ("ne", "n", "e"), ("sw", "s", "w"), ("es", "s", "e"))
    c = tuple(0 if (d in open_diag or a in open_sides or b in open_sides) else 1 for d, a, b in corners)
    return quarters(kind, c, m)


def line(kind, sides):
    """a road or river cell: in at the centre and the midpoints it runs through, out at the corners"""
    return quarters(kind, (0, 0, 0, 0), tuple(1 if s in sides else 0 for s in "nesw"))


def grass_cell():
    return quarters("road", (0, 0, 0, 0), (0, 0, 0, 0), centre=0)


def over(base, kind, cell):
    """the set's terrain pixels of cell laid on base (where they differ from the set's grass)"""
    g = np.tile(wang(kind)[(0, 0, 0, 0)], (2, 2, 1))
    m = np.abs(cell.astype(int) - g).sum(2) > 6
    out = base.copy()
    out[m] = cell[m]
    return out
