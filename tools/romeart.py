#!/usr/bin/env python3
"""Rome art: the one tool for every step the Glory of Rome's art and maps go
through. `python3 tools/romeart.py <command> --help` documents each command.

  Terrain tiles   lattice compose seamcheck slots stitch grass sweep edges
                  mouth bridge rebank tile2x2 mirror; the set passes fills
                  interiors aprons details edgevars; shore dock
  Zones           zone (a continent's whole set) install sheet
  Maps            map build|check|lint|sanity|render|place|zones|set|block
  Objects         objects move|add (zone objects in game.json)
  Screens         siegeslice siegewalls fieldcalm fieldgrade splashlogo
                  splashtitle classpicker icon introtheme crop
  Sprites         colormatch key troop install
  Review          review loopreview prompts provenance
  Paid            rdgen cost|run|reprocess|balance (Retro Diffusion)

THE RECORD. Every generation is a job file in art/jobs/ (its prompt, its
settings, its runs, the pack files it made: "pack"); docs/ROME-ART.md is
printed from them (`prompts`). Everything else in the pack is composited from
committed inputs by a command here -- the recipes `provenance check` holds
every shipped file to (scripts/check_maps.sh, CI). The commands are
deterministic: the same inputs give the same bytes.

ONLY `rdgen` REACHES THE NETWORK. `rdgen run` spends money: it quotes the cost
and posts only with --run. Nothing here writes into assets/ except `install`,
`troop install`, `map build`, `objects`, `map place --write` and the commands
given the pack as their output directory.
"""
import argparse
import glob
import io
import json
import math
import os
import random
import shutil
import subprocess
import sys
import tempfile
from collections import deque

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = "assets/glory-of-rome"
ZONES = ("italia", "galliae", "africa", "oriens")     # Italia's set is the master set
SETS = ("", "galliae", "africa", "oriens")           # tile-set folders under art/tiles
JOBS = "art/jobs"
TILE = 96                                            # the pack tile, px
FONT = "/usr/share/fonts/opentype/urw-base35/C059-Bold.otf"   # drawn lettering
MOUNTAIN_SHADOW = [2, 3, 0.35]      # compose's contact shadow for every mountain set (#63)


# ==========================================================================
# Commands: each registers its arguments; main() builds the one argparse CLI
# ==========================================================================

COMMANDS = {}


def A(*flags, **kw):
    """One argparse argument for @command."""
    return flags, kw


def command(name, help, *args, setup=None):
    """Register `romeart.py <name>`: args are A(...) arguments, or setup(p)
    builds a command's own subcommands."""
    def reg(f):
        COMMANDS[name] = (f, help, args, setup)
        return f
    return reg


def need(module, why):
    """An optional heavy import, with the reason it is needed."""
    try:
        return __import__(module)
    except ImportError:
        sys.exit(f"romeart: {why} needs the Python module '{module}'")


# ==========================================================================
# Core: JSON files and game.json edited as text
# ==========================================================================

def load_json(path):
    with open(path) as f:
        return json.load(f)


def save_json(path, obj, sort_keys=False):
    """The repo's JSON layout: indent 1, a final newline."""
    with open(path, "w") as f:
        json.dump(obj, f, indent=1, sort_keys=sort_keys)
        f.write("\n")


# game.json is cJSON's formatted print, hand-edited in places, so it is never
# re-printed whole: an edit replaces one value in the text and the result is
# checked to still parse.

def cj_obj(o, depth):
    """One flat object as cJSON prints it inside an array at this depth."""
    pad = "\t" * (depth + 1)
    body = ",\n".join(f"{pad}{json.dumps(k, ensure_ascii=False)}:\t"
                      f"{json.dumps(v, ensure_ascii=False) if isinstance(v, str) else json.dumps(v)}"
                      for k, v in o.items())
    return "{\n" + body + "\n" + "\t" * depth + "}"


def gj_zone_array_span(text, zid, key):
    """(start, end) of a zone's `key` array in game.json's text, '[' to ']'."""
    zi = text.find(f'\n\t\t\t"id":\t"{zid}",')
    if zi < 0:
        sys.exit(f"romeart: game.json has no zone '{zid}' in the expected layout")
    nxt = text.find('\n\t\t}, {\n', zi)
    nxt = nxt if nxt >= 0 else len(text)
    ki = text.find(f'\n\t\t\t"{key}":\t', zi, nxt)
    if ki < 0:
        sys.exit(f"romeart: game.json's zone '{zid}' has no {key} array")
    start = text.index("[", ki)
    if text.startswith("[]", start):
        return start, start + 1
    return start, text.index("\n\t\t\t\t}]", start) + len("\n\t\t\t\t}]") - 1


def gj_set_zone_array(text, zid, key, objs, append=False):
    """Replace a zone's array of flat objects, or append to it."""
    s, e = gj_zone_array_span(text, zid, key)
    items = ", ".join(cj_obj(o, 4) for o in objs)
    if not append:
        return text[:s] + "[" + items + "]" + text[e + 1:]
    if not objs:
        return text
    if text[s:e + 1] == "[]":
        return text[:s] + "[" + items + "]" + text[e + 1:]
    return text[:e] + ", " + items + text[e:]


def gj_write(pack, text):
    json.loads(text)                       # still valid JSON
    with open(os.path.join(pack, "game.json"), "w", encoding="utf-8") as f:
        f.write(text)


# ==========================================================================
# Small tile operations
# ==========================================================================

def pixels(im):
    """An image's pixels in row order (Pillow 12 renamed getdata)."""
    return im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()


def rgba(path, size=None):
    """An image as RGBA, optionally resized (nearest) to size x size."""
    im = Image.open(path).convert("RGBA")
    return im.resize((size, size), Image.NEAREST) if size and im.size != (size, size) else im


def same_pixels(a, b):
    """Two image files with the same size and RGBA pixels (a missing file never is)."""
    if not (os.path.exists(a) and os.path.exists(b)):
        return False
    x, y = rgba(a), rgba(b)
    return x.size == y.size and x.tobytes() == y.tobytes()


def tile_into(src, size=TILE, mode="RGBA"):
    """A small seamless ground tile laid edge to edge over size x size."""
    out = Image.new(mode, (size, size))
    for y in range(0, size, src.height):
        for x in range(0, size, src.width):
            out.paste(src, (x, y))
    return out


@command("tile2x2", "lay a 48 px seamless tile 2x2 into the 96 px pack tile",
         A("src"), A("out"))
def cmd_tile2x2(a):
    src = rgba(a.src)
    tile_into(src, src.width * 2).save(a.out)
    print(a.out, (src.width * 2, src.height * 2))


@command("mirror", "keep one half of a tile and mirror it over the other (exact symmetry)",
         A("src"), A("out"), A("keep", choices=("bottom", "top", "left", "right")))
def cmd_mirror(a):
    im = Image.open(a.src)
    w, h = im.size
    if a.keep in ("bottom", "top"):
        half = im.crop((0, h // 2, w, h)) if a.keep == "bottom" else im.crop((0, 0, w, h // 2))
        im.paste(half, (0, h // 2) if a.keep == "bottom" else (0, 0))
        im.paste(half.transpose(Image.FLIP_TOP_BOTTOM), (0, 0) if a.keep == "bottom" else (0, h // 2))
    else:
        half = im.crop((w // 2, 0, w, h)) if a.keep == "right" else im.crop((0, 0, w // 2, h))
        im.paste(half, (w // 2, 0) if a.keep == "right" else (0, 0))
        im.paste(half.transpose(Image.FLIP_LEFT_RIGHT), (0, 0) if a.keep == "right" else (w // 2, 0))
    im.save(a.out)


@command("crop", "centre-crop a still to the pack size (portraits generated at 128 drop their painted frame)",
         A("src"), A("out"), A("size", nargs="?", type=int, default=TILE),
         A("top", nargs="?", type=int, help="pin the crop's top row instead of centring"))
def cmd_crop(a):
    im = Image.open(a.src)
    x = (im.width - a.size) // 2
    y = a.top if a.top is not None else (im.height - a.size) // 2
    im.crop((x, y, x + a.size, y + a.size)).save(a.out)


def river_mouth(coast, river_ew, grass, sea):
    """A river mouth: the river enters from the west (sea to the east). Its
    band is every pixel where river_ew differs from the grass it was swept
    on, so it joins the river beside it exactly; across the tile each band
    pixel blends from the river's colour to the sea's on a smoothstep (no
    generator could draw a river-to-sea blend; Dan's exception, 2026-09-16)."""
    coast, river, grass, sea = (rgba(p, TILE) for p in (coast, river_ew, grass, sea))
    cw, rw, gw, sw = coast.load(), river.load(), grass.load(), sea.load()
    for y in range(TILE):
        for x in range(TILE):
            if rw[x, y] != gw[x, y]:
                t = x / (TILE - 1)
                t = t * t * (3 - 2 * t)
                cw[x, y] = tuple(int(rw[x, y][i] * (1 - t) + sw[x, y][i] * t) for i in range(3)) + (255,)
    return coast


@command("mouth", "a river mouth: river_ew's band on a coast tile, blending from river to sea",
         A("coast"), A("river_ew"), A("grass"), A("sea"), A("out"))
def cmd_mouth(a):
    river_mouth(a.coast, a.river_ew, a.grass, a.sea).save(a.out)
    print("mouth ->", a.out)


def nearest_mask(ref_path, fore_cols, back_cols, size):
    """A reference picture read as a shape: 255 where a pixel is nearer the
    foreground's colours than the background's, resized (nearest) to size."""
    near = lambda c, cols: min((r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2 for r, g, b in cols)
    ref = Image.open(ref_path).convert("RGB")
    m = Image.new("L", ref.size, 0)
    px, mp = ref.load(), m.load()
    for y in range(ref.height):
        for x in range(ref.width):
            mp[x, y] = 255 if near(px[x, y], fore_cols) <= near(px[x, y], back_cols) else 0
    return m.resize(size, Image.NEAREST)


def edges_into(tiles, pairs):
    """Each of art/reference/edges/<terrain>_edge_NN (the original 48x34
    tiles, water 00-11, the rest 01-12) read as a shape -- terrain or grass by
    which base's colours a pixel is nearer -- resized to the pack tile and
    filled with this set's base and grass, so every edge seams with its bases
    by construction (REQ-229). pairs: (art, the terrain whose shapes it takes)."""
    ref_dir = os.path.join(ROOT, "art", "reference", "edges")
    cols = lambda p: set(pixels(Image.open(p).convert("RGB")))
    grass = rgba(os.path.join(tiles, "grass.png"))
    g_cols = cols(os.path.join(ref_dir, "grass.png"))
    n = 0
    for art, ref in pairs:
        base = rgba(os.path.join(tiles, f"{art}.png"))
        t_cols = cols(os.path.join(ref_dir, f"{ref}.png"))
        for name in sorted(os.listdir(ref_dir)):
            if name.startswith(f"{ref}_edge_"):
                out = grass.copy()
                out.paste(base, (0, 0), nearest_mask(os.path.join(ref_dir, name), t_cols, g_cols, base.size))
                out.save(os.path.join(tiles, name.replace(f"{ref}_edge_", f"{art}_edge_")))
                n += 1
    return n


@command("edges", "the terrain edge tiles: the original edges' shapes filled with this set's bases",
         A("pack", nargs="?", default=PACK), A("set", nargs="?", default=""),
         A("--as", dest="as_", action="append", default=[], metavar="ART=TERRAIN",
           help="build only ART's edges from TERRAIN's shapes (fields_wheat=desert)"))
def cmd_edges(a):
    tiles = os.path.join(a.pack, "art", "tiles", a.set)
    pairs = [x.split("=", 1) for x in a.as_] or [(t, t) for t in ("water", "forest", "mountain", "desert")]
    print(f"wrote {edges_into(tiles, pairs)} edge tiles to {tiles}")


# ==========================================================================
# Forest and mountain: the lattice, the border contract, the composed tiles
# ==========================================================================
#
# THE BORDER CONTRACT (2026-09-10), checked by seamcheck. Every side of a
# tile is TERMINAL (grass beyond) or an INTERFACE (the same terrain beyond). A
# sprite may straddle at most ONE border, never a corner, so whether it exists
# depends only on that border's two cells, which both tiles know. A straddler
# across an interface is drawn by both tiles, each its own part, from the
# same lattice, so any two tiles join by construction. Nothing crosses a
# terminal border: a terminal side is finished with sprites fully inside the
# tile.
#
# THE LATTICE, per 96 px period, by each sprite's ink: the top row's ink tops
# on the north line, with a straddler across the east line whose topmost ink
# point sits on that line (round sprites cannot cover a corner from inside,
# so the straddlers' extreme points close the four corners); the bottom row
# the same with ink bottoms, the inside one ending 1 px above the south line;
# two lower rows (y 36 and 58) straddling the south border only, inset from
# the west and east lines (flush for trees). The plain tile must show no
# grass between its sprites (lattice prints the count; forest crown 3: 3 px).
#
# TERMINAL SIDES: north loses the rows drawn in from above; south loses the
# lower rows (the y 12 bases are the tree line); west and east lose their
# straddlers and take three edge sprites fully inside at their own insets
# (an outer corner drops the sprite nearest it). Codes 5-8 (diagonal only)
# are plain lattice. --ragged S,A,B[,T,U] (#63): a terminal side ends raggedly
# -- inside sprites within A or B px (alternating) of the open line go, small
# sprites S are laid loose in the gap clear of every straddler, and the west
# and east edge sprites are set back T (top) and U (bottom) px.

# The open sides of each edge code (REQ-229a/e): 1-4 corners, 9-12 sides,
# 13-18 strips and spits, 19 the island; 0 and 5-8 have none.
EDGE_OPEN = {11: "N", 12: "S", 9: "E", 10: "W", 1: "NW", 3: "NE", 2: "SW", 4: "SE",
             5: "", 6: "", 7: "", 8: "", 0: "",
             13: "NS", 14: "EW", 15: "NES", 16: "ESW", 17: "SWN", 18: "WNE", 19: "NESW"}


class Sprites:
    """A sprite folder (tile_NN.png), each image, ink box and alpha read once."""
    def __init__(self, d):
        self.dir, self._im, self._bb, self._al = d, {}, {}, {}

    def im(self, i):
        if i not in self._im:
            self._im[i] = rgba(os.path.join(self.dir, f"tile_{i:02d}.png"))
        return self._im[i]

    def bbox(self, i):
        if i not in self._bb:
            self._bb[i] = self.im(i).getbbox()
        return self._bb[i]

    def alpha(self, i):
        if i not in self._al:
            self._al[i] = self.im(i).split()[3].load()
        return self._al[i]

    def ink(self, i, x, y):
        l, t, r, b = self.bbox(i)
        return (x + l, y + t, x + r, y + b)          # right/bottom exclusive

    def ids(self):
        return sorted(int(f[5:7]) for f in os.listdir(self.dir) if f.startswith("tile_") and f.endswith(".png"))


def lattice_layout(sprites, name="forest", terrain="forest", crown=None, slots=None, ragged=None,
                   grass=f"{PACK}/art/tiles/grass.png"):
    """A forest or mountain layout under the border contract: {"sprites",
    "grass", "tiles": {name, name_edge_01..19: {"wrap": "", "sprites":
    [[i, x, y], ...]}}}, every sprite listed in draw order, negatives
    included, for compose. Returns (layout, grass pixels left showing)."""
    S = Sprites(sprites)
    if terrain == "forest":
        UPPER = LOWER = [[crown, crown], [crown, crown]]
        # edge sprites on a terminal west/east side: (sprite, inset, place)
        EDGE_W = [(crown, 0, "top"), (crown, 12, "mid"), (crown, 5, "bottom")]
        EDGE_E = [(crown, 8, "top"), (crown, 0, "mid"), (crown, 14, "bottom")]
    else:
        UPPER, LOWER = [[6, 0], [7, 1]], [[6, 5], [5, 7]]
        EDGE_W = [(7, 4, "top"), (3, 12, "mid"), (5, 0, "bottom")]
        EDGE_E = [(3, 0, "top"), (6, 14, "mid"), (0, 6, "bottom")]
        if slots:   # another rock set's own slots: which rock fits depends on its ink box
            s = load_json(slots)
            UPPER, LOWER = s["upper"], s["lower"]
            EDGE_W, EDGE_E = [tuple(e) for e in s["edge_w"]], [tuple(e) for e in s["edge_e"]]
    rag_top, rag_bot = (ragged[3:5] if ragged and len(ragged) >= 5 else (0, 0))
    ragged = ragged[:3] if ragged else None

    def crossings(i, x, y):
        l, t, r, b = S.ink(i, x, y)
        return {k for k, hit in (("N", t < 0 < b), ("S", t < 96 < b), ("W", l < 0 < r), ("E", l < 96 < r)) if hit}

    def touches(i, x, y):
        l, t, r, b = S.ink(i, x, y)
        return r > 0 and b > 0 and l < 96 and t < 96

    def extreme_col(i, top):
        """The centre column of the sprite's topmost (or lowest) ink row."""
        l, t, r, b = S.bbox(i)
        row = t if top else b - 1
        xs = [x for x in range(l, r) if S.alpha(i)[x, row] > 0]
        return (xs[0] + xs[-1]) // 2

    LOWER_Y = (36, 58)
    # rocks stop short of the west and east lines, so no shared rock lies
    # along them; a tree closes no corner from its trunk, so trees stay flush
    LOWER_INSET = ((12, 8), (8, 12)) if terrain != "forest" else ((0, 0), (0, 0))

    def period():
        """One 96 px period: (sprite, x, y, layer); layer 0 the lower rows."""
        pts = []
        for k in range(2):
            for j in range(2):
                i = LOWER[k][j]; l, t, r, b = S.bbox(i); ins = LOWER_INSET[k][j]
                pts.append((i, -l + ins if j == 0 else 96 - r - ins, LOWER_Y[k], 0))
        for j in range(2):
            i = UPPER[0][j]; l, t, r, b = S.bbox(i)
            pts.append((i, 0 if j == 0 else 96 - extreme_col(i, True), -t, 1))
        i = UPPER[1][0]; l, t, r, b = S.bbox(i)
        pts.append((i, 0, 95 - b, 1))
        i = UPPER[1][1]; l, t, r, b = S.bbox(i)
        pts.append((i, 96 - extreme_col(i, False), 96 - b, 1))
        # every sprite moved -1, 0 or +1 px in y, seeded per slot and the same
        # in every tile so straddlers still match; never into another line
        rng = random.Random(1)
        out = []
        for (i, x, y, lay) in pts:
            d = rng.choice((-1, 0, 1))
            if len(crossings(i, x, y + d)) > len(crossings(i, x, y)):
                d = 0
            out.append((i, x, y + d, lay))
        return out

    lattice = [(i, x + ox, y + oy, lay) for oy in (-96, 0, 96) for ox in (-96, 0, 96)
               for (i, x, y, lay) in period()]
    for (i, x, y, _) in lattice:
        if touches(i, x, y) and len(crossings(i, x, y)) > 1:
            sys.exit(f"romeart: sprite {i} at {x},{y} crosses {sorted(crossings(i, x, y))}: one border only")

    def ragged_side(keep, open_sides):
        small, reach_a, reach_b = ragged
        out, n = [], 0
        for (i, x, y, lay) in keep:
            l, t, r, bb = S.ink(i, x, y)
            if crossings(i, x, y) or lay == 2:      # straddlers and the side's own edge sprites stay
                out.append((i, x, y, lay)); continue
            reach = reach_a if n % 2 == 0 else reach_b
            n += 1
            if not (("N" in open_sides and t < reach) or ("S" in open_sides and bb > 96 - reach) or
                    ("W" in open_sides and l < reach) or ("E" in open_sides and r > 96 - reach)):
                out.append((i, x, y, lay))
        # loose sprites in the gap: on a few spots, kept where their ink covers
        # no straddler's ink and no other loose sprite
        sl, st, sr, sb = S.bbox(small)
        w, h = sr - sl, sb - st
        inkpx = set()
        for (i, x, y, lay) in keep:
            if crossings(i, x, y):
                a_ = S.alpha(i); l_, t_, r_, b_ = S.bbox(i)
                inkpx.update((x + xx, y + yy) for yy in range(t_, b_) for xx in range(l_, r_) if a_[xx, yy] > 0)
        sa = S.alpha(small)
        mine = [(xx - sl, yy - st) for yy in range(st, sb) for xx in range(sl, sr) if sa[xx, yy] > 0]
        placed, spots = [], []
        for side in "NESW":
            if side in open_sides:
                for f in (0.22, 0.5, 0.78):
                    c = int(96 * f)
                    spots.append({"N": (c - w // 2, 6), "S": (c - w // 2, 90 - h),
                                  "W": (6, c - h // 2), "E": (90 - w, c - h // 2)}[side])
        for k, (px, py) in enumerate(spots):
            if k % 2:                       # every other spot: a gap of grass
                continue
            px, py = max(4, min(92 - w, px)), max(4, min(92 - h, py))
            box = (px, py, px + w, py + h)
            if not any((px + dx_, py + dy_) in inkpx for dx_, dy_ in mine) and \
                    all(box[2] <= a_ or box[0] >= c_ or box[3] <= b_ or box[1] >= d_ for (a_, b_, c_, d_) in placed):
                placed.append(box)
                out.append((small, px - sl, py - st, 3))
        return out

    def tile(code):
        open_sides = EDGE_OPEN[code]
        keep = [(i, x, y, lay) for (i, x, y, lay) in lattice
                if touches(i, x, y) and not crossings(i, x, y) & set(open_sides)]
        for side, spec in (("W", EDGE_W), ("E", EDGE_E)):
            if side not in open_sides:
                continue
            for (i, ins, place) in spec:
                if ragged and place == "mid" and code < 13:
                    continue          # a notch midway: the side is not one wall
                if (place == "top" and "N" in open_sides) or (place == "bottom" and "S" in open_sides):
                    continue
                l, t, r, b = S.bbox(i)
                y = {"top": 1 - t, "mid": 48 - (t + b) // 2, "bottom": 95 - b}[place]
                if ragged and code < 13:
                    ins += {"top": rag_top, "mid": 0, "bottom": rag_bot}[place]
                x = -l + ins if side == "W" else 96 - r - ins
                keep.append((i, max(-l, min(96 - r, x)), y, 2))      # fully inside, whatever the inset
        # strips and spits keep their full rows: thinned from both sides they
        # come apart into a string of beads
        if ragged and open_sides and code < 13:
            keep = ragged_side(keep, open_sides)
        if any(crossings(i, x, y) & set(open_sides) for (i, x, y, _) in keep):
            sys.exit(f"romeart: {name} code {code}: a sprite crosses a terminal line")
        # one draw order in every tile: lower rows, upper rows, edge sprites --
        # so nothing drawn on one side of a line covers a straddler on it
        keep = sorted(set(keep), key=lambda p: (min(p[3], 0), p[2], p[1]))
        return {"wrap": "", "sprites": [[i, x, y] for (i, x, y, _) in keep]}

    lay = {"sprites": sprites, "grass": grass, "tiles": {name: tile(0)}}
    for code in range(1, 20):
        lay["tiles"][f"{name}_edge_{code:02d}"] = tile(code)
    can = Image.new("RGBA", (288, 288))           # the plain tile among plain tiles
    for oy in (0, 96, 192):
        for ox in (0, 96, 192):
            for (i, x, y) in lay["tiles"][name]["sprites"]:
                can.alpha_composite(S.im(i), (x + ox, y + oy))
    a_ = can.crop((96, 96, 192, 192)).split()[3].load()
    return lay, sum(1 for y in range(96) for x in range(96) if a_[x, y] == 0)


def zone_lattice(zone, terrain, ragged=None):
    """A zone's own forest or mountain lattice from its primitives (tree crown
    1 for Oriens, 0 elsewhere; its rock_slots.json); None without sprites."""
    prim = os.path.join(ROOT, "art", "primitives", zone)
    spr = os.path.join(prim, "trees" if terrain == "forest" else "rocks")
    if not os.path.isdir(spr):
        return None
    slots = os.path.join(prim, "rock_slots.json")
    if terrain == "forest":
        return lattice_layout(spr, "forest", crown=1 if zone == "oriens" else 0)[0]
    return lattice_layout(spr, "mountain", "mountain", slots=slots if os.path.exists(slots) else None,
                          ragged=ragged)[0]


@command("lattice", "a forest or mountain layout under the border contract (for compose)",
         A("out", help="layout .json"), A("--sprites", required=True, help="tile_NN.png folder"),
         A("--name", default="forest"), A("--terrain", default="forest", choices=("forest", "mountain")),
         A("--crown", type=int, help="forest: the tree sprite"), A("--slots", help="mountain: rock_slots.json"),
         A("--ragged", help="S,A,B[,T,U]: ragged terminal sides (#63)"),
         A("--shadow", help="DX,DY,A: compose's contact shadow under the edge pieces (mountains: 2,3,0.35)"))
def cmd_lattice(a):
    ragged = [int(v) for v in a.ragged.split(",")] if a.ragged else None
    lay, holes = lattice_layout(a.sprites, a.name, a.terrain, a.crown, a.slots, ragged)
    if a.shadow:
        dx, dy, al = a.shadow.split(",")
        lay["shadow"] = [int(dx), int(dy), float(al)]
    json.dump(lay, open(a.out, "w"), indent=1)
    print(len(lay["tiles"]), "tiles,", a.terrain, "->", a.out, "| grass showing in the plain tile:", holes, "px")


def blit(im, spr, x, y):
    """alpha_composite at a position that may be negative or overhang."""
    sx, sy, dx, dy = max(0, -x), max(0, -y), max(0, x), max(0, y)
    if sx < spr.width and sy < spr.height and dx < im.width and dy < im.height:
        im.alpha_composite(spr, dest=(dx, dy), source=(sx, sy))


def compose_layout(lay, out, sprite_override=None):
    """Draw a layout's tiles into out/<name>.png over its ground (a small tile
    repeated). A tile is [[i, x, y], ...] in draw order, or {"wrap": "hv",
    "sprites": [...]}: wrap names the borders that continue into a tile of
    the same kind (h right, v bottom), where an overhanging sprite is drawn
    again 96 px back -- below everything for v. With "shadow": [dx, dy, a],
    an edge piece's sprites cast a soft contact shadow, faded to nothing 4 px
    from every line so it never seams (#63; the plain tile has none, so what
    is built from it still matches)."""
    os.makedirs(out, exist_ok=True)
    S = sprite_override or Sprites(lay["sprites"])
    ground = tile_into(rgba(lay["grass"]))
    ground.save(os.path.join(out, "grass.png"))
    for name, entry in lay["tiles"].items():
        places = entry["sprites"] if isinstance(entry, dict) else entry
        wrap = entry.get("wrap", "hv") if isinstance(entry, dict) else "hv"
        im = ground.copy()
        if lay.get("shadow") and "_edge_" in name:
            dx_, dy_, a_ = lay["shadow"]
            ink = Image.new("RGBA", (96, 96))
            for i, x, y in places:
                blit(ink, S.im(i), x + dx_, y + dy_)
            m = ink.getchannel("A").point(lambda v: 255 if v else 0).filter(ImageFilter.GaussianBlur(2))
            mp = m.load()
            for yy in range(96):
                for xx in range(96):
                    mp[xx, yy] = int(mp[xx, yy] * a_ * (min(4, xx, yy, 95 - xx, 95 - yy) / 4))
            sh = Image.new("RGBA", (96, 96), (0, 0, 0, 0)); sh.putalpha(m)
            im.alpha_composite(sh)
        for i, x, y in places:
            if "v" in wrap and y + S.im(i).height > 96:
                blit(im, S.im(i), x, y - 96)
                if "h" in wrap and x + S.im(i).width > 96:
                    blit(im, S.im(i), x - 96, y - 96)
        for i, x, y in places:
            blit(im, S.im(i), x, y)
            if "h" in wrap and x + S.im(i).width > 96:
                blit(im, S.im(i), x - 96, y)
        im.save(os.path.join(out, name + ".png"))
    return out


@command("compose", "draw a layout's tiles (lattice output, or a hand layout) into a folder",
         A("layout"), A("out"))
def cmd_compose(a):
    lay = load_json(a.layout)
    compose_layout(lay, a.out)
    for name, entry in lay["tiles"].items():
        print(name, len(entry["sprites"] if isinstance(entry, dict) else entry), "sprites")


def seam_violations(lay, report=print):
    """Check a lattice layout against the border contract; returns the count.

    For every pair of codes that can sit side by side: every straddler of the
    shared border in one tile must be held by the other at the mirrored
    position, and a terminal border has none. What is drawn in front of a
    straddler must match on both sides too, or the straddler shows a cut where
    the cover ends at the line -- unless the cover stops short of the line,
    straddles itself, meets the same sprite flush on the other side, or only
    touches the line in a run of at most 8 px (an outline, not a cut)."""
    S = Sprites(lay["sprites"])
    name = [k for k in lay["tiles"] if "_edge_" not in k][0]
    tiles = {0: lay["tiles"][name]}
    tiles.update({c: lay["tiles"][f"{name}_edge_{c:02d}"] for c in range(1, 20)})
    held = {c: {tuple(s) for s in t["sprites"]} for c, t in tiles.items()}
    ink = lambda s: S.ink(*s)
    bad = 0

    def straddlers(code, side):
        out = set()
        for s in tiles[code]["sprites"]:
            l, t, r, b = ink(s)
            if (side == "E" and l < 96 < r) or (side == "W" and l < 0 < r) or \
                    (side == "S" and t < 96 < b) or (side == "N" and t < 0 < b):
                out.add(tuple(s))
        return out

    for a in tiles:
        for side in "ESWN":
            st = straddlers(a, side) if side in EDGE_OPEN[a] else ()
            if st:
                bad += len(st)
                report(f"code {a:2d} {side} is TERMINAL but {len(st)} sprite(s) straddle it: {sorted(st)[:4]}")
    for a in tiles:
        for b in tiles:
            if "E" not in EDGE_OPEN[a] and "W" not in EDGE_OPEN[b]:
                sa, sb = straddlers(a, "E"), straddlers(b, "W")
                if {(i, x - 96, y) for (i, x, y) in sa} != sb:
                    bad += 1; report(f"H pair {a:2d}|{b:2d}: A crosses {sorted(sa)} vs B holds {sorted(sb)}")
            if "S" not in EDGE_OPEN[a] and "N" not in EDGE_OPEN[b]:
                sa, sb = straddlers(a, "S"), straddlers(b, "N")
                if {(i, x, y - 96) for (i, x, y) in sa} != sb:
                    bad += 1; report(f"V pair {a:2d}/{b:2d}: A crosses {sorted(sa)} vs B holds {sorted(sb)}")

    def covers(code, s):
        lst = [tuple(t) for t in tiles[code]["sprites"]]
        i_ = ink(s)
        return [u for u in lst[lst.index(s) + 1:]
                if ink(u)[0] < i_[2] and i_[0] < ink(u)[2] and ink(u)[1] < i_[3] and i_[1] < ink(u)[3]]

    def run_on_line(u, side):
        i, x, y = u
        a_ = S.alpha(i)
        if side in "EW":
            col = 95 - x if side == "E" else -x
            return sum(1 for yy in range(96) if 0 <= col < 96 and a_[col, yy] > 0)
        row = 95 - y if side == "S" else -y
        return sum(1 for xx in range(96) if 0 <= row < 96 and a_[xx, row] > 0)

    def flush_ok(other, side, u):
        l, t, r, b = ink(u)
        i, x, y = u
        li, lt, lr, lb = S.bbox(i)
        line, mirror = {"E": (r == 96, (i, -li, y)), "S": (b == 96, (i, x, -lt)),
                        "W": (l == 0, (i, 96 - lr, y)), "N": (t == 0, (i, x, 96 - lb))}[side]
        return not line or mirror in held[other] or run_on_line(u, side) <= 8

    for a in tiles:
        for b in tiles:
            for side, other, dx, dy in (("E", "W", -96, 0), ("S", "N", 0, -96)):
                if side in EDGE_OPEN[a] or other in EDGE_OPEN[b]:
                    continue
                for s in straddlers(a, side):
                    m = (s[0], s[1] + dx, s[2] + dy)
                    if m not in held[b]:
                        continue
                    cut = [u for u in covers(a, s) if not flush_ok(b, side, u)]
                    cut += [u for u in covers(b, m) if not flush_ok(a, other, u)]
                    if cut:
                        bad += 1
                        report(f"cover {side} pair {a:2d}|{b:2d}: straddler {s} cut by flush cover(s) {cut}")
    return bad


@command("seamcheck", "check a lattice layout against the border contract", A("layout"))
def cmd_seamcheck(a):
    print("violations:", seam_violations(load_json(a.layout)))


@command("slots", "search rock-slot arrangements for a mountain lattice (seamcheck 0, fewest grass px)",
         A("sprites"), A("out", help="rock_slots.json"), A("--tries", type=int, default=300),
         A("--seed", type=int, default=1), A("--straddle", help="sprite digits allowed to straddle, e.g. 4567"))
def cmd_slots(a):
    """Which rock fits a slot depends on its ink box, so a new rock set needs
    its own rock_slots.json. Draws random arrangements, keeps the one with no
    seam violations and the fewest grass pixels; --straddle names the rocks
    small enough to cross one border (Italia's 4-7, #67)."""
    rnd = random.Random(a.seed)
    ALL = list(range(len(Sprites(a.sprites).ids())))
    SMALL = [int(c) for c in a.straddle] if a.straddle else ALL
    INS = [0, 4, 6, 8, 12, 14]
    tmp = tempfile.mkdtemp()
    sl = os.path.join(tmp, "slots.json")
    res = []
    for _ in range(a.tries):
        slots = {"upper": [[rnd.choice(ALL), rnd.choice(SMALL)], [rnd.choice(ALL), rnd.choice(SMALL)]],
                 "lower": [[rnd.choice(SMALL), rnd.choice(SMALL)], [rnd.choice(SMALL), rnd.choice(SMALL)]],
                 "edge_w": [[rnd.choice(ALL), rnd.choice(INS), p] for p in ("top", "mid", "bottom")],
                 "edge_e": [[rnd.choice(ALL), rnd.choice(INS), p] for p in ("top", "mid", "bottom")]}
        json.dump(slots, open(sl, "w"))
        try:
            lay, holes = lattice_layout(a.sprites, "mountain", "mountain", slots=sl)
        except SystemExit:
            continue                          # a slot crossed two borders
        res.append((seam_violations(lay, report=lambda *_: None), holes, slots))
    shutil.rmtree(tmp)
    res.sort(key=lambda t: (t[0], t[1]))
    print(f"tried {a.tries}, layouts {len(res)}, zero-violation {sum(1 for r in res if r[0] == 0)}")
    for viol, grass, s in res[:10]:
        print(f"  violations {viol:4d} grass {grass:4d}  upper {s['upper']} lower {s['lower']} "
              f"w {[e[0] for e in s['edge_w']]} e {[e[0] for e in s['edge_e']]}")
    if not res or res[0][0]:
        sys.exit("no zero-violation arrangement found; raise --tries or change --straddle")
    json.dump(res[0][2], open(a.out, "w"), indent=1)      # the committed files' layout
    print("wrote", a.out)


@command("rebank", "carry river bands onto a new interior: rewrite <prefix>_*.png in place",
         A("old_ground"), A("new_ground"), A("tiles"), A("prefix"))
def cmd_rebank(a):
    """A river-through-terrain tile is the interior with the band swept over
    it, so every pixel unlike the old interior is band or rim: those pixels
    are kept and laid on the new interior (Italia's rivers, #67)."""
    np = need("numpy", "rebank")
    old, new = (np.array(rgba(p)) for p in (a.old_ground, a.new_ground))
    for f in sorted(os.listdir(a.tiles)):
        if f.startswith(a.prefix + "_") and f.endswith(".png"):
            t = np.array(rgba(os.path.join(a.tiles, f)))
            band = np.any(t != old, axis=2)
            out = new.copy(); out[band] = t[band]
            Image.fromarray(out).save(os.path.join(a.tiles, f))
            print(f"{f}: {int(band.sum())} band px")


def bridge_tiles(tiles, out):
    """The two river bridges from a set's road and river pieces: the road's
    pixels over the river, then over the river and 4 px of bank a straight
    deck between 5 px parapets (the road's stone mixed with pale travertine,
    a dark outer line, a joint every 8 px), and the deck's shadow on the
    water below it. Writes out/bridge_river_ew.png and _ns.png."""
    np = need("numpy", "bridge")
    load = lambda n: np.array(rgba(os.path.join(tiles, n))).astype(np.int32)
    P, LINE, SHADOW, BANK, PALE = 5, 0.55, 0.6, 4, (196, 188, 168)

    def build(road, river, grass):              # an east-west deck; north-south is transposed
        on_road = (road[:, :, :3] != grass[:, :, :3]).any(axis=2)
        wet = (river[:, :, :3] != grass[:, :, :3]).any(axis=2)
        im = river.copy()
        im[on_road] = road[on_road]
        cols = np.where(wet.any(axis=0))[0]
        x0, x1 = max(0, cols.min() - BANK), min(95, cols.max() + BANK)
        tops = [np.where(on_road[:, x])[0].min() for x in range(x0, x1 + 1)]
        bots = [np.where(on_road[:, x])[0].max() for x in range(x0, x1 + 1)]
        t, b, mt, mb = min(tops) - 1, max(bots) + 1, max(tops), min(bots)
        stone = road[on_road][:, :3].mean(axis=0)
        light = np.clip(stone * 0.4 + np.array(PALE) * 0.6, 0, 255)
        for x in range(x0, x1 + 1):
            im[:t, x] = river[:t, x]
            im[b + 1:, x] = river[b + 1:, x]
            for y in range(t + P, b - P + 1):     # the deck: cobbles from the road's solid middle
                if not on_road[y, x]:
                    im[y, x] = road[min(max(y, mt), mb), x]
            for y0, outer in ((t, t), (b - P + 1, b)):
                for y in range(y0, y0 + P):
                    c = stone * LINE if y == outer else light * (0.8 if x % 8 == 0 else 1.0)
                    im[y, x, :3] = c.astype(np.int32)
            for y in range(b + 1, min(96, b + 4)):
                if wet[y, x]:
                    im[y, x, :3] = (river[y, x, :3] * SHADOW).astype(np.int32)
        return im

    os.makedirs(out, exist_ok=True)
    tr = lambda a: a.transpose(1, 0, 2)
    G = load("grass.png")
    ew = build(load("road_ew.png"), load("river_ns.png"), G)
    ns = tr(build(tr(load("road_ns.png")), tr(load("river_ew.png")), tr(G)))
    for name, arr in (("bridge_river_ew.png", ew), ("bridge_river_ns.png", ns)):
        Image.fromarray(arr.astype(np.uint8), "RGBA").save(os.path.join(out, name))


@command("bridge", "the two river bridges from a set's road and river pieces", A("tiles"), A("out"))
def cmd_bridge(a):
    bridge_tiles(a.tiles, a.out)
    print(f"bridges -> {a.out}")


# ==========================================================================
# The set passes: what the shell draws round and inside woods and ranges
# ==========================================================================
#
# Each pass writes into art/tiles/[<set>/] for all four sets, from the set's
# own lattice and sprites (no generation), seeded per set so a rerun gives the
# same bytes. The shell picks among them per cell (src/map_render.c,
# src/tilevar.c); `map render --tiles` draws them the same way.

def for_each_set(pack):
    """(zone, tiles folder) for the master set (Italia) and each zone set."""
    for s in SETS:
        yield s or "italia", os.path.join(pack, "art", "tiles", s)


def whole(im):
    """Not drawn to its frame edge: a sprite cut square to straddle a tile
    line would stand on the grass cut off."""
    l, t, r, b = im.getbbox()
    return l > 0 and t > 0 and r < im.width and b < im.height


def island_clump(d, terrain, ground="grass", fam=""):
    """The free-standing clump of a set's island piece (<terrain>_edge_19):
    its pixels unlike the ground, on clear. For a set without kept sprites."""
    isl, gr = (Image.open(os.path.join(d, n)).convert("RGB")
               for n in (f"{terrain}{fam}_edge_19.png", f"{ground}.png"))
    ip, gp = isl.load(), gr.load()
    clump = Image.new("RGBA", isl.size, (0, 0, 0, 0))
    cp = clump.load()
    for y in range(isl.height):
        for x in range(isl.width):
            if ip[x, y] != gp[x, y]:
                cp[x, y] = ip[x, y] + (255,)
    return clump


def fills_set(zone, d):
    """Where a grass or sand cell has a wood or range on two adjacent sides,
    the shell draws <terrain>[_sand]_fill_<corner> into it, so a concave
    corner rounds off. A fill is the plain tile's lattice continued into the
    cell -- the whole sprites whose ink centre lies within 50 px of the corner
    -- on a 288 px canvas with the cell in the middle (drawn one cell up and
    left), so it joins the neighbours and no sprite is cut. Without sprites:
    the island clump, or the plain tile's pixels within a ragged radius."""
    CORNER = {"se": (96, 96), "ne": (96, 0), "sw": (0, 96), "nw": (0, 0)}
    n = 0
    for t in ("forest", "mountain"):
        lay = zone_lattice(zone, t)
        S = lay and Sprites(lay["sprites"])
        for fam, ground in (("", "grass"), ("_sand", "desert")):
            if not os.path.exists(os.path.join(d, f"{ground}.png")) or \
                    fam and not os.path.exists(os.path.join(d, f"{t}_sand_edge_01.png")):
                continue
            for corner, (cx, cy) in CORNER.items():
                out = Image.new("RGBA", (288, 288), (0, 0, 0, 0))
                if lay:
                    keep = []
                    for (i, x, y) in lay["tiles"][t]["sprites"]:
                        l, tp, r, b = S.bbox(i)
                        mx, my = x + (l + r) / 2, y + (tp + b) / 2
                        if 0 <= mx < 96 and 0 <= my < 96 and math.hypot(mx - cx, my - cy) <= 50:
                            keep.append((y, x, S.im(i)))
                    for (y, x, im) in sorted(keep, key=lambda k: (k[0], k[1])):
                        out.alpha_composite(im, (x + 96, y + 96))
                elif os.path.exists(os.path.join(d, f"{t}{fam}_edge_19.png")):
                    # the clump set into the corner, its middle 26 px in from the vertex
                    clump = island_clump(d, t, ground, fam)
                    clump = clump.crop(clump.getbbox())
                    mx, my = (cx - 26 if cx else cx + 26), (cy - 26 if cy else cy + 26)
                    out.alpha_composite(clump, (int(mx - clump.width / 2) + 96, int(my - clump.height / 2) + 96))
                else:
                    plain = rgba(os.path.join(d, f"{t}.png"))
                    rng = random.Random(f"{'' if zone == 'italia' else zone}{fam}{t}{corner}")
                    lobes = [rng.uniform(-7, 7) for _ in range(6)]
                    pp, op = plain.load(), out.load()
                    for y in range(96):
                        for x in range(96):
                            dx, dy = x - cx, y - cy
                            k = math.atan2(abs(dy), abs(dx)) / (math.pi / 2) * (len(lobes) - 1)
                            i0, f = int(k), k - int(k)
                            rad = 44 + lobes[i0] * (1 - f) + lobes[min(i0 + 1, len(lobes) - 1)] * f
                            if math.hypot(dx, dy) <= rad:
                                op[x + 96, y + 96] = pp[x, y][:3] + (255,)
                out.putalpha(out.getchannel("A").point(lambda v: 255 if v > 127 else 0))
                out.save(os.path.join(d, f"{t}{fam}_fill_{corner}.png"))
                n += 1
    return n


@command("fills", "inner-corner fills: a wood's or range's own sprites round a concave corner",
         A("pack", nargs="?", default=PACK))
def cmd_fills(a):
    n = sum(fills_set(zone, d) for zone, d in for_each_set(a.pack))
    print(f"wrote {n} fills")


def aprons_set(zone, d):
    """A grass or sand cell beside a wood or range draws one of three aprons
    for that side, or none (draw_aprons): one or two of the set's own sprites
    straddling the shared line, mostly on the grass, on a 288 px canvas with
    the cell in the middle and a clear background, so a straight side no
    longer reads as a cut-out line. Forest aprons use only the trees the wood
    is made of; a set without sprites takes the island clump."""
    n = 0
    for t in ("forest", "mountain"):
        lay = zone_lattice(zone, t) if t == "forest" else None
        spr = os.path.join(ROOT, "art", "primitives", zone, "trees" if t == "forest" else "rocks")
        if os.path.isdir(spr):
            S = Sprites(spr)
            used = {i for (i, _, _) in lay["tiles"][t]["sprites"]} if lay else None
            pool = [S.im(i).crop(S.im(i).getbbox()) for i in S.ids()
                    if (used is None or i in used) and whole(S.im(i))]
        else:
            clump = island_clump(d, t)
            pool = [c.crop(c.getbbox()) for c in (clump, clump.transpose(Image.FLIP_LEFT_RIGHT))]
        for side in "nesw":
            for v in (1, 2, 3):
                rng = random.Random(f"apron-{zone}-{t}-{side}-{v}")
                out = Image.new("RGBA", (288, 288), (0, 0, 0, 0))
                placed, drawn = [], []
                for _ in range(rng.choice((1, 2, 2))):
                    im = rng.choice(pool)
                    if rng.random() < 0.5:
                        im = im.transpose(Image.FLIP_LEFT_RIGHT)
                    w, h = im.size
                    along = w if side in "ns" else h
                    for _try in range(20):
                        u = rng.randint(4, 92)      # may lean onto the cells beside
                        if all(abs(u - q) >= along * 0.55 for q in placed):
                            break
                    else:
                        continue
                    placed.append(u)
                    dep = rng.randint(-10, 26)      # its middle this far onto the grass
                    cx, cy = {"n": (96 + u, 96 + dep), "s": (96 + u, 192 - dep),
                              "w": (96 + dep, 96 + u), "e": (192 - dep, 96 + u)}[side]
                    drawn.append((cy + h // 2, cx - w // 2, cy - h // 2, im))
                for (_, x_, y_, im) in sorted(drawn, key=lambda k: k[0]):   # back to front
                    out.alpha_composite(im, (x_, y_))
                out.save(os.path.join(d, f"{t}_apron_{side}_{v}.png"))
                n += 1
    return n


@command("aprons", "aprons: the set's own rocks and trees straddling a wood's or range's side",
         A("pack", nargs="?", default=PACK))
def cmd_aprons(a):
    n = sum(aprons_set(zone, d) for zone, d in for_each_set(a.pack))
    print(f"wrote {n} aprons")


def details_set(zone, d):
    """The set's own trees and rocks at half size, drawn on about one plain
    grass or sand cell in twelve with no wood, range or sea beside it
    (draw_details). Cosmetic: the cell stays walkable."""
    def small(im, f):
        im = im.crop(im.getbbox())
        return im.resize((max(1, int(im.width * f)), max(1, int(im.height * f))), Image.NEAREST)
    n = 0
    prim = os.path.join(ROOT, "art", "primitives", zone)
    lay = zone_lattice(zone, "forest")
    trees = [] if not lay else [
        im for im in (Sprites(lay["sprites"]).im(i)
                      for i in sorted({i for (i, _, _) in lay["tiles"]["forest"]["sprites"]})) if whole(im)]
    trees = trees or [island_clump(d, "forest")]
    rocks = [im for im in (rgba(os.path.join(prim, "rocks", f)) for f in sorted(os.listdir(os.path.join(prim, "rocks")))
                           if f.startswith("tile_") and f.endswith(".png")) if whole(im)] \
        if os.path.isdir(os.path.join(prim, "rocks")) else []
    rng = random.Random(f"detail-{zone}")
    # 1: a bush; 2: two bushes; 3: a stone; 4: two small stones
    plan = {1: [(trees, 0.55)], 2: [(trees, 0.48), (trees, 0.4)],
            3: [(rocks, 0.5)], 4: [(rocks, 0.4), (rocks, 0.32)]}
    for v, parts in plan.items():
        out = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
        spots = [(rng.randint(30, 50), rng.randint(34, 52)), (rng.randint(54, 70), rng.randint(50, 66))]
        drawn = []
        for (pool, f), (cx, cy) in zip(parts, spots):
            im = small(rng.choice(pool), f)
            if rng.random() < 0.5:
                im = im.transpose(Image.FLIP_LEFT_RIGHT)
            drawn.append((cy + im.height // 2, cx - im.width // 2, cy - im.height // 2, im))
        for (_, x_, y_, im) in sorted(drawn, key=lambda k: k[0]):
            out.alpha_composite(im, (max(0, min(96 - im.width, x_)), max(0, min(96 - im.height, y_))))
        out.save(os.path.join(d, f"detail_{v}.png"))
        n += 1
    return n


@command("details", "small detail: a bush, a stone or two on open grass or sand (detail_1..4)",
         A("pack", nargs="?", default=PACK))
def cmd_details(a):
    n = sum(details_set(zone, d) for zone, d in for_each_set(a.pack))
    print(f"wrote {n} details")


def nudged(S, sprites, rng, keep_test, dx=5, dy=4):
    """A variant of a tile's arrangement: every sprite straddling a tile line
    kept exactly (the neighbours draw it too, so tiles still join), every
    sprite fully inside flipped half the time and nudged a few px within the
    tile -- unless keep_test(i, x, y, box) drops it. Returns
    [(sprite, flipped, x, y)]."""
    out = []
    for (i, x, y) in sprites:
        l, tp, r, b = S.bbox(i)
        if not (x + l >= 0 and y + tp >= 0 and x + r <= 96 and y + b <= 96):
            out.append((i, False, x, y)); continue
        if not keep_test(i, x, y, (l, tp, r, b)):
            continue
        flip = rng.random() < 0.5
        jl, jt, jr, jb = S.im(i).transpose(Image.FLIP_LEFT_RIGHT).getbbox() if flip else (l, tp, r, b)
        nx = x + l - jl + rng.randint(-dx, dx)
        ny = y + tp - jt + rng.randint(-dy, dy)
        out.append((i, flip, max(-jl, min(96 - jr, nx)), max(-jt, min(96 - jb, ny))))
    return out


def interiors_set(zone, d):
    """Listed as the code's `variants`, the shell picks one per cell. A
    variant redraws the sprites fully inside the tile flipped and nudged
    (seeded per set and variant); without sprites, the interior is mirrored
    inside a kept 12 px border."""
    n = 0
    for t in ("forest", "mountain"):
        plain_p = os.path.join(d, f"{t}.png")
        if not os.path.exists(plain_p):
            continue
        plain = rgba(plain_p)
        lay = zone_lattice(zone, t)
        if not lay:
            for v, op in ((1, Image.FLIP_LEFT_RIGHT), (2, Image.FLIP_TOP_BOTTOM)):
                out = plain.copy()
                out.paste(plain.crop((12, 12, 84, 84)).transpose(op), (12, 12))
                out.save(os.path.join(d, f"{t}_v{v}.png")); n += 1
            continue
        S = Sprites(lay["sprites"])
        ground = rgba(os.path.join(d, "grass.png"))
        for v in (1, 2):
            out = ground.copy()
            for (i, flip, x, y) in nudged(S, lay["tiles"][t]["sprites"], random.Random(f"{zone}-{t}-{v}"),
                                          lambda *_: True):
                can = Image.new("RGBA", (288, 288))
                can.alpha_composite(S.im(i).transpose(Image.FLIP_LEFT_RIGHT) if flip else S.im(i), (x + 96, y + 96))
                out.alpha_composite(can.crop((96, 96, 192, 192)))
            out.save(os.path.join(d, f"{t}_v{v}.png")); n += 1
    return n


@command("interiors", "plain forest and mountain variants (<terrain>_v1, _v2), so a mass is no grid",
         A("pack", nargs="?", default=PACK))
def cmd_interiors(a):
    n = sum(interiors_set(zone, d) for zone, d in for_each_set(a.pack))
    print(f"wrote {n} interior variants")


def edgevars_set(zone, d):
    """As the interiors, but one in three inside rocks near the open side is
    left out too, and they are composed like the edge pieces (the ragged
    lattice, the contact shadow)."""
    n = 0
    lay = zone_lattice(zone, "mountain", ragged=[4, 20, 34, 0, 28] if zone == "italia" else [6, 20, 34, 12, 28])
    S = Sprites(lay["sprites"])
    tiles, extra = {}, {}
    for code, side in ((9, "E"), (10, "W")):
        for v in (1, 2):
            rng = random.Random(f"edgevar-{zone}-{code}-{v}")
            near = (lambda i, x, y, b: not ((x + b[0] < 30) and rng.random() < 0.34)) if side == "W" else \
                   (lambda i, x, y, b: not ((x + b[2] > 66) and rng.random() < 0.34))
            places = []
            for (i, flip, x, y) in nudged(S, lay["tiles"][f"mountain_edge_{code:02d}"]["sprites"],
                                          rng, near, dx=6, dy=5):
                if flip:                  # the flipped copy, as sprite 100 + i
                    extra[100 + i] = S.im(i).transpose(Image.FLIP_LEFT_RIGHT)
                places.append([100 + i if flip else i, x, y])
            tiles[f"mountain_edge_{code:02d}_v{v}"] = {"wrap": "", "sprites": places}
    S2 = Sprites(lay["sprites"])
    S2._im.update(extra)
    outd = tempfile.mkdtemp()
    compose_layout({"sprites": lay["sprites"], "grass": os.path.join(d, "grass.png"),
                    "shadow": MOUNTAIN_SHADOW, "tiles": tiles}, outd, sprite_override=S2)
    for name in tiles:
        shutil.copy(os.path.join(outd, name + ".png"), os.path.join(d, name + ".png")); n += 1
    shutil.rmtree(outd)
    return n


@command("edgevars", "mountain side variants (mountain_edge_09/10_v1, _v2), so a long side stops repeating",
         A("pack", nargs="?", default=PACK))
def cmd_edgevars(a):
    n = sum(edgevars_set(zone, d) for zone, d in for_each_set(a.pack))
    print(f"wrote {n} edge variants")
# ==========================================================================
# Terrain sets: corner tilesets stitched and swept into the pack's pieces
# ==========================================================================
#
# The sea, desert, road and river primitives are 16 px CORNER TILESETS kept
# under art/primitives/<zone>/<set>/ (tile_NN.png and tiles_meta.json, whose
# pattern_4x4 says which corners of each tile are the lower terrain, 0, the
# upper, 1, or a cliff's wall, 2; 255 is a wildcard). A 96 px piece is a 6x6
# grid of those tiles chosen by a 7x7 grid of vertices, so every pixel is one
# pixel of the generated art.

def corner_set(d):
    """[(pattern rows 0-3, middle two columns), tile image)] of a corner tileset."""
    out = []
    for i, t in enumerate(load_json(os.path.join(d, "tiles_meta.json"))):
        p = t["pattern_4x4"]
        out.append(([list(p[f"row_{r}"])[1:3] for r in range(4)], rgba(os.path.join(d, f"tile_{i:02d}.png"))))
    return out


def corner_pick(setl, rows):
    """The tile for a 4x4 pattern: rows 1-2 exact, rows 0 and 3 not
    contradicting a non-wildcard, the most agreement winning; None if none."""
    best, score = None, -1
    for prow, im in setl:
        if prow[1] != rows[1] or prow[2] != rows[2]:
            continue
        s, ok = 0, True
        for r in (0, 3):
            for a, b in zip(prow[r], rows[r]):
                if a != 255:
                    s, ok = (s + 1, ok) if a == b else (s, False)
        if ok and s > score:
            best, score = im, s
    return best


def plain_of(setl, v):
    """The set's plain tile of one terrain (0 lower, 1 upper)."""
    return next(im for rows, im in setl if rows[1] == [v, v] and rows[2] == [v, v])


# The engine's edge codes as corner values (nw ne sw se; l grass, u terrain),
# water numbered from 0 and the rest from 1 (REQ-229a); the strips, spits and
# island by their open sides (REQ-229e).
STITCH_STD = {1: "lllu", 2: "lull", 3: "llul", 4: "ulll", 5: "uuul", 6: "uluu", 7: "uulu", 8: "luuu",
              9: "ulul", 10: "lulu", 11: "lluu", 12: "uull", 13: "S:NS", 14: "S:EW", 15: "S:NES",
              16: "S:ESW", 17: "S:SWN", 18: "S:WNE", 19: "S:NESW"}
STITCH_WATER = {0: "llul", 1: "lllu", 2: "lull", 3: "ulll", 4: "uuul", 5: "uluu", 6: "uulu", 7: "luuu",
                8: "ulul", 9: "lulu", 10: "lluu", 11: "uull", 12: "S:NS", 13: "S:EW", 14: "S:NES",
                15: "S:ESW", 16: "S:SWN", 17: "S:WNE", 18: "S:NESW"}


def stitch_set(src, terrain, out, seed=1):
    """A terrain's plain tile, grass and every edge piece from its corner set.

    The terrain FILLS the tile: a border edge whose two corners are both grass
    is grass, a mixed edge only at its grass corner, every interior vertex
    terrain -- so terrain reaches the tile edge and grass is only the fringe
    the neighbour needs. Interior vertices beside the fringe then creep to
    grass at random (two passes, where two or more orthogonal neighbours are
    grass), so the fringe bulges and nicks; border vertices never change, they
    are shared. A cliff set (walls, value 2) creeps only under grass, bites
    crags two rows deep along a south fringe, and turns the vertex between
    terrain and the grass below it into wall."""
    setl = corner_set(src)
    has_wall = any(2 in r[1] + r[2] for r, _ in setl)
    S = setl[0][1].width
    N = TILE // S
    VAL = {"l": 0, "u": 1, "t": 2}
    rng = random.Random(seed)

    def vertices(corners):
        if corners == "llll":
            return [["l"] * (N + 1) for _ in range(N + 1)]
        v = [["u"] * (N + 1) for _ in range(N + 1)]          # v[j][i]
        if corners.startswith("S:"):                         # open sides: every vertex on one is grass
            o = corners[2:]
            for k in range(N + 1):
                for side, (j, i) in (("N", (0, k)), ("S", (N, k)), ("W", (k, 0)), ("E", (k, N))):
                    if side in o:
                        v[j][i] = "l"
        else:
            nw, ne, sw, se = corners
            for k in range(N + 1):
                if nw == "l" and ne == "l": v[0][k] = "l"
                if sw == "l" and se == "l": v[N][k] = "l"
                if nw == "l" and sw == "l": v[k][0] = "l"
                if ne == "l" and se == "l": v[k][N] = "l"
            v[0][0], v[0][N], v[N][0], v[N][N] = nw, ne, sw, se
        for _ in range(2):
            for j in range(1, N):
                for i in range(1, N):
                    if v[j][i] == "u" and not (has_wall and v[j - 1][i] != "l"):
                        near = sum(v[jj][ii] == "l" for ii, jj in ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)))
                        if near >= 2 and rng.random() < 0.35:
                            v[j][i] = "l"
        if has_wall:
            i = 1
            while i < N:            # crags: a step is two rows or more (the set has no one-row stagger)
                if v[N][i] == "l" and v[N - 1][i] == "u" and v[N - 3][i] == "u" and rng.random() < 0.3:
                    run = rng.choice((1, 1, 2))
                    for k in range(i, min(i + run, N)):
                        v[N - 1][k] = v[N - 2][k] = "l"
                    i += run + 1
                else:
                    i += 1
            for i in range(N + 1):
                if v[0][i] == "u" and v[1][i] == "l":
                    v[1][i] = "u"                    # no room for a wall under the top border
                for j in range(1, N):
                    if v[j][i] == "u" and v[j + 1][i] == "l":
                        v[j][i] = "t"
        return v

    def build(corners):
        # the creep can ask for a corner combination the set lacks: redraw
        for _ in range(60):
            v = vertices(corners)
            cells = []
            for b in range(N):
                for a in range(N):
                    r1, r2 = [VAL[v[b][a]], VAL[v[b][a + 1]]], [VAL[v[b + 1][a]], VAL[v[b + 1][a + 1]]]
                    r0 = [VAL[v[b - 1][a]], VAL[v[b - 1][a + 1]]] if b > 0 else list(r1)
                    r3 = [VAL[v[b + 2][a]], VAL[v[b + 2][a + 1]]] if b + 2 <= N else list(r2)
                    cells.append((a, b, corner_pick(setl, [r0, r1, r2, r3])))
            if all(t is not None for _, _, t in cells):
                im = Image.new("RGBA", (TILE, TILE))
                for a, b, t in cells:
                    im.paste(t, (a * S, b * S))
                return im
        sys.exit(f"romeart: no layout for {corners}")

    os.makedirs(out, exist_ok=True)
    made = {terrain: build("uuuu"), "grass": build("llll")}
    for code, corners in (STITCH_WATER if terrain == "water" else STITCH_STD).items():
        made[f"{terrain}_edge_{code:02d}"] = build(corners)
    for name, im in made.items():
        im.save(os.path.join(out, name + ".png"))
    return made


@command("stitch", "a terrain's tiles (plain, grass, every edge) from a 16 px corner set",
         A("src", help="corner set folder"), A("terrain"), A("out"), A("--seed", type=int, default=1))
def cmd_stitch(a):
    print(f"{len(stitch_set(a.src, a.terrain, a.out, a.seed))} tiles in {a.out}")


# ---- sweep: roads and rivers as swept bands --------------------------------
#
# Every piece is a signed distance (px from the band's edge, negative inside)
# plus a periodic value noise for a ragged edge, filled with the set's plain
# upper tile over the given ground. The noise repeats every 96 px and at a
# border every shape's distance equals the straight band's, so any piece meets
# any other; curves are true quarter circles, diagonals true 45-degree bands.
# A diagonal passes through a tile corner, so the two cells sharing it take a
# COMPANION (road_c_*): the band's triangle in that corner. The four ENDS
# (road_n/e/s/w, by their one exit) enter at full width for half the tile and
# stop at a slanted, frayed front -- each tilted its own way so no two are
# rotations of one shape -- so a run can stop in open grass.

SWEEP_HW = 16         # half width of a straight band, centred at 48
# ends: (power of the low edge, power of the high edge, centreline lean px, front slant)
SWEEP_END = {"n": (0.45, 0.30, +5.0, +0.60), "e": (0.30, 0.50, -4.0, -0.55),
             "s": (0.50, 0.32, -6.0, -0.65), "w": (0.32, 0.48, +4.0, +0.50)}
END_TIP, END_FULL, END_FRAY, END_MIN_W = 12.0, 48.0, 3.5, 6.0


def sweep_shapes():
    """name -> (signed distance, noise amplitude or None) for the 24 pieces."""
    HW = SWEEP_HW
    span = 96.0 - END_TIP - END_FULL

    def run(side, x, y):          # (how far along the run, 1+ at full width; offset off centre)
        return {"s": ((y - END_TIP) / span, x - 48.0), "n": (((96 - y) - END_TIP) / span, x - 48.0),
                "e": ((x - END_TIP) / span, y - 48.0), "w": (((96 - x) - END_TIP) / span, y - 48.0)}[side]

    def end(side):
        p_lo, p_hi, lean, slant = SWEEP_END[side]

        def f(x, y):
            u, off = run(side, x, y)
            if u <= 0.0:
                return 96.0                          # past the tip
            t = min(u, 1.0)
            off -= lean * (1.0 - t)
            return max(off - max(HW * t ** p_hi, END_MIN_W), -off - max(HW * t ** p_lo, END_MIN_W),
                       slant * off - u * span)       # the slanted front where the paving stops
        return f

    def fray(side):               # noise grows towards the tip: zero change where it meets a neighbour
        return lambda x, y: 1.0 + (END_FRAY - 1.0) * (1.0 - max(0.0, min(1.0, run(side, x, y)[0])))

    def seg(x, y, ax, ay, bx, by):
        vx, vy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy)))
        return math.hypot(x - ax - t * vx, y - ay - t * vy)

    def poly(*pts):                # a straight leg to the centre and a diagonal on, notch-free
        return lambda x, y: min(seg(x, y, *pts[i], *pts[i + 1]) for i in range(len(pts) - 1)) - HW
    nesw = lambda x, y: abs(x + y - 96) / math.sqrt(2) - HW
    nwse = lambda x, y: abs(x - y) / math.sqrt(2) - HW
    arc = lambda cx, cy: lambda x, y: abs(math.hypot(x - cx, y - cy) - 48) - HW
    C = (48, 48)
    sd = {"ns": lambda x, y: abs(x - 48) - HW, "ew": lambda x, y: abs(y - 48) - HW,
          "ne": arc(96, 0), "es": arc(96, 96), "sw": arc(0, 96), "wn": arc(0, 0), "nesw": nesw, "nwse": nwse,
          "n_sw": poly((48, 0), C, (0, 96)), "n_se": poly((48, 0), C, (96, 96)),
          "s_nw": poly((48, 96), C, (0, 0)), "s_ne": poly((48, 96), C, (96, 0)),
          "e_nw": poly((96, 48), C, (0, 0)), "e_sw": poly((96, 48), C, (0, 96)),
          "w_ne": poly((0, 48), C, (96, 0)), "w_se": poly((0, 48), C, (96, 96)),
          "c_nw": lambda x, y: nesw(x + 96, y), "c_se": lambda x, y: nesw(x - 96, y),     # a neighbour's diagonal
          "c_ne": lambda x, y: nwse(x, y + 96), "c_sw": lambda x, y: nwse(x + 96, y)}
    out = {k: (f, None) for k, f in sd.items()}
    out.update({s: (end(s), fray(s)) for s in "nesw"})
    return out


def sweep_set(src, out, prefix="road", ground=f"{PACK}/art/tiles/grass.png", rim=0.0, rim_shade=0.0, seed=1):
    """The 24 road (or river: a road of water) pieces, <prefix>_<shape>.png,
    the band filled with the set's plain upper tile over `ground`, its last
    `rim` px inside the edge shaded by rim_shade."""
    d96 = tile_into(plain_of(corner_set(src), 1))
    g96 = rgba(ground)
    r = random.Random(seed * 7919 + 1)
    lat = [[r.uniform(-1, 1) for _ in range(12)] for _ in range(12)]      # 8 px cells, 96 px period

    def noise(x, y):
        fx, fy = (x % 96) / 8, (y % 96) / 8
        i, j = int(fx) % 12, int(fy) % 12
        tx, ty = fx - int(fx), fy - int(fy)
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a, b, c, d = lat[j][i], lat[j][(i + 1) % 12], lat[(j + 1) % 12][i], lat[(j + 1) % 12][(i + 1) % 12]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty

    os.makedirs(out, exist_ok=True)
    g96.save(os.path.join(out, "grass.png"))
    dp = d96.load()
    for name, (sd, fray) in sweep_shapes().items():
        im = g96.copy()
        px = im.load()
        for y in range(96):
            for x in range(96):
                d = sd(x + 0.5, y + 0.5) + 3.0 * (fray(x + 0.5, y + 0.5) if fray else 1.0) * noise(x + 0.5, y + 0.5)
                if d <= 0:
                    if d > -rim and rim_shade:
                        r_, g_, b_, a_ = dp[x, y]
                        px[x, y] = (int(r_ * rim_shade), int(g_ * rim_shade), int(b_ * rim_shade), a_)
                    else:
                        px[x, y] = dp[x, y]
        im.save(os.path.join(out, f"{prefix}_{name}.png"))


@command("sweep", "the 24 road or river pieces swept from a corner set's plain tile",
         A("src", help="corner set folder"), A("out"), A("--prefix", default="road", help="road, river, river_forest ..."),
         A("--grass", default=f"{PACK}/art/tiles/grass.png", help="the ground the band is swept over"),
         A("--rim", type=float, default=0.0, help="px of rim just inside the edge"),
         A("--rim-shade", type=float, default=0.0, help="the rim is the band's colour times this"),
         A("--seed", type=int, default=1))
def cmd_sweep(a):
    sweep_set(a.src, a.out, a.prefix, a.grass, a.rim, a.rim_shade, a.seed)
    print(f"24 {a.prefix} pieces -> {a.out}")


# ==========================================================================
# Shores, docks and keyed sprites
# ==========================================================================

SHORE_CODES = list(range(14)) + [18]       # the water edges the pack draws a sand shore for (#63)


def shore_set(d):
    """Sand shores, water_sand_edge_NN, for a set with desert: each pixel of
    the set's water_edge_NN read as sea or shore (nearer the set's water or
    its grass colours), the shore filled with the set's desert -- so a sea
    whose coast is all sand draws a sand shore line, not a grass one."""
    if not os.path.exists(os.path.join(d, "desert.png")):
        return 0
    cols = lambda n: set(pixels(Image.open(os.path.join(d, n)).convert("RGB")))
    W, G = cols("water.png"), cols("grass.png")
    near = lambda c, cs: min((r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2 for r, g, b in cs)
    dp = rgba(os.path.join(d, "desert.png")).load()
    sea = {}
    for k in SHORE_CODES:
        im = rgba(os.path.join(d, f"water_edge_{k:02d}.png"))
        px = im.load()
        for y in range(TILE):
            for x in range(TILE):
                c = px[x, y][:3]
                if c not in sea:
                    sea[c] = near(c, W) <= near(c, G)
                if not sea[c]:
                    px[x, y] = dp[x, y]
        im.save(os.path.join(d, f"water_sand_edge_{k:02d}.png"))
    return len(SHORE_CODES)


@command("shore", "sand shores (water_sand_edge_NN) for every set with desert",
         A("pack", nargs="?", default=PACK))
def cmd_shore(a):
    print(f"wrote {sum(shore_set(d) for _, d in for_each_set(a.pack))} sand shores")


def key_sprite(im, threshold=80):
    """A generated sprite keyed off its magenta background: every pixel whose
    red and blue both exceed its green by more than `threshold` (the ground
    and its shadow) made clear."""
    im = im.convert("RGBA")
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a and r - g > threshold and b - g > threshold:
                px[x, y] = (0, 0, 0, 0)
    return im


@command("key", "key a generated sprite off its magenta background", A("src"), A("out"),
         A("--threshold", type=int, default=80))
def cmd_key(a):
    key_sprite(Image.open(a.src), a.threshold).save(a.out)
    print("keyed ->", a.out)


DOCK_DECK = os.path.join("art", "primitives", "pieces", "dock_deck.png")   # piece_dock run01, as generated


def dock_tiles(tiles, src=DOCK_DECK):
    """dock_<n|e|s|w>: the keyed deck cropped to its ink, centred along the
    shore and set 6 px from the land side of a clear tile. The shell draws
    it over that side's water_edge piece (the map's 'j')."""
    deck = key_sprite(Image.open(src))
    deck = deck.crop(deck.getbbox())
    w, h = deck.size
    for side, pos in (("n", ((96 - w) // 2, 6)), ("s", ((96 - w) // 2, 90 - h)),
                      ("w", (6, (96 - h) // 2)), ("e", (90 - w, (96 - h) // 2))):
        out = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
        out.alpha_composite(deck, pos)
        out.save(os.path.join(tiles, f"dock_{side}.png"))


@command("dock", "the four dock tiles from the committed deck", A("tiles", nargs="?", default=f"{PACK}/art/tiles"))
def cmd_dock(a):
    dock_tiles(a.tiles)
    print("docks ->", a.tiles)


# ==========================================================================
# Zones: a continent's whole tile set, from its primitives to the pack
# ==========================================================================
#
# The inputs are art/primitives/<zone>/ -- the grass, sea, desert, cobble and
# river corner sets and the tree and rock sprites, as generated -- and the
# master set (art/tiles/) for the few pieces a zone shares. `zone` builds
# every tile the zone's set ships into build/art/<zone>_tiles/out, the same
# bytes each time; `install` copies exactly that into the pack and lists it
# as the zone's tile_set_arts. Italia's own set is the master set, built by
# hand from committed layouts (art/primitives/italia/BUILD.md).

# Per zone: the pieces it takes as they are -- from the master set, or kept
# under art/primitives/<zone>/pieces/ (Galliae's causeway road bridges over
# its own water, drawn for the Sein in #218; Africa's irrigated farmland, the
# job fields_africa_irrigated laid 2x2, which Oriens shares).
ZONE_CFG = {"galliae": {"master": ["fields_plough", "fields_wheat"],
                        "pieces": ["galliae/pieces/bridge_h", "galliae/pieces/bridge_v"]},
            "africa": {"master": ["fields_plough"], "pieces": ["africa/pieces/fields_wheat"]},
            "oriens": {"master": ["fields_plough"], "pieces": ["africa/pieces/fields_wheat"]}}
ZONE_RAGGED = [6, 20, 34, 12, 28]     # mountain edges: loose rock 6, crags set back 12 and 28 px (#63)


def master_names(prefix):
    """The master set's art names with this prefix: a zone set ships the same names."""
    return [f for f in sorted(os.listdir(os.path.join(PACK, "art", "tiles")))
            if f.endswith(".png") and f.startswith(prefix)]


def zone_build(zone):
    if zone not in ZONE_CFG:
        sys.exit(f"romeart: no zone recipe for {zone} (Italia's set is the master set)")
    cfg = ZONE_CFG[zone]
    prim = os.path.join("art", "primitives", zone)
    stage = os.path.join("build", "art", f"{zone}_tiles")
    out = os.path.join(stage, "out")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    has = lambda p: os.path.exists(os.path.join(out, p))
    keep = lambda d, names: [Image.open(os.path.join(d, f)).save(os.path.join(out, f))
                             for f in names if os.path.exists(os.path.join(d, f))]

    # 1. the grass: the grass set's plain lower tile laid to the pack tile
    meta = load_json(os.path.join(prim, "grass", "tiles_meta.json"))
    g = [n for n, t in enumerate(meta) if all(t["corners"][k] == "lower" for k in ("NW", "NE", "SW", "SE"))][0]
    tile_into(rgba(os.path.join(prim, "grass", f"tile_{g:02d}.png"))).save(os.path.join(out, "grass.png"))
    # 2. sea and desert, stitched
    for src, terrain in (("sea", "water"), ("desert", "desert")):
        if os.path.isdir(os.path.join(prim, src)):
            stitch_set(os.path.join(prim, src), terrain, os.path.join(stage, terrain), seed=3)
            keep(os.path.join(stage, terrain), master_names(terrain))
    # 3. forest and mountain: the zone's lattice composed over its grass, and
    #    again over its desert for the pieces whose open sides all face sand
    #    (forest 07 and 08 have none: their codes went to the vista landmarks)
    for terrain in ("forest", "mountain"):
        lay = zone_lattice(zone, terrain, ragged=ZONE_RAGGED if terrain == "mountain" else None)
        lay["grass"] = os.path.join(out, "grass.png")
        if terrain == "mountain":
            lay["shadow"] = MOUNTAIN_SHADOW
        print(f"  {terrain}: violations: {seam_violations(lay)}")
        keep(compose_layout(lay, os.path.join(stage, terrain)), master_names(terrain))
        if has("desert.png"):
            lay["grass"] = os.path.join(out, "desert.png")
            sand = compose_layout(lay, os.path.join(stage, terrain + "_sand"))
            for k in range(1, 13):
                if not (terrain == "forest" and k in (7, 8)):
                    Image.open(os.path.join(sand, f"{terrain}_edge_{k:02d}.png")).save(
                        os.path.join(out, f"{terrain}_sand_edge_{k:02d}.png"))
    # 4. roads and rivers swept over the grass, the rivers again over forest and mountain
    for src, prefix, ground in (("cobble", "road", "grass"), ("river", "river", "grass"),
                                ("river", "river_forest", "forest"), ("river", "river_mountain", "mountain")):
        if os.path.isdir(os.path.join(prim, src)):
            sweep_set(os.path.join(prim, src), os.path.join(stage, prefix), prefix,
                      os.path.join(out, ground + ".png"), rim=2, rim_shade=0.8)
            keep(os.path.join(stage, prefix), master_names(prefix + "_"))
    # 5. the river bridges, and the mouths: east built, west its mirror, and
    #    both drawn the other way up for a sea to the south
    if has("river_ns.png") and has("road_ew.png"):
        bridge_tiles(out, out)
    if has("water_edge_02.png"):
        e = river_mouth(*(os.path.join(out, n) for n in ("water_edge_02.png", "river_ew.png", "grass.png", "water.png")))
        e.save(os.path.join(out, "river_mouth_e.png"))
        e = Image.open(os.path.join(out, "river_mouth_e.png"))
        e.transpose(Image.FLIP_LEFT_RIGHT).save(os.path.join(out, "river_mouth_w.png"))
        for k in ("e", "w"):
            Image.open(os.path.join(out, f"river_mouth_{k}.png")).transpose(Image.FLIP_TOP_BOTTOM).save(
                os.path.join(out, f"river_mouth_{k}_s.png"))
    # 6. what the zone shares with the master set, and the farmland's edges
    keep(os.path.join(PACK, "art", "tiles"), [n + ".png" for n in cfg["master"]])
    for p in cfg["pieces"]:
        shutil.copyfile(os.path.join("art", "primitives", p + ".png"), os.path.join(out, os.path.basename(p) + ".png"))
    if has("fields_wheat.png"):
        edges_into(out, [("fields_wheat", "desert"), ("fields_plough", "desert")])
    # 7. the sand shores and the set passes
    shore_set(out)
    for f in (fills_set, aprons_set, details_set, interiors_set, edgevars_set):
        f(zone, out)
    print(f"built {len([f for f in os.listdir(out) if f.endswith('.png')])} tiles in {out}")
    return out


@command("zone", "build a zone's whole tile set from its primitives into build/art/<zone>_tiles/out",
         A("zone", choices=sorted(ZONE_CFG)))
def cmd_zone(a):
    zone_build(a.zone)


@command("install", "copy a built zone set into the pack and list it as the zone's tile_set_arts",
         A("zone", choices=sorted(ZONE_CFG)))
def cmd_install(a):
    out = os.path.join("build", "art", f"{a.zone}_tiles", "out")
    if not os.path.isdir(out):
        sys.exit(f"romeart: nothing built for {a.zone} (run: romeart.py zone {a.zone})")
    arts = sorted(f[:-4] for f in os.listdir(out) if f.endswith(".png"))
    # game.json first, so a refused edit leaves the pack untouched: the
    # zone's tile_set_arts line is replaced inside that zone's own block
    p = os.path.join(PACK, "game.json")
    s = open(p).read()
    want = json.loads(s)
    zi = [i for i, z in enumerate(want["zones"]) if z["id"] == a.zone][0]
    # the listing keeps its order; names it lacks are appended, sorted
    old = [n for n in want["zones"][zi].get("tile_set_arts", []) if n in arts]
    listed = old + [n for n in arts if n not in old]
    start = s.find(f'\n\t\t\t"id":\t"{a.zone}",')
    end = s.find('\n\t\t}, {\n', start)
    end = end if end >= 0 else len(s)
    listing = '\t\t\t"tile_set_arts":\t["' + '", "'.join(listed) + '"],\n'
    k = s.find('\n\t\t\t"tile_set_arts":\t[', start, end)
    if k >= 0:
        s = s[:k + 1] + listing + s[s.index("\n", k + 1) + 1:]
    else:
        anchor = f'\t\t\t"map":\t"maps/{a.zone}.dat",\n'
        m = s.find(anchor, start, end)
        if m < 0:
            sys.exit("romeart: game.json's map line is not where expected")
        m += len(anchor)
        s = s[:m] + f'\t\t\t"tile_set":\t"{a.zone}",\n' + listing + s[m:]
    want["zones"][zi].update(tile_set=a.zone, tile_set_arts=listed)
    if json.loads(s) != want:
        sys.exit("romeart: refusing to write game.json -- the edit changed something else")
    dst = os.path.join(PACK, "art", "tiles", a.zone)
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(dst):
        if f.endswith(".png") and f[:-4] not in arts:
            os.remove(os.path.join(dst, f))          # the set ships exactly what it builds
    wrote = 0
    for n in arts:          # a file whose pixels are unchanged keeps its bytes
        if not same_pixels(os.path.join(out, n + ".png"), os.path.join(dst, n + ".png")):
            shutil.copyfile(os.path.join(out, n + ".png"), os.path.join(dst, n + ".png"))
            wrote += 1
    open(p, "w").write(s)
    print(f"installed the {a.zone} set: {len(arts)} tiles, {wrote} changed")


def contact_sheet(images, cols=8, cell=96, pad=8, bg=(25, 25, 25)):
    """[(name, image)] laid out on a dark sheet, each over a dark cell."""
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (cell + pad) + pad, rows * (cell + pad + 10) + pad), bg)
    for i, (_, im) in enumerate(images):
        c = Image.new("RGBA", (cell, cell), (30, 30, 30, 255))
        c.alpha_composite(im.convert("RGBA").resize((cell, cell), Image.NEAREST) if im.size != (cell, cell) else im.convert("RGBA"))
        sheet.paste(c.convert("RGB"), (pad + (i % cols) * (cell + pad), pad + (i // cols) * (cell + pad + 10)))
    return sheet


@command("sheet", "a review page of a zone's set: every tile at 1:1",
         A("zone"), A("out", nargs="?", help="default build/art/<zone>_sheet"))
def cmd_sheet(a):
    src = os.path.join(PACK, "art", "tiles", a.zone)
    if not os.path.isdir(src):
        sys.exit(f"romeart: {a.zone} has no tile set in the pack")
    out = a.out or os.path.join("build", "art", f"{a.zone}_sheet")
    os.makedirs(out, exist_ok=True)
    names = sorted(f for f in os.listdir(src) if f.endswith(".png"))
    contact_sheet([(n, Image.open(os.path.join(src, n))) for n in names]).save(os.path.join(out, "tiles.png"))
    print(f"wrote {out}/tiles.png: {len(names)} tiles")


# ==========================================================================
# Maps: the zone maps, from hand-drawn source to the .dat the engine loads
# ==========================================================================
#
#   map build  <pack> <zone> <source.txt> <out.dat> [--strict]
#   map check  <pack> <zone> <map.dat>
#   map lint   <pack> <zone> [--update-allow] [--update-reach] [--all]
#   map sanity <pack> <map.dat> [WxH] [zone]
#   map render <pack> <map.dat> <out.png> [--tiles] [--zone Z] ...
#   map place  <pack> <zone> <map.dat> <regions.json> [--add] [--write]
#   map zones  <pack>              each zone's id, map file and size
#   map set    <zone> X,Y=C ...    edit source cells, gated (strict build, lint, reach)
#   map block  <zone> <y> <rows.txt>  replace a block of source rows, gated
#
# THE SOURCE is the map, art/maps/<zone>.txt: one character per tile, `#`
# lines are comments.
#   terrain   ~ sea  . grass  , grass variant  f forest  ^ mountain  d desert
#             p ploughed field  w wheat field (farmland: grass to the engine)
#   overlays  r river on grass  R river in forest  M river in mountains
#             = road  H bridge (a road crossing a river)
#   pieces    P T S O K G landmarks on grass; j a jetty (sea off a straight
#             shore, drawn as dock_<side>); n u h e farmstead, ruin, shrine,
#             well (solid set pieces on grass)
#
# `build` bakes every look into the .dat (GLORY-OF-ROME 10.6.1: nothing about
# a map's look is computed at game time): terrain edges by REQ-229a/e
# (cardinals before diagonals, the outside counting as the same terrain),
# rivers and roads by their links, bridges, river mouths. A shape the pack
# ships no piece for is an error naming the cell; shapes the art draws badly
# are warnings, errors under --strict. scripts/check_maps.sh rebuilds every
# map and fails if the shipped .dat differs, so a .dat is never hand-edited.
#
# A map file is latin-1: one byte per tile, a tile_codes key being the byte
# itself or a "\xNN" escape (engine/resources.c resolves them the same way).

MAP_DIRS4 = {'n': (0, -1), 'e': (1, 0), 's': (0, 1), 'w': (-1, 0)}
MAP_DIRS8 = dict(MAP_DIRS4, ne=(1, -1), se=(1, 1), sw=(-1, 1), nw=(-1, -1))
MAP_OPP = {'n': 's', 's': 'n', 'e': 'w', 'w': 'e',
           'ne': 'sw', 'sw': 'ne', 'nw': 'se', 'se': 'nw'}

# Source character -> the terrain its neighbours see, and -> the art of a cell
# no edge changes. Landmarks and set pieces are grass to their neighbours; a
# jetty (j) is sea, drawn as the dock over that side's shore.
MAP_BASE = {'~': 'water', '.': 'grass', ',': 'grass', 'f': 'forest',
            '^': 'mountain', 'd': 'desert', 'P': 'grass', 'T': 'grass',
            'S': 'grass', 'O': 'grass', 'K': 'grass', 'G': 'grass',
            'p': 'fields_plough', 'w': 'fields_wheat',
            'j': 'water', 'n': 'grass', 'u': 'grass', 'h': 'grass', 'e': 'grass'}
MAP_PLAIN = {'~': 'water', '.': 'grass', ',': 'grass_variant', 'f': 'forest',
             '^': 'mountain', 'd': 'desert', 'P': 'pharos', 'T': 'temple_ocean',
             'S': 'landmark_sibyl', 'O': 'landmark_oppidum', 'K': 'landmark_tophet',
             'G': 'landmark_gordian', 'p': 'fields_plough', 'w': 'fields_wheat',
             'j': None, 'n': 'piece_farmstead', 'u': 'piece_ruin',
             'h': 'piece_shrine', 'e': 'piece_well'}
MAP_RIVER = {'r': 'grass', 'R': 'forest', 'M': 'mountain', 'H': 'grass'}
MAP_ROAD = {'=', 'H'}
MAP_RIVER_PREFIX = {'grass': 'river_', 'forest': 'river_forest_',
                    'mountain': 'river_mountain_'}
# REQ-229a: two edge families, water 0-based, the rest 1-based; REQ-229e:
# strips, spits and the island, keyed by the open cardinals.
MAP_WATER_IDX = {'n': 10, 's': 11, 'e': 8, 'w': 9, 'ne_c': 0, 'nw_c': 1,
                 'sw_c': 2, 'se_c': 3, 'ne': 5, 'se': 4, 'sw': 6, 'nw': 7}
MAP_OTHER_IDX = {'n': 11, 's': 12, 'e': 9, 'w': 10, 'ne_c': 3, 'nw_c': 1,
                 'sw_c': 2, 'se_c': 4, 'ne': 6, 'se': 5, 'sw': 7, 'nw': 8}
MAP_SPIT_IDX = {frozenset('ns'): 13, frozenset('ew'): 14, frozenset('nes'): 15,
                frozenset('esw'): 16, frozenset('swn'): 17, frozenset('wne'): 18,
                frozenset('nesw'): 19}
MAP_CURVES = {frozenset('ns'): 'ns', frozenset('ew'): 'ew', frozenset('ne'): 'ne',
              frozenset('es'): 'es', frozenset('sw'): 'sw', frozenset('wn'): 'wn'}
MAP_JOINS = {('n', 'sw'), ('n', 'se'), ('s', 'nw'), ('s', 'ne'),
             ('e', 'nw'), ('e', 'sw'), ('w', 'ne'), ('w', 'se')}
MAP_OPEN_ARTS = ("grass", "grass_variant", "desert")   # where `place` may scatter


def map_die(msg):
    sys.exit(f"romeart map: {msg}")


def code_char(key):
    """A tile_codes key's map byte: the key itself, or its "\\xNN" escape."""
    if len(key) == 4 and key[0] == "\\" and key[1] in "xX":
        try:
            return chr(int(key[2:], 16))
        except ValueError:
            return None
    return key if len(key) == 1 else None


def load_game(pack):
    with open(os.path.join(pack, "game.json")) as f:
        return json.load(f)


def tile_codes(g):
    """(art name -> map byte, map byte -> its tile_codes entry)."""
    a2c, c2e = {}, {}
    for k, v in g["tile_codes"].items():
        c = code_char(k)
        if c is None:
            continue
        c2e[c] = v
        a2c.setdefault(v["art"], c)
    return a2c, c2e


def zone_of(g, zid):
    for z in g["zones"]:
        if z["id"] == zid:
            return z
    map_die(f"no zone '{zid}'")


def read_rows(path, pad=False):
    """A map file's rows, comments and blank lines dropped; pad=True fills short
    rows with grass, as the engine does."""
    with open(path, encoding="latin-1") as f:
        rows = [l.rstrip("\n").rstrip("\r") for l in f if l.strip() and not l.startswith("#")]
    if pad and rows:
        w = max(len(r) for r in rows)
        rows = [r.ljust(w, ".") for r in rows]
    return rows


def zone_objects(g, zid):
    """[(x, y, kind, label)] for everything the pack places in a zone, a castle
    at its gate; plus the town docks and the tiles a vista lays."""
    objs, docks, vistas = [], [], set()
    for t in g.get("towns", []):
        if t.get("zone") == zid:
            objs.append((t["x"], t["y"], "town", "town " + t["id"]))
            if t.get("boat", {}).get("x", -1) >= 0:
                docks.append((t["boat"]["x"], t["boat"]["y"]))
    for c in g.get("castles", []):
        if c.get("zone") == zid:
            objs.append((c.get("gate_x", c.get("x")), c.get("gate_y", c.get("y")),
                         "castle", "castle " + c["id"]))
    for z in g.get("zones", []):
        if z.get("id") != zid:
            continue
        for key, kind in (("chests", "chest"), ("signs", "sign"),
                          ("dwellings", "dwelling"), ("wandering_armies", "army")):
            objs += [(o["x"], o["y"], kind, kind) for o in z.get(key, []) if "x" in o and "y" in o]
        for ev in z.get("events", []):
            vistas |= {(fx["x"], fx["y"]) for fx in ev.get("effects", []) if "x" in fx and "y" in fx}
    return objs, docks, vistas


# ---- map build -------------------------------------------------------------

def map_build(pack, zid, src, out, strict=False):
    g = load_game(pack)
    a2c, _ = tile_codes(g)
    z = zone_of(g, zid)
    W, H = z["width"], z["height"]
    rows = read_rows(src)
    if len(rows) != H or any(len(r) != W for r in rows):
        map_die(f"{src}: want {W}x{H}, have {len(rows)} rows of "
                f"{sorted({len(r) for r in rows})}")
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c not in MAP_BASE and c not in MAP_RIVER and c not in MAP_ROAD:
                map_die(f"({x},{y}): unknown source character {c!r}")
    # towns and castles: their sprites cover the road under them
    objs = {(t["x"], t["y"]) for t in z.get("towns", [])} | {(c["x"], c["y"]) for c in z.get("castles", [])}
    errors, warnings, notes = [], [], []
    wants_sand = set()      # sand-backed edge pieces the pack does not ship

    def at(x, y):
        return rows[y][x] if 0 <= x < W and 0 <= y < H else None

    def inside(x, y):
        return 0 <= x < W and 0 <= y < H

    # A tile an event turns into a bridge (the Rubicon's) is road to its
    # neighbours, so the roads either side point at the crossing to come; the
    # Rubicon's own trigger cell draws its boundary stone over the road.
    codes = g["tile_codes"]
    future_bridge = {(fx["x"], fx["y"]) for ev in z.get("events", [])
                     for fx in ev.get("effects", [])
                     if "tile" in fx and codes.get(fx["tile"], {}).get("is_bridge")}
    landmark_on_road = {(ev["x"], ev["y"]) for ev in z.get("events", []) if ev.get("id") == "rubicon"}

    def links(x, y, kind):
        """The directions this cell's run continues in. Rivers link only
        orthogonally (movement is 8-way with no corner rule, so a diagonal
        river would let the hero step across it); a road takes a diagonal
        only where the run does not turn the corner itself."""
        if kind == 'river':
            member = lambda x, y: (at(x, y) or '') in MAP_RIVER
        else:
            member = lambda x, y: (at(x, y) or '') in MAP_ROAD or (x, y) in future_bridge
        out = {d for d, (dx, dy) in MAP_DIRS4.items() if member(x + dx, y + dy)}
        for d, (dx, dy) in MAP_DIRS8.items():
            if len(d) != 2 or not member(x + dx, y + dy) or member(x + dx, y) or member(x, y + dy):
                continue
            if kind == 'road':
                out.add(d)
            else:
                errors.append(f"({x},{y}): river links diagonally to "
                              f"({x + dx},{y + dy}) -- make it a corner")
        return out

    def piece(ex):
        orth = {d for d in ex if len(d) == 1}
        diag = {d for d in ex if len(d) == 2}
        if len(ex) == 1 and orth:
            return next(iter(orth))
        if len(ex) == 2 and len(orth) == 2:
            return MAP_CURVES.get(frozenset(orth))
        if len(ex) == 2 and len(diag) == 2:
            return {frozenset(('ne', 'sw')): 'nesw', frozenset(('nw', 'se')): 'nwse'}.get(frozenset(diag))
        if len(ex) == 2 and len(orth) == 1 and len(diag) == 1:
            o, c = next(iter(orth)), next(iter(diag))
            if (o, c) in MAP_JOINS:
                return f"{o}_{c}"
        return None

    out_art = [[None] * W for _ in range(H)]
    cls = [[MAP_BASE.get(rows[y][x]) or MAP_RIVER.get(rows[y][x]) or 'grass'
            for x in range(W)] for y in range(H)]          # terrain class for edges
    companions = {}                                         # (x, y) -> road corners

    for y in range(H):
        for x in range(W):
            c = rows[y][x]
            if c in MAP_RIVER and c != 'H':
                ex = links(x, y, 'river')
                prefix = MAP_RIVER_PREFIX[MAP_RIVER[c]]
                sea = [d for d, (dx, dy) in MAP_DIRS4.items() if at(x + dx, y + dy) == '~']
                if len(ex) == 1 and len(sea) >= 1:
                    # A river ending against the sea is its mouth. The mouth
                    # art has the open sea on its outflow side and along its
                    # top, land along its foot; _s is drawn the other way up.
                    inflow = next(iter(ex))
                    north, south = at(x, y - 1), at(x, y + 1)
                    flip = south == '~' and north != '~'
                    if inflow == 'w' and 'e' in sea:
                        out_art[y][x] = 'river_mouth_e' + ('_s' if flip else '')
                    elif inflow == 'e' and 'w' in sea:
                        out_art[y][x] = 'river_mouth_w' + ('_s' if flip else '')
                    else:
                        errors.append(f"({x},{y}): river meets the sea from the "
                                      f"{inflow}; the pack has mouths for east "
                                      f"and west only")
                    if (north == '~') == (south == '~'):
                        warnings.append(f"({x},{y}): river mouth wants the sea on "
                                        f"one of its north and south sides, land "
                                        f"on the other (has {north!r} and {south!r})")
                    cls[y][x] = 'water'      # a coast tile: its neighbours see sea
                    continue
                if len(ex) == 1:
                    o = MAP_OPP[next(iter(ex))]
                    if not inside(x + MAP_DIRS4[o][0], y + MAP_DIRS4[o][1]):
                        ex = ex | {o}          # the run flows off the map: a straight
                    elif c == 'r' and not any(
                            at(x + dx, y + dy) in ('^', 'f')
                            for d, (dx, dy) in MAP_DIRS4.items() if d != next(iter(ex))):
                        warnings.append(f"({x},{y}): river ends in open ground, "
                                        f"not at the sea, the map's edge or a "
                                        f"source in the mountains or woods")
                if c in 'RM':
                    for d, (dx, dy) in MAP_DIRS4.items():
                        n = at(x + dx, y + dy)
                        if n is None or n in MAP_RIVER or n == 'H':
                            continue
                        if MAP_BASE.get(n) != MAP_RIVER[c]:
                            warnings.append(f"({x},{y}): {MAP_RIVER[c]}-banked river "
                                            f"has {n!r} to its {d}; its drawn bank "
                                            f"meets it in a seam")
                            break
                p = piece(ex)
                if p is None:
                    errors.append(f"({x},{y}): river with exits {sorted(ex)} "
                                  f"has no piece")
                    continue
                out_art[y][x] = prefix + p
            elif c == 'H':
                rex, oex = links(x, y, 'river'), links(x, y, 'road')
                if rex == {'e', 'w'} and oex == {'n', 's'}:
                    out_art[y][x] = 'bridge_river_ns'
                elif rex == {'n', 's'} and oex == {'e', 'w'}:
                    out_art[y][x] = 'bridge_river_ew'
                else:
                    errors.append(f"({x},{y}): bridge needs a straight river "
                                  f"and the road across it at right angles "
                                  f"(river {sorted(rex)}, road {sorted(oex)})")
            elif c == '=':
                ex = links(x, y, 'road')
                p = piece(ex)
                if p is None:
                    if (x, y) in objs:         # any number of exits under a town
                        out_art[y][x] = 'grass'
                        continue
                    errors.append(f"({x},{y}): road with exits {sorted(ex)} "
                                  f"has no piece")
                    continue
                out_art[y][x] = 'road_' + p
                if (x, y) in landmark_on_road and p == 'ew' and 'landmark_rubicon' in a2c:
                    out_art[y][x] = 'landmark_rubicon'
                for d in ex:
                    if len(d) != 2:
                        continue
                    dx, dy = MAP_DIRS8[d]
                    # the corner a diagonal crosses, seen from each flank
                    for fx, fy, corner in ((x + dx, y, ('s' if dy > 0 else 'n') + ('w' if dx > 0 else 'e')),
                                           (x, y + dy, ('n' if dy > 0 else 's') + ('e' if dx > 0 else 'w'))):
                        companions.setdefault((fx, fy), set()).add(corner)

    for (x, y), corners in companions.items():
        c = at(x, y)
        if c is None or c in MAP_ROAD or c in MAP_RIVER:
            continue
        if len(corners) > 1:
            errors.append(f"({x},{y}): two diagonals cross this cell's corners "
                          f"{sorted(corners)}; there is no piece")
        elif MAP_BASE.get(c) != 'grass':
            errors.append(f"({x},{y}): a road diagonal's corner falls on "
                          f"{MAP_BASE.get(c)}; its companion needs grass")
        else:
            out_art[y][x] = 'road_c_' + next(iter(corners))

    def edge_idx(t, diff):
        """(index, None) for the edge piece showing these different
        neighbours, or (None, why) when the families have none."""
        card = frozenset(d for d in diff if len(d) == 1)
        m = MAP_WATER_IDX if t == 'water' else MAP_OTHER_IDX
        for pair in ('ne', 'nw', 'sw', 'se'):
            if card == frozenset(pair):
                return m[pair + '_c'], None
        if len(card) == 1:
            return m[next(iter(card))], None
        if not card:
            dg = sorted(d for d in diff if len(d) == 2)
            if len(dg) == 1:
                return m[dg[0]], None
            return None, f"{t} with open diagonals {dg} only; no variant"
        if card in MAP_SPIT_IDX:
            return MAP_SPIT_IDX[card] - (1 if t == 'water' else 0), None
        return None, f"{t} open on {sorted(card)}; no variant"

    for y in range(H):
        for x in range(W):
            if out_art[y][x] is not None:
                continue
            c = rows[y][x]
            if c not in MAP_BASE:
                continue            # an overlay whose error is already listed
            t = MAP_BASE[c]
            if t == 'grass':
                out_art[y][x] = MAP_PLAIN[c]
                continue
            near = {d: cls[y + dy][x + dx] for d, (dx, dy) in MAP_DIRS8.items()
                    if 0 <= x + dx < W and 0 <= y + dy < H}
            full = {d for d, k in near.items() if k != t}
            # Land edges fade to grass and the sea's edge draws the shore. Sand
            # keeps its ground to the coast (the sea draws a sand shore) and
            # leaves its edge to a wood or range that has sand edges, unless no
            # piece shows what is left, when the full shape stands.
            diff = {d for d in full if near[d] != 'water' and
                    not (near[d] in ('forest', 'mountain') and f"{near[d]}_sand_edge_01" in a2c)} \
                if t == 'desert' else full
            idx, why = (None, None) if not diff else edge_idx(t, diff)
            if diff and idx is None and diff != full:
                diff = full
                idx, why = edge_idx(t, diff)
            if not diff:
                if c == 'j':
                    errors.append(f"({x},{y}): a jetty in open sea; it wants one straight shore")
                else:
                    out_art[y][x] = MAP_PLAIN[c]
                continue
            if idx is None:
                errors.append(f"({x},{y}): {why}")
                continue
            card = frozenset(d for d in diff if len(d) == 1)
            if card:
                lost = sorted(d for d in diff if len(d) == 2 and d[0] not in card and d[1] not in card)
                if lost:
                    (warnings if t == 'water' else notes).append(
                        f"({x},{y}): {t} edge on {sorted(card)} cannot "
                        f"show its different diagonal {lost}")
            # A sea, wood or range whose every other neighbour is sand fades to
            # sand: its *_sand_edge piece, where the pack has it.
            family = f"{t}_edge"
            if t in ('water', 'forest', 'mountain') and all(near[d] == 'desert' for d in diff) \
                    and f"{t}_sand_edge_{idx:02d}" in a2c:
                family = f"{t}_sand_edge"
            elif t in ('forest', 'mountain') and all(near[d] == 'desert' for d in diff):
                wants_sand.add(f"{t}_sand_edge_{idx:02d}")
            name = f"{family}_{idx:02d}"
            if c == 'j':
                side = next(iter(card)) if len(card) == 1 else None
                if family != 'water_edge' or side is None or f"dock_{side}" not in a2c:
                    errors.append(f"({x},{y}): a jetty wants one straight grass shore "
                                  f"(has land on {sorted(diff)})")
                else:
                    out_art[y][x] = f"dock_{side}"
                continue
            if name not in a2c:
                errors.append(f"({x},{y}): {t} open on {sorted(diff)} wants "
                              f"{name}, which the pack does not ship")
                continue
            out_art[y][x] = name

    errors += [f"({x},{y}): the pack has no tile code for {out_art[y][x]}"
               for y in range(H) for x in range(W)
               if out_art[y][x] is not None and out_art[y][x] not in a2c]
    if wants_sand:
        warnings.append("wood or rock on sand drawn with a grass fringe; the pack "
                        "ships no " + ", ".join(sorted(wants_sand)))
    if notes:
        print(f"{len(notes)} note(s): land corners no edge variant shows "
              f"(a small notch), e.g. {notes[0]}")
    for label, items in (("warning", warnings), ("error", errors)):
        if not items:
            continue
        print(f"{len(items)} {label}(s):")
        for w in items[:60]:
            print("  " + w)
        if len(items) > 60:
            print(f"  ... and {len(items) - 60} more")
        if label == "warning" and strict:
            errors += warnings
    if errors:
        sys.exit(1)

    with open(out, "w", encoding="latin-1") as f:
        f.write(f"# {z.get('name', zid)} -- {W}x{H}.\n#\n"
                f"# BUILT by tools/romeart.py map build from {os.path.relpath(src)}.\n"
                f"# Do not edit this file: edit the source and rebuild.\n"
                f"# Check: tools/romeart.py map check {pack} {zid} <this file>\n")
        for y in range(H):
            f.write("".join(a2c[out_art[y][x]] for x in range(W)) + "\n")
    print(f"wrote {out} ({W}x{H})")


# ---- map check: objects on walkable ground, docks, and the hero's reach -----

def map_grid(pack, zid, path):
    g = load_game(pack)
    _, c2e = tile_codes(g)
    z = zone_of(g, zid)
    W, H = z["width"], z["height"]
    rows = read_rows(path)
    if len(rows) != H or any(len(r) != W for r in rows):
        map_die(f"{path}: not {W}x{H}")
    # a solid object on grass (a set piece) is no ground to stand on
    ter = [[(("blocked" if c2e[c].get("blocks_foot") and c2e[c]["terrain"] in ("grass", "desert")
              else c2e[c]["terrain"]) if c in c2e else map_die(f"unknown byte {c!r}"))
            for c in r] for r in rows]
    art = [[c2e[c]["art"] for c in r] for r in rows]
    return g, z, W, H, ter, art


def walkable(t):
    return t in ("grass", "desert")


def map_reach(W, H, ter, start, docks, open_rivers, arrivals=()):
    """Tiles the hero can stand on: foot from the spawn, then every landing on
    the water body of a dock the hero has reached (a boat sails only the water
    it was rented on), until nothing new is reached. A hero sailing in lands at
    an arrival, so that water body is sailed from the start. Also returns the
    water connected to the map's edge (the open sea)."""
    def ok(x, y):
        t = ter[y][x]
        return walkable(t) or (open_rivers and t == "river")

    def body(seed):
        out, q = {seed}, deque([seed])
        while q:
            x, y = q.popleft()
            for dx, dy in MAP_DIRS8.values():
                n = (x + dx, y + dy)
                if 0 <= n[0] < W and 0 <= n[1] < H and n not in out and ter[n[1]][n[0]] == "water":
                    out.add(n); q.append(n)
        return out

    sea = set()
    for y in range(H):
        for x in range(W):
            if (x in (0, W - 1) or y in (0, H - 1)) and ter[y][x] == "water" and (x, y) not in sea:
                sea |= body((x, y))
    seen, q, sailed, first = {start}, deque([start]), set(), set()
    for a in arrivals:
        if ter[a[1]][a[0]] == "water" and a not in first:
            first |= body(a)
    while True:
        while q:
            x, y = q.popleft()
            for dx, dy in MAP_DIRS8.values():
                n = (x + dx, y + dy)
                if 0 <= n[0] < W and 0 <= n[1] < H and n not in seen and ok(*n):
                    seen.add(n); q.append(n)
        waters, first = first, set()
        sailed |= waters
        for t, d in docks:
            if d in sea and d not in sailed and any(
                    (t[0] + dx, t[1] + dy) in seen or t in seen for dx, dy in MAP_DIRS8.values()):
                waters |= body(d)
                sailed |= body(d)
        if not waters:
            break
        for y in range(H):
            for x in range(W):
                if ok(x, y) and (x, y) not in seen and any(
                        (x + dx, y + dy) in waters for dx, dy in MAP_DIRS8.values()):
                    seen.add((x, y)); q.append((x, y))
    return seen, sea


def map_reach_table(pack, zid, path):
    """(problems, [(place, reached with guardians standing, beaten, rivers
    bridged, vistas played)], terrain counts, W, H) for `map check` and the
    reach baseline `map lint` compares against."""
    g, z, W, H, ter, art = map_grid(pack, zid, path)
    towns = [t for t in g["towns"] if t.get("zone") == zid]
    castles = [c for c in g["castles"] if c.get("zone") == zid]
    bad, points = [], {}

    def stand(x, y, what):
        if not (0 <= x < W and 0 <= y < H):
            bad.append(f"{what} ({x},{y}) is off the map")
        elif not walkable(ter[y][x]):
            bad.append(f"{what} ({x},{y}) stands on {ter[y][x]} ({art[y][x]})")

    for t in towns:
        stand(t["x"], t["y"], f"town {t['id']}")
        gt = t.get("gate") or {}
        if gt.get("x", -1) >= 0:
            stand(gt["x"], gt["y"], f"town {t['id']} gate")
        points["town " + t["id"]] = (t["x"], t["y"])
    for c in castles:
        stand(c["x"], c["y"], f"castle {c['id']}")
        stand(c["x"], c["y"] + 1, f"castle {c['id']} gate")
        points[f"castle {c['id']} ({c.get('name', '')})"] = (c["x"], c["y"] + 1)
    for k in ("signs", "chests", "wandering_armies"):
        for o in z.get(k, []):
            stand(o["x"], o["y"], f"{k[:-1]} {o.get('id', '')}")
    stand(z["magic_alcove"]["x"], z["magic_alcove"]["y"], "the Augur")
    stand(z["hero_spawn"]["x"], z["hero_spawn"]["y"], "the spawn")
    points["the Augur"] = (z["magic_alcove"]["x"], z["magic_alcove"]["y"])

    docks = []
    for t in towns:
        b = t.get("boat") or {}
        if b.get("x", -1) >= 0:
            if ter[b["y"]][b["x"]] != "water":
                bad.append(f"town {t['id']} dock ({b['x']},{b['y']}) is not water")
            docks.append(((t["x"], t["y"]), (b["x"], b["y"])))
    start = (z["hero_spawn"]["x"], z["hero_spawn"]["y"])
    # A static army (a guardian) holds its tile until beaten; a vista's tiles
    # (the Rubicon's bridge) exist only once it has played.
    held = [r[:] for r in ter]
    for a in z.get("wandering_armies", []):
        if a.get("static"):
            held[a["y"]][a["x"]] = "forest"
    arrivals = [(a["x"], a["y"]) for a in z.get("arrivals", {}).values()]
    fired = [r[:] for r in ter]
    for ev in z.get("events", []):
        for fx in ev.get("effects", []):
            code = g["tile_codes"].get(fx.get("tile"))
            if "tile" in fx and code:
                fired[fx["y"]][fx["x"]] = ("grass" if code.get("is_bridge") else
                                           "river" if code.get("terrain") == "river"
                                           else code.get("terrain", "grass"))
    guarded, _ = map_reach(W, H, held, start, docks, False, arrivals)
    shut, sea = map_reach(W, H, ter, start, docks, False, arrivals)
    open_, _ = map_reach(W, H, ter, start, docks, True, arrivals)
    played, _ = map_reach(W, H, fired, start, docks, False, arrivals)
    bad += [f"dock {d} is not on the open sea" for t, d in docks if d not in sea]
    for frm, a in z.get("arrivals", {}).items():
        x, y = a["x"], a["y"]
        if (x, y) not in sea:
            bad.append(f"arrival from {frm} ({x},{y}) is not on the open sea")
        elif not any(0 <= x + dx < W and 0 <= y + dy < H and walkable(ter[y + dy][x + dx])
                     for dx, dy in MAP_DIRS8.values()):
            bad.append(f"arrival from {frm} ({x},{y}) touches no land")
    missing = [n for n in z.get("neighbors", []) if n not in z.get("arrivals", {})]
    if z.get("arrivals") and missing:
        bad.append(f"no arrival from {', '.join(missing)}")

    def reached(seen, p):        # a town is entered from its tile or one beside it
        return p in seen or any((p[0] + dx, p[1] + dy) in seen for dx, dy in MAP_DIRS8.values())
    table = [(name, [reached(s, p) for s in (guarded, shut, open_, played)])
             for name, p in sorted(points.items(), key=lambda kv: (kv[1][1], kv[1][0]))]
    counts = {}
    for row in ter:
        for t in row:
            counts[t] = counts.get(t, 0) + 1
    return bad, table, counts, W, H


def map_check(pack, zid, path):
    bad, table, counts, W, H = map_reach_table(pack, zid, path)
    print(f"{zid}: {W}x{H}")
    print(f"  {'':44s} guardians    guardians    rivers        vistas")
    print(f"  {'':44s} standing     beaten       bridged       played")
    yn = lambda b: 'yes' if b else 'NO '
    for name, r in table:
        print(f"  {name:44s} {yn(r[0]):12s} {yn(r[1]):12s} {yn(r[2]):13s} {'yes' if r[3] else 'NO'}")
    print("  terrain: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    print(f"  walkable {sum(v for k, v in counts.items() if walkable(k))}")
    if bad:
        print(f"{len(bad)} problem(s):")
        for b in bad:
            print("  " + b)
        sys.exit(1)
    print("OK")


# ---- map lint: shapes the edge art draws badly, and the reach baseline -----
#
#   step    a one-cell stair step (two same-turned corners diagonally adjacent)
#   strand  a mass cell in no 2x2 block of its mass (strand, spur, lone cell)
#   notch   an edge that cannot show its different diagonal (a cut corner)
#   pair    two different masses side by side (edges fade only to grass)
#   coast   a mass beside the sea outside the zone's intended coasts
#   edge    a mass within two cells of the world's edge outside intended edges
#   dat     a finding in the built .dat's terrain the source does not have
#
# The masses are forest f, mountain ^ and farmland p/w (farmland never at the
# sea or the edge). art/maps/<zone>_lint_intended.json lists the coasts, edges
# and shapes kept on purpose; <zone>_lint_allow.json the findings still to
# clear (only unlisted ones fail, so the count can only fall);
# <zone>_reach.json the reach `map check` reports, which must not change.

LINT_MASS = {'f': 'forest', '^': 'mountain', 'p': 'field', 'w': 'field'}


def lint_findings(rows, intended=None):
    H, W = len(rows), len(rows[0])
    inside = lambda boxes, x, y: any(b[0] <= x <= b[2] and b[1] <= y <= b[3] for b in boxes)
    coast_ok = [e["box"] for e in (intended or {}).get("coast", [])]
    edge_ok = [e["box"] for e in (intended or {}).get("edge", [])]
    at = lambda x, y: rows[y][x] if 0 <= x < W and 0 <= y < H else None
    kind = lambda c: None if c is None else ('field-' + c if c in 'pw' else LINT_MASS.get(c))
    out, corners = [], {}
    for y in range(H):
        for x in range(W):
            k = kind(rows[y][x])
            if not k:
                continue
            same = lambda dx, dy: kind(at(x + dx, y + dy)) == k or at(x + dx, y + dy) is None
            open4 = {d for d, (dx, dy) in MAP_DIRS4.items() if not same(dx, dy)}
            if not any(all(kind(at(x + ox + i, y + oy + j)) == k for i in (0, 1) for j in (0, 1))
                       for ox in (-1, 0) for oy in (-1, 0)):
                out.append(("strand", x, y, f"{k} cell in no 2x2 block of {k}"))
            if open4:
                for d, (dx, dy) in MAP_DIRS8.items():
                    if len(d) == 2 and not same(dx, dy) and d[0] not in open4 and d[1] not in open4:
                        out.append(("notch", x, y, f"{k} edge on {sorted(open4)} hides the {d} corner"))
            for pair in (('n', 'e'), ('n', 'w'), ('s', 'e'), ('s', 'w')):
                if open4 == set(pair):
                    corners[(x, y)] = (k, pair)
            for d in ('e', 's'):                     # each pair once
                k2 = kind(at(x + MAP_DIRS4[d][0], y + MAP_DIRS4[d][1]))
                if k2 and k2 != k:
                    out.append(("pair", x, y, f"{k} beside {k2} to the {d}"))
            if any(at(x + dx, y + dy) == '~' for dx, dy in MAP_DIRS8.values()) and \
                    (k.startswith('field') or not inside(coast_ok, x, y)):
                out.append(("coast", x, y, f"{k} beside the sea"))
            if min(x, y, W - 1 - x, H - 1 - y) < 2 and \
                    (k.startswith('field') or not inside(edge_ok, x, y)):
                out.append(("edge", x, y, f"{k} within two cells of the world's edge"))
    shape_ok = [(e["box"], set(e["rules"])) for e in (intended or {}).get("shape", [])]
    for (x, y), (k, pair) in corners.items():
        # the next step of a staircase: the corner cell diagonally beyond,
        # along the outline, turned the same way
        ddx, ddy = (1 if 'e' in pair else -1), (1 if 'n' in pair else -1)
        for nx, ny in ((x + ddx, y + ddy), (x - ddx, y - ddy)):
            if corners.get((nx, ny)) == (k, pair) and (nx, ny) > (x, y):
                out.append(("step", x, y, f"{k} stair step with ({nx},{ny})"))
    return [f for f in out if not any(f[0] in rules and inside([b], f[1], f[2]) for b, rules in shape_ok)]


def lint_dat_rows(g, dat):
    """The built .dat as source characters for the masses: forest f, mountain
    ^, sea ~, farmland p / w by its art, everything else grass."""
    _, c2e = tile_codes(g)
    out = []
    for line in open(dat, "rb").read().decode("latin-1").split("\n"):
        line = line.rstrip("\r")
        if not line or line.startswith("#"):
            continue
        r = []
        for c in line:
            e = c2e.get(c, {})
            art, ter = e.get("art", ""), e.get("terrain", "grass")
            r.append('p' if art.startswith("fields_plough") else 'w' if art.startswith("fields_wheat")
                     else {'forest': 'f', 'mountain': '^', 'water': '~'}.get(ter, '.'))
        out.append(''.join(r))
    w = max(len(r) for r in out)
    return [r.ljust(w, '.') for r in out]


def map_lint(pack, zid, update_allow=False, update_reach=False, show_all=False, src=None, dat=None):
    g = load_game(pack)
    z = zone_of(g, zid)
    maps = os.path.join(ROOT, "art", "maps")
    dat = dat or os.path.join(pack, z["map"])
    allow_p, reach_p = (os.path.join(maps, f"{zid}_{k}.json") for k in ("lint_allow", "reach"))
    int_p = os.path.join(maps, f"{zid}_lint_intended.json")
    intended = load_json(int_p) if os.path.exists(int_p) else {}
    found = lint_findings(read_rows(src or os.path.join(maps, f"{zid}.txt")), intended)
    src_keys = {(r, x, y) for r, x, y, _ in found}
    found += [("dat", x, y, f"the built .dat has a {r} the source does not ({w})")
              for r, x, y, w in lint_findings(lint_dat_rows(g, dat), intended)
              if (r, x, y) not in src_keys]
    allow = load_json(allow_p) if os.path.exists(allow_p) else []
    allowed = {(e["rule"], e["x"], e["y"]) for e in allow}
    new = [f for f in found if (f[0], f[1], f[2]) not in allowed]
    gone = [e for e in allow if (e["rule"], e["x"], e["y"]) not in {(f[0], f[1], f[2]) for f in found}]
    if update_allow:
        old = {(e["rule"], e["x"], e["y"]): e.get("reason", "") for e in allow}
        allow = [{"rule": r, "x": x, "y": y, "what": w,
                  "reason": old.get((r, x, y), "baseline: still to clear")} for r, x, y, w in found]
        save_json(allow_p, allow)
        print(f"{zid}: recorded {len(allow)} findings still to clear in {os.path.relpath(allow_p, ROOT)}")
        new, gone = [], []

    reach = {" ".join(name.split()): ["yes" if b else "NO" for b in r]
             for name, r in map_reach_table(pack, zid, dat)[1]}
    if update_reach:
        save_json(reach_p, reach, sort_keys=True)
        print(f"{zid}: recorded the reach baseline ({len(reach)} places)")
    if not os.path.exists(reach_p):
        sys.exit(f"romeart: no reach baseline {os.path.relpath(reach_p, ROOT)} (record it with --update-reach)")
    base = load_json(reach_p)
    reach_bad = [f"{k}: {base.get(k)} -> {reach.get(k)}" for k in sorted(set(base) | set(reach))
                 if base.get(k) != reach.get(k)]

    by = {}
    for r, *_ in found:
        by[r] = by.get(r, 0) + 1
    print(f"{zid}: {len(found)} findings ({', '.join(f'{k} {v}' for k, v in sorted(by.items())) or 'none'}), "
          f"{len(found) - len(new)} still to clear, {len(new)} new; reach "
          f"{'unchanged' if not reach_bad else 'CHANGED'}")
    shown = found if show_all else new
    for r, x, y, w in shown[:80]:
        print(f"  {r:6s} ({x},{y}) {w}")
    if len(shown) > 80:
        print(f"  ... and {len(shown) - 80} more")
    for e in gone[:20]:
        print(f"  fixed  ({e['x']},{e['y']}) {e['rule']}: drop it from the allow list")
    for b in reach_bad:
        print(f"  REACH  {b}")
    return 1 if new or reach_bad else 0


# ---- map sanity: the map rules of GLORY-OF-ROME section 10 -----------------
#
# In order of how badly each breaks the game: (1) dimensions and every code
# known; (2) no town dock on a landlocked water body -- the boat trap: boats
# spawn only at a dock and a boat on enclosed water is unrecoverable, while a
# pond nothing launches into is harmless; (3) no OCCUPIED walkable pocket --
# empty islands are fine, and a pocket walled by a river (the bridge spell) or
# opened by a vista is a gate, not a trap; (4) room for the zone's object
# budget. Without a zone, (2) and (3) only warn.

def map_flood(cells, w, h, start, member):
    seen, q = {start}, deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in MAP_DIRS8.values():
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and (nx, ny) not in seen and member(cells[ny][nx]):
                seen.add((nx, ny)); q.append((nx, ny))
    return seen


def map_sanity(pack, map_path, declared=None, zone_id=None):
    g = load_game(pack)
    _, codes = tile_codes(g)
    objects, docks, vista_tiles = zone_objects(g, zone_id) if zone_id else ([], [], set())
    rows = read_rows(map_path, pad=True)
    if not rows:
        print("FAIL: map is empty")
        return 1
    h, w = len(rows), len(rows[0])
    fails = []
    if declared:
        dw, dh = (int(v) for v in declared.lower().split("x"))
        if (w, h) != (dw, dh):
            fails.append(f"dimensions are {w}x{h}, declaration says {dw}x{dh}")
    unknown = sorted({c for r in rows for c in r} - set(codes))
    if unknown:
        print("FAIL:", f"tile codes not in game.json: {' '.join(repr(c) for c in unknown)}")
        return 1
    terr = lambda c: codes[c].get("terrain")
    # a river blocks the foot as a wall does; a bridge over either is walkable
    walk = lambda c: codes[c].get("is_bridge") or not (codes[c].get("blocks_foot") or terr(c) == "river")
    is_water = lambda c: terr(c) == "water"
    near = lambda r, test: any(test(x + dx, y + dy) for (x, y) in r for dx in (-1, 0, 1) for dy in (-1, 0, 1))

    water = [(x, y) for y in range(h) for x in range(w) if is_water(rows[y][x])]
    sea = set()
    if not water:
        fails.append("no water at all -- the zone cannot be sailed to or from")
    else:
        edge = [(x, y) for (x, y) in water if x in (0, w - 1) or y in (0, h - 1)]
        if not edge:
            fails.append("no water touches the map edge -- there is no open sea")
        else:
            sea = map_flood(rows, w, h, edge[0], is_water)
            orphan = set(water) - sea
            if orphan:
                trapped = [d for d in docks if d in orphan]
                if trapped:
                    fails.append(f"town dock(s) on a landlocked water body (BOAT TRAP): {trapped}")
                else:
                    print(f"  note: {len(orphan)} enclosed water tile(s) "
                          f"(decorative ponds; harmless -- no dock launches into them)")
                if not zone_id:
                    print("  note: no zone id given, so dock placement was not checked against these")

    land = [(x, y) for y in range(h) for x in range(w) if walk(rows[y][x]) and not is_water(rows[y][x])]
    if not land:
        fails.append("no walkable land")
    else:
        unvisited, regions = set(land), []
        while unvisited:
            r = map_flood(rows, w, h, next(iter(unvisited)),
                          lambda c: walk(c) and not is_water(c)) & set(land)
            regions.append(r)
            unvisited -= r
        regions.sort(key=len, reverse=True)
        if len(regions) > 1:
            small = regions[1:]
            sizes = sorted((len(r) for r in small), reverse=True)
            print(f"  note: {len(small)} landmass(es) besides the mainland "
                  f"(sizes {sizes[:12]}{' ...' if len(sizes) > 12 else ''})")
            landlocked = []
            for r in small:
                on_it = [o[3] for o in objects if (o[0], o[1]) in r]
                if not on_it:
                    continue
                # Disembarking lands the hero on any coastal tile (REQ-243), so
                # a region touching the open sea needs no dock of its own.
                coastal = near(r, lambda x, y: (x, y) in sea)
                by_river = near(r, lambda x, y: 0 <= x < w and 0 <= y < h and terr(rows[y][x]) == "river")
                by_vista = near(r, lambda x, y: (x, y) in vista_tiles)
                if not coastal and by_vista:
                    print(f"  note: a {len(r)}-tile pocket with {len(on_it)} objective(s) is opened "
                          f"by a vista -- its tiles are laid when the vista plays")
                elif not coastal and by_river:
                    print(f"  note: a {len(r)}-tile pocket with {len(on_it)} objective(s) is walled "
                          f"by a river -- reached with the bridge spell or by flight")
                elif not coastal:
                    landlocked.append((len(r), on_it))
            fails += [f"objective(s) {what} sit in a {size}-tile INLAND POCKET "
                      f"with no coast -- reachable only by flight or gate" for size, what in landlocked]
            if not docks and any(near(r, lambda x, y: (x, y) in sea)
                                 for r in small if any((o[0], o[1]) in r for o in objects)):
                print("  note: island objectives exist and are coastal, but the zone declares no "
                      "town dock -- one town must be able to rent a boat or they cannot be reached")

    open_land = sum(1 for (x, y) in land if terr(rows[y][x]) in ("grass", "desert"))
    if open_land < 21 * 4:
        fails.append(f"only {open_land} open walkable tiles; section 10.7 needs "
                     f"room for >=21 chest placeholders plus castles and towns")
    counts = {}
    for r in rows:
        for c in r:
            counts[terr(c)] = counts.get(terr(c), 0) + 1
    total = w * h
    print(f"{os.path.basename(map_path)}: {w}x{h} = {total} tiles")
    for t in sorted(counts, key=lambda k: -counts[k]):
        print(f"  {t:<9}{counts[t]:>6}  {100.0*counts[t]/total:5.1f}%")
    print(f"  {'walkable':<9}{len(land):>6}  {100.0*len(land)/total:5.1f}%")
    if fails:
        print()
        for f in fails:
            print("FAIL:", f)
        return 1
    print("\nOK: one sea, no pockets, budget has room.")
    return 0


# ---- map render: a map to an image, flat or in the pack's own tiles --------
#
# Flat: one colour block per tile, close to the engine's minimap palette, for
# judging coastlines and shapes. --tiles: the real tile art at the pack's
# tile size, each code's cosmetic variant picked per cell as src/tilevar.c
# picks it, with the details, aprons and inner-corner fills src/map_render.c
# draws -- what the zone looks like in the game.

MAP_TERRAIN_RGB = {"grass": (72, 132, 48), "forest": (28, 78, 32), "mountain": (120, 108, 96),
                   "water": (36, 68, 140), "river": (58, 118, 196), "desert": (198, 176, 104)}
MAP_OBJECT_RGB = {"town": (240, 220, 80), "castle": (230, 90, 70), "chest": (250, 250, 250),
                  "sign": (170, 140, 90), "dwelling": (210, 120, 210), "army": (255, 40, 40)}
APRON_SEED = 0xA960       # src/map_render.c APRON_SEED: which apron a cell draws
DETAIL_SEED = 0xD7A1      # src/map_render.c DETAIL_SEED: which open cells draw a detail


def tilevar_pick(seed, x, y, n):
    """src/tilevar.c tilevar_pick, in 32-bit unsigned arithmetic."""
    if n <= 1:
        return 0
    M = 0xFFFFFFFF
    h = (seed ^ 0x9E3779B9) & M
    h ^= (x * 0x85EBCA6B) & M
    h ^= h >> 13
    h ^= (y * 0xC2B2AE35) & M
    h ^= h >> 16
    h = (h * 0x27D4EB2F) & M
    h ^= h >> 15
    return h % n


def render_flat(rows, w, h, codes, scale):
    img = Image.new("RGB", (w * scale, h * scale), (0, 0, 0))
    px = img.load()
    for y in range(h):
        for x in range(w):
            c = codes.get(rows[y][x], {})
            terr = c.get("terrain", "grass")
            rgb = MAP_TERRAIN_RGB.get(terr, (255, 0, 255))
            if c.get("blocks_foot") and terr != "water":
                rgb = tuple(int(v * 0.82) for v in rgb)       # walls a shade darker
            for dy in range(scale):
                for dx in range(scale):
                    px[x * scale + dx, y * scale + dy] = rgb
    return img


def render_tiles(rows, w, h, codes, pack_dir, tile_set="", cell=(48, 34), set_arts=None,
                 seed=0, box=None):
    TW, TH = cell
    x0, y0, x1, y1 = box or (0, 0, w - 1, h - 1)
    img = Image.new("RGB", ((x1 - x0 + 1) * TW, (y1 - y0 + 1) * TH), (0, 0, 0))
    cache = {}
    var = {}           # art stem -> its variants, the first code with any winning (tilevar_init)
    for v in codes.values():
        if v.get("variants") and v.get("art") and v["art"] not in var:
            var[v["art"]] = v["variants"]

    def vary(art, x, y):
        names = var.get(art)
        if not names:
            return art
        k = tilevar_pick(seed, x, y, len(names) + 1)     # 0 = the base art
        return art if k == 0 else names[k - 1]

    def tile(name, size=None, warn=False):
        """A tile by art stem, as src/tile_cache.c resolves it: the zone's set
        (only the names in its tile_set_arts, when it lists any), else the
        master set; scaled to `size` once."""
        if name not in cache:
            own = tile_set and (not set_arts or name in set_arts)
            p = os.path.join(pack_dir, "art", "tiles", tile_set if own else "", name + ".png")
            cache[name] = Image.open(p).convert("RGBA") if os.path.exists(p) else None
            if cache[name] is None and warn:
                print(f"  warn: no art for tile '{name}' (looked for {p})")
        t = cache[name]
        if t is not None and size and t.width != size[0]:
            t = cache[name] = t.resize(size, Image.NEAREST)
        return t

    def ter(x, y):
        return codes.get(rows[y][x], {}).get("terrain", "grass") if 0 <= x < w and 0 <= y < h else None

    cells = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
    at = lambda x, y, dx=0, dy=0: ((x - x0 + dx) * TW, (y - y0 + dy) * TH)
    for x, y in cells:
        c = codes.get(rows[y][x], {})
        if not c.get("art"):
            continue
        if c.get("ground"):                      # a landmark over its own ground
            ground = vary(c["ground"], x, y)
            own = tile_set and (not set_arts or ground in set_arts)
            gp = os.path.join(pack_dir, "art", "tiles", tile_set if own else "", ground + ".png")
            if os.path.exists(gp):
                gt = Image.open(gp).convert("RGBA")
                img.paste(gt, at(x, y), gt)
        t = tile(vary(c["art"], x, y), warn=True)
        if t is not None:
            img.paste(t, at(x, y), t)

    def open_ground(x, y):
        if not (0 <= x < w and 0 <= y < h):
            return False
        c = codes.get(rows[y][x], {})
        return c.get("terrain") in ("grass", "desert") and c.get("art", "").startswith(("grass", "desert"))
    # small detail: one plain grass or sand cell in twelve with no wood, range
    # or sea beside it (draw_details)
    for x, y in cells:
        v = tilevar_pick(DETAIL_SEED, x, y, 48)
        if 1 <= v <= 4 and open_ground(x, y) and all(
                ter(x + dx, y + dy) not in ("forest", "mountain", "water")
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            t = tile(f"detail_{v}", (TW, TH))
            if t is not None:
                img.paste(t, at(x, y), t)
    # aprons: a plain grass or sand cell beside a wood or range gets one of that
    # side's three aprons, or none (draw_aprons)
    for x, y in cells:
        art = codes.get(rows[y][x], {}).get("art", "")
        if ter(x, y) not in ("grass", "desert") or not art.startswith(("grass", "desert")):
            continue
        for k, (side, (dx, dy)) in enumerate((("n", (0, -1)), ("e", (1, 0)), ("s", (0, 1)), ("w", (-1, 0)))):
            a = ter(x + dx, y + dy)
            v = tilevar_pick(APRON_SEED + k, x, y, 5)     # 0 and 4: none
            if a in ("forest", "mountain") and 1 <= v <= 3:
                t = tile(f"{a}_apron_{side}_{v}", (TW * 3, TH * 3))
                if t is not None:
                    img.paste(t, at(x, y, -1, -1), t)
    # inner-corner fills: a grass or sand cell with a wood or range on two
    # adjacent sides gets that terrain's fill in the corner between them
    for x, y in cells:
        here = ter(x, y)
        if here not in ("grass", "desert"):
            continue
        for corner, (dx, dy) in (("ne", (1, -1)), ("nw", (-1, -1)), ("se", (1, 1)), ("sw", (-1, 1))):
            a, b = ter(x + dx, y), ter(x, y + dy)
            if a == b and a in ("forest", "mountain"):
                t = tile(f"{a}{'_sand' if here == 'desert' else ''}_fill_{corner}", (TW * 3, TH * 3))
                if t is not None:
                    img.paste(t, at(x, y, -1, -1), t)
    # an apron or fill may lean onto its neighbours: every landmark and set
    # piece (a code over its own ground) is drawn again on top, as the shell does
    for x, y in cells:
        c = codes.get(rows[y][x], {})
        if c.get("ground") and cache.get(vary(c["art"], x, y)) is not None:
            t = cache[vary(c["art"], x, y)]
            img.paste(t, at(x, y), t)
    return img


def map_render(a):
    g = load_game(a.pack)
    _, codes = tile_codes(g)
    rows = read_rows(a.map, pad=True)
    w, h = len(rows[0]), len(rows)
    box = tuple(int(v) for v in a.crop.split(",")) if a.crop else (0, 0, w - 1, h - 1)
    if len(box) != 4 or not (0 <= box[0] <= box[2] < w and 0 <= box[1] <= box[3] < h):
        sys.exit(f"romeart map: --crop wants X0,Y0,X1,Y1 inside {w}x{h}")
    if a.tiles:
        z = next((z for z in g.get("zones", []) if a.zone and z.get("id") == a.zone), {})
        r = g.get("render", {})
        cell = (int(r.get("tile_w", 48)), int(r.get("tile_h", 34)))
        img = render_tiles(rows, w, h, codes, a.pack, z.get("tile_set", ""), cell,
                           set(z.get("tile_set_arts", [])) or None, a.seed, box)
    else:
        cell = (a.scale, a.scale)
        img = render_flat(rows, w, h, codes, a.scale).crop(
            (box[0] * a.scale, box[1] * a.scale, (box[2] + 1) * a.scale, (box[3] + 1) * a.scale))
    ox, oy = box[0], box[1]
    if a.grid and cell[0] >= 6:
        d = ImageDraw.Draw(img)
        for x in range(0, w + 1, 10):
            d.line([((x - ox) * cell[0], 0), ((x - ox) * cell[0], img.height)], fill=(255, 255, 255), width=1)
        for y in range(0, h + 1, 10):
            d.line([(0, (y - oy) * cell[1]), (img.width, (y - oy) * cell[1])], fill=(255, 255, 255), width=1)
    if a.zone and not a.no_objects:
        d = ImageDraw.Draw(img)
        objs = zone_objects(g, a.zone)[0]
        r = max(2, cell[0] // 3)
        for (x, y, kind, _) in objs:
            if box[0] <= x <= box[2] and box[1] <= y <= box[3]:
                cx, cy = (x - ox) * cell[0] + cell[0] // 2, (y - oy) * cell[1] + cell[1] // 2
                d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=MAP_OBJECT_RGB.get(kind, (255, 255, 255)),
                          outline=(0, 0, 0))
        print(f"overlaid {len(objs)} objects for zone {a.zone}")
    if a.shrink > 1:
        img = img.resize((img.width // a.shrink, img.height // a.shrink), Image.LANCZOS)
    img.save(a.out)
    print(f"wrote {a.out}  {img.width}x{img.height}  ({w}x{h} tiles, {'art' if a.tiles else 'flat'})")


# ---- map place: scatter a zone's chests and armies inside region boxes -----

def map_place(pack, zid, path, regions_path, add=False):
    """regions.json: {"seed": N, "spacing": r, "fixed": {"<id>": [x, y]},
    "regions": [{"name", "box": [x0,y0,x1,y1], "chests": n, "armies": n}, ...]}

    A static army (a guardian) and a "fixed" chest keep what they are and go
    where "fixed" puts them; every other chest and army is re-scattered, from
    the fixed seed, on open grass or sand. With add, every chest and army
    stays where it is and a region's counts are totals: only what it still
    lacks is placed, numbered on from the zone's highest id. Towns, castles,
    signs, dwellings, events and the tiles an event changes are kept clear."""
    g, z, W, H, ter, art = map_grid(pack, zid, path)
    spec = load_json(regions_path)
    rng = random.Random(spec["seed"])
    spacing = spec.get("spacing", 2)
    taken = set()

    def mark(x, y, r):
        taken.update((x + dx, y + dy) for dy in range(-r, r + 1) for dx in range(-r, r + 1))

    for t in (t for t in g["towns"] if t.get("zone") == zid):
        mark(t["x"], t["y"], 1)
        gt = t.get("gate") or {}
        if gt.get("x", -1) >= 0:
            mark(gt["x"], gt["y"], 1)
    for c in (c for c in g["castles"] if c.get("zone") == zid):
        mark(c["x"], c["y"], 1); mark(c["x"], c["y"] + 1, 1)
    for o in z.get("signs", []) + z.get("dwellings", []):
        mark(o["x"], o["y"], 1)
    for ev in z.get("events", []):
        mark(ev["x"], ev["y"], 1)
        for fx in ev.get("effects", []):
            if "x" in fx:
                mark(fx["x"], fx["y"], 1)

    def pinned(objs, test, what):
        out = []
        for o in objs:
            if test(o):
                if o["id"] not in spec.get("fixed", {}):
                    map_die(f"{what} {o['id']} needs a place in \"fixed\"")
                o = dict(o)
                o["x"], o["y"] = spec["fixed"][o["id"]]
                out.append(o)
                mark(o["x"], o["y"], 1)
        return out
    fixed_chests = pinned(z.get("chests", []), lambda c: c.get("fixed"), "fixed chest")
    fixed = pinned(z.get("wandering_armies", []), lambda a: a.get("static"), "static army")
    for k in ("magic_alcove", "hero_spawn"):
        mark(z[k]["x"], z[k]["y"], 1)

    inbox = lambda box, x, y: box[0] <= x <= box[2] and box[1] <= y <= box[3]
    # With add, what each region already holds counts against its total. An
    # object in several overlapping boxes goes to whichever is furthest short.
    have = [{"chests": 0, "armies": 0} for _ in spec["regions"]]
    if add:
        for kind, objs in (("chests", [c for c in z.get("chests", []) if not c.get("fixed")]),
                           ("armies", [a for a in z.get("wandering_armies", []) if not a.get("static")])):
            shared = []
            for o in objs:
                mark(o["x"], o["y"], spacing)
                hits = [i for i, reg in enumerate(spec["regions"]) if inbox(reg["box"], o["x"], o["y"])]
                if len(hits) == 1:
                    have[hits[0]][kind] += 1
                elif hits:
                    shared.append(hits)
            for hits in shared:
                i = max(hits, key=lambda i: (spec["regions"][i].get(kind, 0) - have[i][kind], -i))
                have[i][kind] += 1

    free = lambda x, y: 0 <= x < W and 0 <= y < H and (x, y) not in taken and art[y][x] in MAP_OPEN_ARTS
    found = {"chests": [], "armies": []}
    spill = {"chests": 0, "armies": 0}
    # The zone wants what its regions add up to: what a full region spilled
    # before counts where it landed, so a second run adds nothing.
    left = {k: sum(r.get(k, 0) for r in spec["regions"]) - sum(h[k] for h in have) for k in found}
    for reg, held in zip(spec["regions"], have):
        x0, y0, x1, y1 = reg["box"]
        cells = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
        rng.shuffle(cells)
        for kind in ("chests", "armies"):
            n = max(0, min(reg.get(kind, 0) - held[kind], left[kind]))
            left[kind] -= n
            got = 0
            for x, y in cells:
                if got == n:
                    break
                if free(x, y):
                    found[kind].append((x, y)); mark(x, y, spacing); got += 1
            if got < n:
                if not add:
                    map_die(f"region {reg['name']}: room for {got} of {n} {kind}")
                print(f"  region {reg['name']}: room for {got} of {n} {kind}; "
                      f"the rest go to the zone's other regions")
                spill[kind] += n - got
    if any(spill.values()):        # anywhere in the zone's regions there is room
        cells = sorted({(x, y) for reg in spec["regions"]
                        for y in range(reg["box"][1], reg["box"][3] + 1)
                        for x in range(reg["box"][0], reg["box"][2] + 1)})
        rng.shuffle(cells)
        for kind in ("chests", "armies"):
            for x, y in cells:
                if spill[kind] == 0:
                    break
                if free(x, y):
                    found[kind].append((x, y)); mark(x, y, spacing); spill[kind] -= 1
            if spill[kind]:
                map_die(f"no room in any region for {spill[kind]} more {kind}")

    def next_num(objs, prefix):
        nums = [int(o["id"][len(prefix):]) for o in objs
                if o.get("id", "").startswith(prefix) and o["id"][len(prefix):].isdigit()]
        return max(nums) + 1 if nums else 0
    chests, armies = found["chests"], found["armies"]
    if add:
        c0 = max(1, next_num(z.get("chests", []), "chest_"))
        a0 = next_num(z.get("wandering_armies", []), "wandering_army_")
        return ([{"id": f"chest_{c0 + i}", "x": x, "y": y} for i, (x, y) in enumerate(chests)],
                [{"x": x, "y": y, "id": f"wandering_army_{a0 + i:03d}"} for i, (x, y) in enumerate(armies)])
    return ([{"id": f"chest_{i + 1}", "x": x, "y": y} for i, (x, y) in enumerate(chests)] + fixed_chests,
            fixed + [{"x": x, "y": y, "id": f"wandering_army_{i:03d}"} for i, (x, y) in enumerate(armies)])


def map_edit(pack, zid, edit, force=False):
    """Apply edit(rows) -> rows to art/maps/<zone>.txt only if the result still
    builds under --strict, lints clean and keeps the recorded reach; then write
    the source and rebuild the shipped .dat. --force writes it regardless."""
    src = os.path.join(ROOT, "art", "maps", f"{zid}.txt")
    lines = open(src, encoding="latin-1").read().split("\n")
    body = [i for i, l in enumerate(lines) if l.strip() and not l.startswith("#")]
    rows = edit([lines[i] for i in body])
    for i, r in zip(body, rows):
        lines[i] = r
    tmp = tempfile.mkdtemp()
    cand, dat = os.path.join(tmp, f"{zid}.txt"), os.path.join(tmp, f"{zid}.dat")
    open(cand, "w", encoding="latin-1").write("\n".join(lines))
    ok = True
    try:
        with open(os.devnull, "w") as null:
            out, sys.stdout = sys.stdout, null
            try:
                map_build(pack, zid, cand, dat, strict=True)
            finally:
                sys.stdout = out
        ok = map_lint(pack, zid, src=cand, dat=dat) == 0
    except SystemExit:
        ok = False
        print(f"{zid}: the edit does not build under --strict (run map build on it to see why)")
    if not ok and not force:
        shutil.rmtree(tmp)
        sys.exit(f"romeart: {zid}: edit refused, nothing written (--force writes it anyway)")
    shutil.copyfile(cand, src)
    shutil.rmtree(tmp)
    map_build(pack, zid, src, os.path.join(pack, zone_of(load_game(pack), zid)["map"]), strict=not force)


def map_setup(ap):
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("build", help="bake a zone's source into its .dat")
    p.add_argument("pack"); p.add_argument("zone"); p.add_argument("src"); p.add_argument("out")
    p.add_argument("--strict", action="store_true", help="every warning is an error")
    p = sub.add_parser("check", help="objects on walkable ground, docks, the hero's reach")
    p.add_argument("pack"); p.add_argument("zone"); p.add_argument("map")
    p = sub.add_parser("lint", help="shapes the edge art draws badly, and the reach baseline")
    p.add_argument("pack"); p.add_argument("zone")
    p.add_argument("--update-allow", action="store_true", help="record every finding as still to clear")
    p.add_argument("--update-reach", action="store_true", help="record the current reach as the baseline")
    p.add_argument("--all", action="store_true", help="print every finding, allowed or not")
    p = sub.add_parser("sanity", help="one sea, no occupied pockets, the object budget")
    p.add_argument("pack"); p.add_argument("map"); p.add_argument("size", nargs="?", help="WxH")
    p.add_argument("zone", nargs="?")
    p = sub.add_parser("render", help="a map to an image, flat or in the pack's tiles")
    p.add_argument("pack"); p.add_argument("map"); p.add_argument("out")
    p.add_argument("--scale", type=int, default=8, help="flat: pixels per tile")
    p.add_argument("--zone", help="overlay this zone's objects and use its tile set")
    p.add_argument("--grid", action="store_true", help="a line every ten tiles")
    p.add_argument("--tiles", action="store_true", help="the real tile art")
    p.add_argument("--seed", type=int, default=0, help="game seed for the tile variants")
    p.add_argument("--crop", help="X0,Y0,X1,Y1: keep only that tile box")
    p.add_argument("--shrink", type=int, default=1, help="scale the picture down N times")
    p.add_argument("--no-objects", action="store_true", help="no object markers")
    p = sub.add_parser("place", help="scatter chests and armies inside region boxes")
    p.add_argument("pack"); p.add_argument("zone"); p.add_argument("map"); p.add_argument("regions")
    p.add_argument("--add", action="store_true", help="keep what stands; place only what is lacking")
    p.add_argument("--write", action="store_true", help="write them into game.json")
    p = sub.add_parser("zones", help="each zone's id, map file and size, one per line")
    p.add_argument("pack")
    p = sub.add_parser("set", help="set source cells X,Y=C (gated: strict build, lint, reach)")
    p.add_argument("zone"); p.add_argument("cells", nargs="+", metavar="X,Y=C")
    p.add_argument("--force", action="store_true", help="write even if a gate fails")
    p = sub.add_parser("block", help="replace source rows from Y down with a text file's rows (gated)")
    p.add_argument("zone"); p.add_argument("y", type=int); p.add_argument("rows", help="a text file of rows")
    p.add_argument("--x", type=int, default=0, help="the column the rows start at")
    p.add_argument("--force", action="store_true", help="write even if a gate fails")


@command("map", "the zone maps: build | check | lint | sanity | render | place | zones | set | block",
         setup=map_setup)
def cmd_map(a):
    if a.cmd == "zones":
        for z in load_game(a.pack)["zones"]:
            print(z["id"], z["map"], f'{z["width"]}x{z["height"]}')
    elif a.cmd == "build":
        map_build(a.pack, a.zone, a.src, a.out, a.strict)
    elif a.cmd == "check":
        map_check(a.pack, a.zone, a.map)
    elif a.cmd == "lint":
        return map_lint(a.pack, a.zone, a.update_allow, a.update_reach, a.all)
    elif a.cmd == "sanity":
        return map_sanity(a.pack, a.map, a.size, a.zone)
    elif a.cmd == "render":
        map_render(a)
    elif a.cmd == "place":
        chests, armies = map_place(a.pack, a.zone, a.map, a.regions, a.add)
        verb = "added" if a.add else "placed"
        if a.write:
            text = open(os.path.join(a.pack, "game.json"), encoding="utf-8").read()
            for key, objs in (("wandering_armies", armies), ("chests", chests)):
                text = gj_set_zone_array(text, a.zone, key, objs, append=a.add)
            gj_write(a.pack, text)
            print(f"{verb} {len(chests)} chests and {len(armies)} armies; wrote game.json")
        else:
            print(f"{verb} {len(chests)} chests and {len(armies)} armies (not written)")
            print("  " + json.dumps(chests))
            print("  " + json.dumps(armies))
    elif a.cmd == "set":
        def edit(rows):
            for spec in a.cells:
                xy, c = spec.split("=", 1)
                x, y = (int(v) for v in xy.split(","))
                if len(c) != 1 or not (0 <= y < len(rows) and 0 <= x < len(rows[y])):
                    sys.exit(f"romeart: bad cell {spec}")
                rows[y] = rows[y][:x] + c + rows[y][x + 1:]
            return rows
        map_edit(PACK, a.zone, edit, a.force)
    elif a.cmd == "block":
        new = [l.rstrip("\n") for l in open(a.rows, encoding="latin-1") if l.strip()]

        def edit(rows):
            for k, r in enumerate(new):
                y = a.y + k
                if y >= len(rows) or a.x + len(r) > len(rows[y]):
                    sys.exit(f"romeart: the block runs off the map at row {y}")
                rows[y] = rows[y][:a.x] + r + rows[y][a.x + len(r):]
            return rows
        map_edit(PACK, a.zone, edit, a.force)
# ==========================================================================
# Screens: the siege and field grids, the combat set, the splash, the title
# ==========================================================================

@command("siegeslice", "cut a picture into the combat grid: --grid (the siege, 6x6) or --field (the open field, 6x5)",
         A("src"), A("out"), A("--grid", action="store_true", help="every cell of a 6x6 picture, untouched"),
         A("--field", metavar="PREFIX", help="<prefix>_<x>_<y>: the largest centred 6:5 rectangle of content at 576x480"))
def cmd_siegeslice(a):
    """--grid: the whole picture is the siege board and its back band, six
    equal cells a side (sprites.ui.siege_grid), each written as cell_<x>_<y>
    at the picture's own cell size; the shell scales them. --field: the
    largest 6:5 rectangle of content centred in the picture (a meadow painted
    on white keeps the white out), scaled (Lanczos) to the 576x480 board and
    cut into its 6x5 cells."""
    os.makedirs(a.out, exist_ok=True)
    im = rgba(a.src)
    if a.grid:
        if im.width % 6 or im.height % 6:
            sys.exit(f"romeart: {a.src} is {im.size}, not six equal cells a side")
        cw, ch = im.width // 6, im.height // 6
        for y in range(6):
            for x in range(6):
                im.crop((x * cw, y * ch, x * cw + cw, y * ch + ch)).save(os.path.join(a.out, f"cell_{x}_{y}.png"))
        print(f"36 cells of {cw}x{ch} in {a.out}")
        return
    if not a.field:
        sys.exit("romeart: siegeslice wants --grid or --field <prefix>")
    W, H, T = 6, 5, TILE
    bw, bh = W * T, H * T
    px = im.load()

    def content(x, y):
        r, g, b, al = px[x, y]
        return al > 16 and not (r > 235 and g > 235 and b > 235)
    cx, cy = im.width // 2, im.height // 2

    def edge_ok(rw, rh):      # every pixel on the rectangle's edge is content
        x0, y0 = cx - rw // 2, cy - rh // 2
        x1, y1 = x0 + rw - 1, y0 + rh - 1
        if x0 < 0 or y0 < 0 or x1 >= im.width or y1 >= im.height:
            return False
        return all(content(x, y0) and content(x, y1) for x in range(x0, x1 + 1, 4)) and \
            all(content(x0, y) and content(x1, y) for y in range(y0, y1 + 1, 4))
    rw = min(im.width, im.height * W // H)
    rh = rw * H // W
    while rw > bw and not edge_ok(rw, rh):
        rw -= 6; rh = rw * H // W
    x0, y0 = cx - rw // 2, cy - rh // 2
    board = im.crop((x0, y0, x0 + rw, y0 + rh)).resize((bw, bh), Image.LANCZOS)
    for y in range(H):
        for x in range(W):
            board.crop((x * T, y * T, x * T + T, y * T + T)).save(os.path.join(a.out, f"{a.field}_{x}_{y}.png"))
    print(f"{W * H} cells of {T}x{T}: the centre {rw}x{rh} of {im.size[0]}x{im.size[1]} scaled to {bw}x{bh}, in {a.out}")


@command("fieldcalm", "calm a field painting (mask its clutter, refill from its own texture) and cut its cells",
         A("painting"), A("out"), A("prefix"), A("--level", default="medium", choices=("light", "medium", "strong")),
         A("--fill", default="patchmatch", choices=("patchmatch", "lama")),
         A("--mask", help="a hand-edited mask instead (white = repaint)"), A("--dilate", type=int, default=15))
def cmd_fieldcalm(a):
    """light repaints grey boulders and dark clumps; medium also brown earth;
    strong also mid-dark leaf clusters and pale moss. The mask (colour and
    size thresholds) is written to <out>/mask.png for hand editing. Fill:
    G'MIC inpaint_matchpatch, or LaMa on CPU at 1024 px. NOT deterministic, so
    the calmed painting that shipped is committed beside the source
    (art/fields/italia_calm.png). Needs gmic, opencv, numpy; lama: torch,
    simple-lama-inpainting."""
    cv2, np = need("cv2", "fieldcalm"), need("numpy", "fieldcalm")
    os.makedirs(a.out + "/cells", exist_ok=True)
    src = cv2.imread(a.painting, cv2.IMREAD_UNCHANGED)
    if src.shape[2] == 3:
        src = np.dstack([src, np.full(src.shape[:2], 255, np.uint8)])
    bgr, alpha = src[:, :, :3], src[:, :, 3]
    K = lambda n: cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (n, n))

    def blobs(mask, min_area):
        n, lab, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
        keep = np.zeros_like(mask)
        for i in range(1, n):
            if stats[i, cv2.CC_STAT_AREA] >= min_area:
                keep |= (lab == i)
        return keep
    if a.mask:
        mask = cv2.imread(a.mask, cv2.IMREAD_GRAYSCALE)
    else:
        hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
        H, S, V = [hsv[:, :, i].astype(int) for i in range(3)]
        content = (alpha > 16) & ~((bgr[:, :, 0] > 235) & (bgr[:, :, 1] > 235) & (bgr[:, :, 2] > 235))
        m = blobs(content & (S < 0.20 * 255) & (V > 0.42 * 255), 120) | blobs(content & (V < 0.40 * 255), 120)
        if a.level in ("medium", "strong"):
            m |= blobs(content & (H >= 8) & (H <= 30) & (S > 0.25 * 255) & (V > 0.35 * 255) & (V < 0.75 * 255), 400)
        if a.level == "strong":
            leaf = cv2.morphologyEx((content & (V < 0.50 * 255) & (S > 0.30 * 255)).astype(np.uint8), cv2.MORPH_OPEN, K(7)).astype(bool)
            moss = cv2.morphologyEx((content & (H >= 28) & (H <= 42) & (S > 0.35 * 255) & (V > 0.60 * 255)).astype(np.uint8), cv2.MORPH_OPEN, K(7)).astype(bool)
            m |= blobs(leaf, 250) | blobs(moss, 400)
        mask = cv2.dilate(m.astype(np.uint8) * 255, K(a.dilate)) & (content.astype(np.uint8) * 255)
    cv2.imwrite(a.out + "/mask.png", mask)
    print(f"mask: {(mask > 0).sum()} px repainted ({(mask > 0).sum() / max(1, (alpha > 16).sum()) * 100:.1f}% of the painting)")
    if a.fill == "patchmatch":
        rgb_p = a.out + "/inpainted_rgb.png"
        r = subprocess.run(["gmic", a.painting, "-channels[0]", "0,2", a.out + "/mask.png", "-inpaint_matchpatch[0]",
                            "[1],0,15,10,7,1", "-keep[0]", "-o", rgb_p], capture_output=True, text=True)
        if r.returncode:
            sys.exit("gmic failed: " + r.stderr[-400:])
        out = np.dstack([cv2.imread(rgb_p), alpha])
    else:
        lama = need("simple_lama_inpainting", "fieldcalm --fill lama")
        S = 1024
        rgb_s = cv2.resize(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), (S, S), interpolation=cv2.INTER_AREA)
        m_s = cv2.resize(mask, (S, S), interpolation=cv2.INTER_NEAREST)
        res = np.array(lama.SimpleLama()(Image.fromarray(rgb_s), Image.fromarray(m_s)).convert("RGB"))[:S, :S]
        out = np.dstack([cv2.cvtColor(res, cv2.COLOR_RGB2BGR), cv2.resize(alpha, (S, S), interpolation=cv2.INTER_AREA)])
    cv2.imwrite(a.out + "/inpainted.png", out)
    cmd_siegeslice(argparse.Namespace(src=a.out + "/inpainted.png", out=a.out + "/cells", grid=False, field=a.prefix))


def field_grade(src, out, window, flip=None, hue=0, sat=1, val=1, tint=None):
    """A window of a field painting, flipped, hue-shifted (OpenCV's 0..180),
    saturation and value scaled, and `tint` (R,G,B,W) blended over it."""
    cv2, np = need("cv2", "fieldgrade"), need("numpy", "fieldgrade")
    x, y, w, h = window
    im = cv2.imread(src, cv2.IMREAD_UNCHANGED)
    if im is None:
        sys.exit(f"romeart: cannot read {src}")
    crop = im[y:y + h, x:x + w, :3].copy()
    crop = {"h": crop[:, ::-1], "v": crop[::-1, :], "180": crop[::-1, ::-1], None: crop}[flip]
    hsv = cv2.cvtColor(np.ascontiguousarray(crop), cv2.COLOR_BGR2HSV).astype(np.float32)
    hsv[:, :, 0] = (hsv[:, :, 0] + hue) % 180
    hsv[:, :, 1] = np.clip(hsv[:, :, 1] * sat, 0, 255)
    hsv[:, :, 2] = np.clip(hsv[:, :, 2] * val, 0, 255)
    res = cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR).astype(np.float32)
    if tint:
        r, g, b, wt = tint
        res = res * (1 - wt) + np.array([b, g, r], np.float32) * wt
    res = np.clip(res, 0, 255).astype(np.uint8)
    cv2.imwrite(out, np.dstack([res, np.full((h, w), 255, np.uint8)]))
    return res


@command("fieldgrade", "derive a field painting from another: a window, a flip, a colour grade",
         A("src"), A("out"), A("--window", required=True, help="x,y,w,h (6:5, so siegeslice --field keeps it all)"),
         A("--flip", choices=("h", "v", "180")), A("--hue", type=float, default=0, help="OpenCV's 0..180 scale"),
         A("--sat", type=float, default=1), A("--val", type=float, default=1), A("--tint", help="R,G,B,W"))
def cmd_fieldgrade(a):
    window = [int(v) for v in a.window.split(",")]
    res = field_grade(a.src, a.out, window, a.flip, a.hue, a.sat, a.val,
                      [float(v) for v in a.tint.split(",")] if a.tint else None)
    m = res.reshape(-1, 3).mean(axis=0)
    print(f"{a.out}: {window[2]}x{window[3]} from ({window[0]},{window[1]}) flip {a.flip or 'none'}; "
          f"mean RGB {int(m[2])},{int(m[1])},{int(m[0])}")


# The three derived fields (#64): windows of the calmed Italia painting whose
# whole edge is content, taken off centre, flipped, graded for each land --
# Galliae a deeper cooler green, Africa dry grass with tan, Oriens a
# sun-bleached plateau. Deterministic: these reproduce art/fields/<zone>.png.
FIELD_GRADES = {"galliae": dict(window=(208, 268, 696, 580), flip="h", hue=6, sat=1.20, val=0.86),
                "africa": dict(window=(352, 448, 696, 580), flip="v", hue=-12, sat=0.90, val=1.05,
                               tint=(214, 190, 138, 0.12)),
                "oriens": dict(window=(340, 248, 696, 580), flip="180", hue=-5, sat=0.62, val=1.02,
                               tint=(190, 178, 158, 0.10))}


def fields_build(pack, paintings="art/fields"):
    """The three graded paintings from italia_calm.png, and every zone's 30
    field cells into the pack (Italia's from the calmed painting itself)."""
    for zone, g in FIELD_GRADES.items():
        field_grade(os.path.join(paintings, "italia_calm.png"), os.path.join(paintings, f"{zone}.png"), **g)
    for zone in ZONES:
        src = os.path.join(paintings, "italia_calm.png" if zone == "italia" else f"{zone}.png")
        cmd_siegeslice(argparse.Namespace(src=src, out=os.path.join(pack, "art", "combat", "field"),
                                          grid=False, field=zone))


@command("fields", "the field paintings graded from the calmed Italia one, and every zone's field cells",
         A("pack", nargs="?", default=PACK))
def cmd_fields(a):
    fields_build(a.pack)


# ---- the combat set: the spike burst and the cursors -----------------------
#
# Redrawn at the 96 px cell from the reference pack's 48x34 originals: each
# original pixel classified into a material, the material map scaled to the
# cell, and every material re-rendered at pixel scale; the boundaries between
# colours dithered so nothing reads as 2x blocks. (The wall pieces this once
# drew are gone: the Glory of Rome's siege is the generated grid.)

COMBAT_RGB = {"#": (0, 0, 0, 255), "M": (223, 223, 223, 255), "m": (178, 178, 178, 255),
              "h": (121, 121, 121, 255), "S": (105, 105, 105, 255), "s": (85, 85, 85, 255),
              "d": (77, 77, 77, 255), "g": (32, 32, 32, 255), "o": (65, 65, 0, 255), "c": (60, 60, 60, 255),
              "B": (138, 89, 48, 255), "T": (0, 89, 89, 255), "t": (0, 65, 65, 255), "k": (130, 93, 0, 255),
              "W": (93, 97, 255, 255), "w": (0, 0, 154, 255)}
MOAT_LT, CLEAR = (150, 152, 255, 255), (0, 0, 0, 0)


def lcg(seed):
    """The original's own pseudo-random sequence, for byte-stable dither."""
    s = seed & 0xFFFFFFFF
    while True:
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        yield s


def combat_material(p):
    """A material code for one original pixel; an unknown colour is kept as itself."""
    r, g, b, a = p
    rgb = (r, g, b)
    if a == 0:
        return "."
    if rgb == (0, 0, 0):
        return "#"
    if b > 200 and r < 140:
        return "W"                                   # moat water
    if rgb in ((0, 0, 154), (0, 125, 207), (0, 93, 158)):
        return "w"                                   # its dark edge
    if g > 140 and r < 100 and b < 100:
        return " "                                   # grass
    exact = {(223, 223, 223): "M", (178, 178, 178): "m", (121, 121, 121): "h", (105, 105, 105): "S",
             (85, 85, 85): "s", (77, 77, 77): "d", (32, 32, 32): "g", (44, 44, 44): "g", (65, 65, 0): "o",
             (60, 60, 60): "c"}
    if rgb in exact:
        return exact[rgb]
    if r > 100 and g < 100 and b < 80:
        return "B"                                   # brown rubble (before the trunk, as the original)
    return {(0, 89, 89): "T", (0, 65, 65): "t", (130, 93, 0): "k"}.get(rgb, (r, g, b, 255))


def combat_redraw(path, seed):
    """An original piece redrawn at 96 x 96: flat materials, the moat's bank
    dithered and the odd ripple, then every colour boundary softened."""
    src = rgba(path)
    sp = src.load()
    h, w = src.height, src.width
    sm = [[combat_material(sp[min(w - 1, x * w // TILE), min(h - 1, y * h // TILE)]) for x in range(TILE)]
          for y in range(TILE)]
    im = Image.new("RGBA", (TILE, TILE), CLEAR)
    px = im.load()
    r = lcg(seed)
    for y in range(TILE):
        for x in range(TILE):
            c = sm[y][x]
            if isinstance(c, tuple) or COMBAT_RGB.get(c):
                px[x, y] = c if isinstance(c, tuple) else COMBAT_RGB[c]
    for y in range(TILE):
        for x in range(TILE):
            if isinstance(sm[y][x], str) and sm[y][x] in "Ww":
                nb = [sm[yy][xx] for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1))
                      if 0 <= yy < TILE and 0 <= xx < TILE]
                if " " in nb or "." in nb:
                    px[x, y] = COMBAT_RGB["w"] if (x + y) % 2 == 0 else (CLEAR if next(r) % 3 == 0 else COMBAT_RGB["w"])
                elif sm[y][x] == "W" and next(r) % 23 == 0:
                    for k in range(3):
                        if x + k < TILE and sm[y][x + k] == "W":
                            px[x + k, y] = MOAT_LT
    # soften: along each boundary between two colours, swap every other pixel
    r = lcg(seed)
    ref = im.copy().load()
    for y in range(1, TILE - 1):
        for x in range(1, TILE - 1):
            for nx, ny in ((x + 1, y), (x, y + 1)):
                if ref[nx, ny] != ref[x, y] and (x + y) % 2 == 0 and next(r) % 2 == 0:
                    px[x, y] = ref[nx, ny]
    return im


def combat_cursor(path):
    """A cursor's ring, redrawn at 96 from the original's ink box, three px thick."""
    im = rgba(path)
    bb = im.getbbox()
    col = next(c for c in pixels(im) if c[3])
    out = Image.new("RGBA", (TILE, TILE), CLEAR)
    d = ImageDraw.Draw(out)
    rad = min((bb[2] - bb[0]) * 2, round((bb[3] - bb[1]) * TILE / 34)) // 2
    for k in range(3):
        d.ellipse((48 - rad + k, 48 - rad + k, 48 + rad - k, 48 + rad - k), outline=col)
    return out


@command("combat", "the combat spike burst and cursors, redrawn at 96 px from the reference pack's",
         A("pack", nargs="?", default=PACK), A("ref", nargs="?", default="assets/kings-bounty"))
def cmd_combat(a):
    rc, out = os.path.join(a.ref, "art", "combat"), os.path.join(a.pack, "art", "combat")
    os.makedirs(out, exist_ok=True)
    combat_redraw(os.path.join(rc, "castle_spike.png"), 31).save(os.path.join(out, "castle_spike.png"))
    for k in range(1, 5):
        combat_cursor(os.path.join(rc, f"cursor_{k:02d}.png")).save(os.path.join(out, f"cursor_{k:02d}.png"))
    print(f"castle_spike and cursor_01..04 -> {out}")


# ---- drawn lettering (generated lettering garbles) --------------------------

def bitmap_text(text, size, col, shade, offsets=((1, 1), (2, 2))):
    """C059 Bold at 1-bit: the glyphs in `col` over copies in `shade` at the offsets."""
    f = ImageFont.truetype(FONT, size)
    m = Image.new("L", (400, 80), 0)
    ImageDraw.Draw(m).text((4, 4), text, font=f, fill=255)
    m = m.point(lambda v: 255 if v >= 128 else 0)
    m = m.crop(m.getbbox())
    out = Image.new("RGBA", (m.width + 3, m.height + 3), (0, 0, 0, 0))
    for off in offsets:
        out.paste(Image.new("RGBA", m.size, shade), off, m)
    out.paste(Image.new("RGBA", m.size, col), (0, 0), m)
    return out


@command("splashlogo", "the publisher splash (320x84): drawn lettering round one generated emblem",
         A("emblem", help="the generated 44x44 emblem (job splash_logo_emblem)"), A("out"))
def cmd_splashlogo(a):
    canvas = Image.new("RGBA", (320, 84), (0, 0, 0, 0))
    d = ImageDraw.Draw(canvas)
    red, white = (180, 20, 20, 255), (255, 255, 255, 255)
    word = lambda t, s: bitmap_text(t, s, white, red, ((1, 1), (2, 2), (0, 2), (2, 0)))
    left, right, presents = word("Dan", 34), word("Heskett", 34), word("Presents...", 24)
    emb = rgba(a.emblem)
    x0 = (320 - (left.width + 6 + emb.width + 6 + right.width)) // 2
    canvas.paste(left, (x0, 16), left)
    ex = x0 + left.width + 6
    canvas.alpha_composite(emb, (ex, 4))
    canvas.paste(right, (ex + emb.width + 6, 16), right)
    canvas.paste(presents, ((320 - presents.width) // 2, 58), presents)
    for x, y, r in ((60, 58, 7), (262, 52, 5), (292, 44, 4)):          # coins
        d.ellipse((x - r, y - r, x + r, y + r), fill=(214, 160, 40, 255), outline=(120, 80, 10, 255))
        d.ellipse((x - r + 2, y - r + 2, x - r + 4, y - r + 4), fill=(255, 236, 150, 255))
    for x, y, c in ((88, 50, (90, 160, 255, 255)), (120, 60, (90, 160, 255, 255)),
                    (226, 58, (255, 90, 220, 255)), (250, 36, (90, 160, 255, 255))):     # stars
        for i in range(-3, 4):
            d.point((x + i, y), c)
            d.point((x, y + i), c)
    canvas.save(a.out)
    print("wrote", a.out)


@command("splashtitle", "the title screen (256x164): drawn title words over the generated eagle, or --words alone",
         A("eagle", nargs="?", help="the generated eagle standard (job splash_title)"), A("out", nargs="?"),
         A("--words", metavar="OUT", help="only the lettering, on clear, for the modern title screen"))
def cmd_splashtitle(a):
    gold, gold_dk, ink = (236, 200, 90, 255), (150, 100, 20, 255), (60, 30, 70, 255)
    out = a.words or a.out
    if not out or (not a.words and not a.eagle):
        sys.exit("romeart: splashtitle <eagle.png> <out.png>, or --words <out.png>")
    base = Image.new("RGBA", (256, 164), (0, 0, 0, 0)) if a.words else rgba(a.eagle)
    W, H = base.size
    line1 = bitmap_text("OPEN BOUNTY", 20, gold, gold_dk)
    line2 = bitmap_text("THE GLORY OF ROME", 13, gold, gold_dk)
    # the title in the clear band above the eagle, the subtitle across the
    # pole at its foot; a dark halo keeps both legible
    for im, y in ((line1, 2), (line2, H - line2.height - 4)):
        x = (W - im.width) // 2
        halo = Image.new("RGBA", im.size, ink)
        for off in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            base.paste(halo, (x + off[0], y + off[1]), im)
        base.alpha_composite(im, (x, y))
    base.save(out)
    print("wrote", out)


@command("classpicker", "the class-select carousel frames: one figure lit and ringed in gold, the rest dimmed",
         A("pack", nargs="?", default=PACK))
def cmd_classpicker(a):
    """Writes art/ui/class_select_picker_0..3.png from class_select_picker.png
    (read only). The landscape is found scanning each column down to the first
    dark outline (plus short sideways steps under overhangs); the figures
    overlap, so three hand-placed dividing lines decide whose pixel is whose,
    and three small hand fixes remain."""
    from PIL import ImageChops
    SRC = os.path.join(a.pack, 'art', 'ui', 'class_select_picker.png')
    DIM = 150                  # black over the other figures, out of 255
    GOLD = (255, 255, 85, 255) # the palette's yellow
    LIMIT = 90                 # the landscape never reaches below this row
    TH = 50                    # darker than this is an outline

    im = Image.open(SRC).convert('RGB'); W, H = im.size; px = im.load()
    def lum(c): return 0.299*c[0]+0.587*c[1]+0.114*c[2]
    # 1) background: flood from the top edge through pixels that are not dark
    #    outline, never below the landscape strip.
    bg=[[False]*W for _ in range(H)]
    for x in range(W):
        for y in range(min(H,LIMIT)):
            if lum(px[x,y])<TH: break
            bg[y][x]=True
    # hand-drawn dividers between neighbours: x as a polyline in y
    DIV=[
     [(60,0),(60,40),(68,48),(65,56),(62,64),(60,72),(58,80),(58,96),(57,120),(58,164)],
     [(124,0),(124,20),(130,48),(140,56),(142,62),(140,72),(140,80),(134,96),(128,104),(120,116),(120,136),(128,142),(133,150),(136,164)],
     [(186,0),(186,40),(190,52),(194,66),(199,80),(201,100),(201,140),(199,164)],
    ]
    def divx(poly,y):
        for (x0,y0),(x1,y1) in zip(poly,poly[1:]):
            if y0<=y<=y1: return x0+(x1-x0)*(y-y0)/max(1,y1-y0)
        return poly[-1][0]
    # narrow slivers of background (a gap in the outline) belong to the figure
    for y in range(H):
        x=0
        while x<W:
            if bg[y][x]:
                e=x
                while e<W and bg[y][e]: e+=1
                if e-x<=4 and x>0 and e<W:
                    for i in range(x,e): bg[y][i]=False
                x=e
            else: x+=1
    # sideways: a few pixels past known background, for short overhangs (hair
    # over a face) that a top-down scan cannot see under
    ext=[row[:] for row in bg]
    for y in range(min(H,LIMIT)):
        for x in range(W):
            if not bg[y][x]: continue
            for d in (1,-1):
                for i in range(1,5):
                    nx=x+d*i
                    if not (0<=nx<W) or bg[y][nx]: break
                    if lum(px[nx,y])<70: break
                    c0=px[x,y]; c1=px[nx,y]
                    if sum(abs(c0[j]-c1[j]) for j in range(3))>90: break
                    ext[y][nx]=True
    bg=ext
    # narrow slivers of background (a gap in the outline) belong to the figure
    for y in range(H):
        x=0
        while x<W:
            if bg[y][x]:
                e=x
                while e<W and bg[y][e]: e+=1
                if e-x<=4 and x>0 and e<W:
                    for i in range(x,e): bg[y][i]=False
                x=e
            else: x+=1
    # hand fixes: (x0,y0,x1,y1) inclusive
    FORCE_BG=[(22,33,26,39)]          # trees showing beside the Legatus's neck
    FORCE_FIG=[(160,15,162,15),(152,16,162,16),(151,17,162,17),(150,18,162,18)]+[(149,y,162,y) for y in range(19,25)]
                                      # the top of the Sibylla's veil and fillet
    FORCE_FIG+=[(93,8,101,8),(90,9,104,9),(89,10,107,10),(89,11,107,11),(91,12,108,12),
                (90,13,109,13),(89,14,110,14),(88,15,110,15),(88,16,111,16)]
                                      # the top of the Praetorianus's hood: its outline is
                                      # lighter than TH, so the scan runs on into the hood
    for x0,y0,x1,y1 in FORCE_BG:
        for y in range(y0,y1+1):
            for x in range(x0,x1+1):
                if lum(px[x,y])>=70: bg[y][x]=True
    for x0,y0,x1,y1 in FORCE_FIG:
        for y in range(y0,y1+1):
            for x in range(x0,x1+1): bg[y][x]=False

    pic = im.convert('RGBA')
    for k in range(4):
        m = Image.new('L', (W, H), 0); mp = m.load()
        for y in range(H):
            lo = divx(DIV[k-1], y) if k > 0 else -1
            hi = divx(DIV[k], y) if k < 3 else W + 1
            for x in range(W):
                if not bg[y][x] and lo <= x < hi: mp[x, y] = 255
        out = Image.alpha_composite(pic, Image.new('RGBA', (W, H), (0, 0, 0, DIM)))
        ring = ImageChops.subtract(m.filter(ImageFilter.MaxFilter(3)), m)
        out.paste(GOLD, (0, 0), ring)
        out.paste(pic, (0, 0), m)
        dst = os.path.join(a.pack, 'art', 'ui', 'class_select_picker_%d.png' % k)
        out.convert('RGB').save(dst)
        print(dst)


@command("icon", "the launcher icon (128, 512, 1024) from the title's own battle and eagle",
         A("out", nargs="?", default="build/art/icon"))
def cmd_icon(a):
    """The title screen composes title_battle.png and title_eagle.png at run
    time; the icon is the same two, squared at 128 with the menu and
    wordmarks left out, then doubled to 512 and 1024 nearest-neighbour, so it
    is the game's own art at icon size. Opaque: Apple rejects an icon with
    alpha."""
    os.makedirs(a.out, exist_ok=True)
    battle, eagle = rgba(f"{PACK}/art/ui/title_battle.png"), rgba(f"{PACK}/art/ui/title_eagle.png")
    icon = Image.new("RGBA", (128, 128))
    icon.paste(battle.crop((64, 36, 192, 164)), (0, 0))      # the ridge: ranks before, the enemy behind
    standard = eagle.crop((0, 0, 96, 128))                   # the pole leaves the frame under the plaque
    icon.paste(standard, (16, 0), standard)
    icon = icon.convert("RGB")
    icon.save(f"{a.out}/icon_128.png")
    for n in (4, 8):
        icon.resize((128 * n, 128 * n), Image.NEAREST).save(f"{a.out}/icon_{128 * n}.png")
    print(f"icon: {a.out}/icon_128.png, icon_512.png, icon_1024.png")


@command("introtheme", "the Introduction's theme, synthesised (no licence, no credit); needs oggenc",
         A("out"), A("--length", type=float, default=252.3, help="the intro's running time, s"),
         A("--level", type=float, default=0.25, help="scales the whole mix (0.25: background)"))
def cmd_introtheme(a):
    """A lyre (Karplus-Strong) plays slow broken chords over a soft E/B drone;
    one guest a minute fades in over 8 s and out over 10 s -- the aulos at
    1:00, frame drum and finger cymbals at 2:00, pan pipes at 3:00 -- and the
    lyre is alone again from 4:00. Greek Dorian, 66 bpm. Seeded, and the ogg
    serial fixed, so the same arguments give the same file."""
    import wave
    np = need("numpy", "introtheme")

    SR = 44100
    rng = np.random.default_rng(154)
    hz = lambda n: 440.0 * 2 ** (n / 12)
    NOTE = {'E3': -17, 'F3': -16, 'G3': -14, 'A3': -12, 'B3': -10, 'C4': -9, 'D4': -7,
            'E4': -5, 'F4': -4, 'G4': -2, 'A4': 0, 'B4': 2, 'C5': 3, 'D5': 5, 'E5': 7,
            'F5': 8, 'G5': 10, 'A5': 12}
    LENGTH = a.length
    beat = 60 / 66; bar = 4 * beat
    n_total = int((LENGTH + 4) * SR)

    def pluck(f, dur, amp):
        n = int(dur * SR); N = int(SR / f)
        buf = rng.uniform(-1, 1, N)
        buf = 0.5 * (buf + np.roll(buf, 1))            # a soft excitation: gut strings
        out = np.zeros(n); out[:N] = buf; out[N] = 0.996 * buf[0]; i = N + 1
        while i < n:
            k = min(N, n - i)
            out[i:i + k] = 0.996 * 0.5 * (out[i - N:i - N + k] + out[i - N - 1:i - N - 1 + k]); i += k
        r = int(0.05 * SR); out[-r:] *= np.linspace(1, 0, r)
        return amp * out

    def wind(f, dur, amp, vib=0.004, harm=(1, .35, .12), air=0.6, attack=0.08):
        n = int(dur * SR); t = np.arange(n) / SR
        v = 1 + vib * np.sin(2 * np.pi * 5.2 * t) * np.clip(t / 0.4, 0, 1)
        ph = 2 * np.pi * np.cumsum(f * v) / SR
        tone = sum(h * np.sin((k + 1) * ph) for k, h in enumerate(harm))
        br = rng.normal(0, 1, n)
        for _ in range(4):                             # breath: low-passed, following the tone
            br = np.convolve(br, np.ones(12) / 12, 'same')
        br *= air * np.abs(np.sin(ph / 2))
        at = int(attack * SR); r = int(0.25 * SR); env = np.ones(n)
        env[:at] = np.linspace(0, 1, at); env[-r:] = np.linspace(1, 0, r)
        return amp * (tone + br) * env
    aulos = lambda f, d: wind(f, d, 0.10)                                            # reedy
    pipes = lambda f, d: wind(f, d, 0.09, vib=0.002, harm=(1, .08, .03), air=1.4, attack=0.03)  # pure, airy

    def drum(amp):
        n = int(0.6 * SR); t = np.arange(n) / SR
        body = np.sin(2 * np.pi * (70 + 40 * np.exp(-t * 30)) * t) * np.exp(-t * 8)
        skin = np.convolve(rng.normal(0, 1, n) * np.exp(-t * 40), np.ones(8) / 8, 'same') * 0.3
        return amp * (body + skin)

    def cymbal(amp):                                   # finger cymbals: an inharmonic ring
        n = int(1.6 * SR); t = np.arange(n) / SR
        return amp * sum(np.sin(2 * np.pi * f * t) * np.exp(-t * d)
                         for f, d in ((2730, 2.5), (3920, 3.2), (5410, 4.5), (6890, 6))) / 4

    def place(trk, sig, at):
        i = int(at * SR); j = min(len(trk), i + len(sig))
        if i < len(trk): trk[i:j] += sig[:j - i]

    def window(t0, t1, fin=8.0, fout=10.0):            # a guest's fade in and out
        t = np.arange(n_total) / SR
        return np.clip((t - t0) / fin, 0, 1) * np.clip((t1 - t) / fout, 0, 1)

    lyre, aul, perc, pan = (np.zeros(n_total) for _ in range(4))
    chords = [['E3', 'B3', 'E4', 'G4'], ['D4', 'A3', 'D4', 'F4'], ['C4', 'G3', 'C4', 'E4'], ['B3', 'E3', 'B3', 'D4'],
              ['A3', 'E4', 'A4', 'C5'], ['G3', 'D4', 'G4', 'B4'], ['F3', 'C4', 'F4', 'A4'], ['E3', 'B3', 'E4', 'B4']]
    for b in range(int(LENGTH / bar) + 1):
        ch = chords[b % 8]
        for k, nm in enumerate(ch + ch[1:3][::-1]):    # six plucks a bar
            place(lyre, pluck(hz(NOTE[nm]), 3.0, 0.42 if k == 0 else 0.32), b * bar + k * bar / 6)
        place(perc, drum(0.55), b * bar); place(perc, drum(0.33), b * bar + 2.5 * beat)
        if b % 2: place(perc, cymbal(0.16), b * bar + beat)
    phr_aulos = [[('B4', 2), ('A4', 1), ('G4', 1), ('A4', 3), (None, 1), ('G4', 1), ('F4', 1), ('E4', 2), ('F4', 2), ('E4', 4)],
                 [('E4', 1), ('G4', 1), ('A4', 2), ('B4', 2), ('C5', 2), ('B4', 1), ('A4', 1), ('G4', 2), ('A4', 4), (None, 4)],
                 [('D5', 2), ('C5', 1), ('B4', 1), ('A4', 2), ('B4', 2), ('G4', 2), ('F4', 2), ('E4', 4), (None, 4)],
                 [('E4', 2), ('F4', 1), ('G4', 1), ('A4', 2), ('G4', 1), ('F4', 1), ('E4', 8)]]
    phr_pipes = [[('E5', 3), ('D5', 1), ('B4', 4), ('C5', 2), ('B4', 1), ('A4', 1), ('B4', 4)],
                 [('G5', 2), ('F5', 2), ('E5', 4), ('D5', 2), ('C5', 1), ('D5', 1), ('E5', 4)],
                 [('A5', 3), ('G5', 1), ('E5', 4), ('F5', 2), ('E5', 1), ('D5', 1), ('E5', 4)],
                 [('B4', 2), ('C5', 2), ('D5', 2), ('C5', 1), ('B4', 1), ('E5', 8)]]

    def melody(trk, phrases, voice, t0, t1):
        t = t0
        while t < t1:
            for ph in phrases:
                tt = t
                for nm, nb in ph:
                    if nm: place(trk, voice(hz(NOTE[nm]), nb * beat * 0.98), tt)
                    tt += nb * beat
                t += 4 * bar
                if t >= t1: break
    melody(aul, phr_aulos, aulos, 16 * bar, 125)      # heard only inside its window
    melody(pan, phr_pipes, pipes, 48 * bar, 245)
    t = np.arange(n_total) / SR
    drone = 0.05 * (np.sin(2 * np.pi * hz(-29) * t) + 0.6 * np.sin(2 * np.pi * hz(-22) * t)) \
        * (0.7 + 0.3 * np.sin(2 * np.pi * t / 17))
    mix = (lyre + drone + aul * window(60, 120) + perc * window(120, 180) + pan * window(180, 240))[:int(LENGTH * SR)]
    nir = int(2.2 * SR)                                 # reverb: a decaying noise tail, by FFT
    ir = rng.normal(0, 1, nir) * np.exp(-np.arange(nir) / SR * 3.1); ir[0] = 0
    F = 1 << (len(mix) + nir - 1).bit_length()
    wet = np.fft.irfft(np.fft.rfft(mix, F) * np.fft.rfft(ir, F), F)[:len(mix)]
    mix = 0.72 * mix + 0.28 * wet / np.max(np.abs(wet)) * np.max(np.abs(mix))
    fi, fo = int(3 * SR), int(8 * SR)
    mix[:fi] *= np.linspace(0, 1, fi); mix[-fo:] *= np.linspace(1, 0, fo)
    mix = mix / np.max(np.abs(mix)) * 0.6 * a.level    # 0.6 peak is the approved balance; level scales it
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "theme.wav")
        with wave.open(wav, "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
            w.writeframes((mix * 32767).astype(np.int16).tobytes())
        subprocess.run(["oggenc", "-Q", "-q", "4", "--serial", "1", wav, "-o", a.out], check=True)
    print(f"{a.out}  {LENGTH:.0f} s, peak {0.6 * a.level:.2f}")


# ==========================================================================
# Sprites: colour matching, troop installs, zone objects
# ==========================================================================

@command("colormatch", "match a sprite's opaque colours to a reference image's (mean hue, saturation and value)",
         A("src"), A("ref"), A("out"),
         A("--keep-hue", action="append", default=[], metavar="LO,HI",
           help="leave pixels in this hue range (0..360) alone, e.g. a trunk's browns"))
def cmd_colormatch(a):
    """The step a generated sprite takes to sit in a set it was not drawn
    for (Italia's trees, #63): every opaque pixel's hue shifted, and its
    saturation and value scaled, by the difference between the sprite's mean
    and the reference's. The Italia trees were matched by an earlier hand
    version of this step and are kept as committed primitives."""
    import colorsys
    keep = [tuple(float(v) for v in r.split(",")) for r in a.keep_hue]
    to = lambda c: colorsys.rgb_to_hsv(*(v / 255 for v in c[:3]))
    mean = lambda hsvs: [sum(v[k] for v in hsvs) / max(1, len(hsvs)) for k in range(3)]
    im = rgba(a.src)
    px = im.load()
    mine = [(x, y, to(px[x, y])) for y in range(im.height) for x in range(im.width) if px[x, y][3]]
    moving = [(x, y, h) for x, y, h in mine if not any(lo <= h[0] * 360 <= hi for lo, hi in keep)]
    ref = [to(c) for c in pixels(rgba(a.ref)) if c[3]]
    (h0, s0, v0), (h1, s1, v1) = mean([h for _, _, h in moving]), mean(ref)
    for x, y, (h, s, v) in moving:
        r, g, b = colorsys.hsv_to_rgb((h + h1 - h0) % 1.0, min(1, s * s1 / max(s0, 1e-6)), min(1, v * v1 / max(v0, 1e-6)))
        px[x, y] = (round(r * 255), round(g * 255), round(b * 255), px[x, y][3])
    im.save(a.out)
    print(f"{a.out}: hue {(h1 - h0) * 360:+.1f}, sat x{s1 / max(s0, 1e-6):.2f}, val x{v1 / max(v0, 1e-6):.2f}")


def gj_troop_set(text, troop, **values):
    """game.json's text with fields of one troop entry replaced (strings or lists)."""
    lo = text.index('\n\t"troops":\t[')
    hi = text.index('\n\t\t}]', lo)
    hits = [k for k in range(lo, hi) if text.startswith(f'\n\t\t\t"id":\t"{troop}",', k)]
    if len(hits) != 1:
        sys.exit(f"romeart: no single troop entry '{troop}' in game.json")
    i = hits[0]
    j = text.index('\n\t\t}', i)
    block = text[i:j]
    for k, v in values.items():
        start = block.find(f'\n\t\t\t"{k}":\t')
        if start < 0:
            sys.exit(f"romeart: troop {troop} has no {k}")
        end = block.find("\n", start + 1)
        end = len(block) if end < 0 else end
        sep = "," if block[end - 1] == "," else ""
        val = "[" + ", ".join(json.dumps(x) for x in v) + "]" if isinstance(v, list) else json.dumps(v)
        block = block[:start] + f'\n\t\t\t"{k}":\t{val}{sep}' + block[end:]
    return text[:i] + block + text[j:]


@command("troop", "troop install: a troop's attack frames and portrait from their runs into the pack",
         A("action", choices=("install",)), A("troop", help="the troop id"),
         A("frames", help="the attack run folder (build/art/<id>_attack/runNN: frame_NN.png)"),
         A("portrait", nargs="?", help="the portrait run folder (build/art/troop_portrait_<id>/runNN)"))
def cmd_troop(a):
    """art/troops/<id>_NN.png from the run's frames, art/portraits/troop_<id>.png
    from the portrait's 128 px still (resized whole to 96, Lanczos), and the
    troop's sprite, portrait and anim in game.json pointed at them. The jobs'
    "pack" lists are updated so the record names what they made."""
    frames = sorted(f for f in os.listdir(a.frames) if f.startswith("frame_") and f.endswith(".png"))
    if not frames:
        sys.exit(f"romeart: no frame_NN.png in {a.frames}")
    p = os.path.join(PACK, "game.json")
    text = open(p).read()
    if not any(t["id"] == a.troop for t in json.loads(text)["troops"]):
        sys.exit(f"romeart: no troop '{a.troop}' in game.json")
    anim = [f"art/troops/{a.troop}_{k:02d}.png" for k in range(len(frames))]
    for f, dst in zip(frames, anim):
        rgba(os.path.join(a.frames, f)).save(os.path.join(PACK, dst))
    values = {"sprite": anim[0], "anim": anim}
    if a.portrait:
        dst = f"art/portraits/troop_{a.troop}.png"
        rgba(os.path.join(a.portrait, "01_raw.png")).resize((TILE, TILE), Image.LANCZOS).save(os.path.join(PACK, dst))
        values["portrait"] = dst
    text = gj_troop_set(text, a.troop, **values)
    want = json.loads(open(p).read())
    for t in want["troops"]:
        if t["id"] == a.troop:
            t.update(values)
    if json.loads(text) != want:
        sys.exit("romeart: refusing to write game.json -- the edit changed something else")
    open(p, "w").write(text)
    for run, made in ((a.frames, anim), (a.portrait, [values.get("portrait")])):
        if run:
            job_id = os.path.basename(os.path.dirname(os.path.abspath(run)))
            jp = os.path.join(JOBS, job_id + ".json")
            if os.path.exists(jp):
                job_record(jp, pack=made)
    print(f"installed {a.troop}: {len(anim)} frames" + (", portrait" if a.portrait else ""))


def gj_object_span(text, oid, zone=None):
    """(start, end) of the one flat object with this id in game.json's text,
    '{' to '}', optionally inside one zone's block."""
    lo, hi = 0, len(text)
    if zone:
        lo = text.find(f'\n\t\t\t"id":\t"{zone}",')
        nxt = text.find('\n\t\t}, {\n', lo)
        hi = nxt if nxt >= 0 else len(text)
    hits = [i for i in range(lo, hi) if text.startswith(f'"id":\t"{oid}"', i)]
    if len(hits) != 1:
        sys.exit(f"romeart: {len(hits)} objects with id '{oid}'" + (f" in {zone}" if zone else "") + "; want one")
    return text.rindex("{", lo, hits[0]), text.index("}", hits[0]) + 1


@command("objects", "move a zone object, or add one, in game.json (kept formatting, checked)",
         A("action", choices=("move", "add")), A("zone"),
         A("what", help="move: the object's id; add: the zone array (chests, wandering_armies, signs, ...)"),
         A("value", nargs="+", help="move: X Y; add: the object as JSON"))
def cmd_objects(a):
    """Every edit is made in the text and checked by parsing: nothing else in
    game.json may change. Run `map check` and `map sanity` after a move."""
    p = os.path.join(PACK, "game.json")
    text = open(p).read()
    want = json.loads(text)
    zone = next((z for z in want["zones"] if z["id"] == a.zone), None)
    if zone is None:
        sys.exit(f"romeart: no zone '{a.zone}'")
    if a.action == "move":
        x, y = (int(v) for v in a.value)
        s, e = gj_object_span(text, a.what, a.zone)
        obj = text[s:e]
        for k, v in (("x", x), ("y", y)):
            i = obj.find(f'"{k}":\t')
            if i < 0:
                sys.exit(f"romeart: {a.what} has no {k}")
            j = i + len(f'"{k}":\t')
            n = j
            while n < len(obj) and (obj[n].isdigit() or obj[n] == "-"):
                n += 1
            obj = obj[:j] + str(v) + obj[n:]
        text = text[:s] + obj + text[e:]
        moved = 0
        for key, arr in zone.items():
            if isinstance(arr, list):
                for o in arr:
                    if isinstance(o, dict) and o.get("id") == a.what:
                        o["x"], o["y"] = x, y
                        moved += 1
        if moved != 1:
            sys.exit(f"romeart: '{a.what}' is not one object of zone {a.zone}")
    else:
        obj = json.loads(" ".join(a.value))
        text = gj_set_zone_array(text, a.zone, a.what, [obj], append=True)
        zone.setdefault(a.what, []).append(obj)
    if json.loads(text) != want:
        sys.exit("romeart: refusing to write game.json -- the edit changed something else")
    open(p, "w").write(text)
    print(f"{a.action}d {a.what} in {a.zone}")


# ==========================================================================
# Review: review pages, animation checks, the prompt record
# ==========================================================================

def strip(frames, scale, label=True):
    """Frames side by side at a scale, on grey, labelled."""
    w, h = frames[0].size
    out = Image.new("RGB", (len(frames) * (w * scale + 8) + 8, h * scale + 16), (60, 60, 60))
    d = ImageDraw.Draw(out)
    for i, f in enumerate(frames):
        b = f.resize((w * scale, h * scale), Image.NEAREST)
        out.paste(b, (8 + i * (w * scale + 8), 12), b)
        if label:
            d.text((8 + i * (w * scale + 8), 0), f"frame {i}", fill=(230, 230, 230))
    return out


def save_gif(frames, path, scale=3, ground=None):
    """A looping gif that clears between frames (disposal 2: without it PIL
    paints frames over one another and a loop looks smeared)."""
    big = []
    for f in frames:
        if ground:
            g = Image.new("RGBA", f.size, ground)
            g.alpha_composite(f)
            f = g
        big.append(f.resize((f.width * scale, f.height * scale), Image.NEAREST))
    big[0].save(path, save_all=True, append_images=big[1:], duration=150, loop=0, disposal=2, transparency=0)
    return big


@command("loopreview", "an animation run's review: gif, strips and per-frame checks",
         A("run", help="build/art/<id>/runNN"), A("--scale", type=int, default=5))
def cmd_loopreview(a):
    """Checks, per frame: size; clip (opaque pixels at a canvas edge: a cut
    limb or weapon); halo (near-white pixels beside clear: a flatten fringe);
    opaque count (a redrawn figure jumps); moved (pixels changed against
    frame 0, and how many in the bottom quarter: feet should stay planted);
    and that every decoded gif frame equals its source."""
    from PIL import ImageChops
    run = a.run.rstrip("/")
    names = sorted(f for f in os.listdir(run) if f.startswith("frame_") and f.endswith(".png"))
    frames = [rgba(os.path.join(run, f)) for f in names]
    if not frames:
        sys.exit(f"romeart: no frame_*.png in {run}")
    w, h = frames[0].size
    opaque = lambda im: im.getchannel("A").point(lambda v: 255 if v > 0 else 0)
    rows = []
    for name, f in zip(names, frames):
        bb = opaque(f).getbbox()
        clip = [s for s, hit in (("top", bb[1] == 0), ("bottom", bb[3] == h), ("left", bb[0] == 0), ("right", bb[2] == w)) if hit]
        px = f.load()
        halo = sum(1 for y in range(h) for x in range(w) if px[x, y][3] and min(px[x, y][:3]) >= 200 and any(
            0 <= x + dx < w and 0 <= y + dy < h and px[x + dx, y + dy][3] == 0 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))))
        d = ImageChops.difference(f, frames[0]).convert("L").point(lambda v: 255 if v > 24 else 0).load()
        moved = [(x, y) for y in range(h) for x in range(w) if d[x, y]]
        rows.append((name, clip, halo, sum(1 for v in pixels(opaque(f)) if v), len(moved),
                     sum(1 for _, y in moved if y >= h * 3 // 4)))
    gif = os.path.join(run, "preview.gif")
    big = save_gif(frames, gif)
    g = Image.open(gif)
    gif_ok = True
    for i in range(g.n_frames):
        g.seek(i)
        off = ImageChops.difference(g.convert("RGBA"), big[i]).convert("L").point(lambda v: 255 if v > 40 else 0)
        gif_ok &= sum(1 for v in pixels(off) if v) <= (w * h * 9) // 100
    for s in sorted({1, a.scale}):
        strip(frames, s).save(os.path.join(run, f"strip{s}x.png"))
    report = [f"{n} clip {c or 'none'} halo {hl} opaque {o} moved {t} feet {ft}" for n, c, hl, o, t, ft in rows]
    print(f"{run}: {len(frames)} frames {w}x{h}\n  gif clears between frames: {'yes' if gif_ok else 'NO'}")
    print("\n".join("  " + r for r in report))
    open(os.path.join(run, "review.html"), "w").write(
        f"<!doctype html><meta charset=utf-8><title>{run}</title><style>body{{background:#3c3c3c;color:#ddd;"
        f"font:12px sans-serif;padding:16px}}img{{image-rendering:pixelated;display:block;margin-bottom:12px}}</style>"
        f"<h2>{run}</h2><img src='preview.gif'><img src='strip1x.png'><img src='strip{a.scale}x.png' style='max-width:100%'>"
        f"<pre>{chr(10).join(report)}\ngif clears between frames: {'yes' if gif_ok else 'NO'}</pre>")
    print(f"  page: {run}/review.html")


@command("review", "one static review page for chosen runs: each still, attack loop, frames and portrait",
         A("out", help="a folder for the page (index.html)"), A("runs", nargs="+", help="run folders"),
         A("--title", default="Review"), A("--scale", type=int, default=3))
def cmd_review(a):
    """For every run folder: its 01_raw.png on map grass at --scale, and when
    it holds frame_NN.png a looping gif and the frame strip. Serve the folder
    (python3 -m http.server) to look at it."""
    os.makedirs(a.out, exist_ok=True)
    grass = (74, 140, 48, 255)
    cells = []
    for k, run in enumerate(a.runs):
        run = run.rstrip("/")
        name = f"{k:02d}_" + "_".join(run.split("/")[-2:])
        html = [f"<h2>{run}</h2>"]
        raw = os.path.join(run, "01_raw.png")
        if os.path.exists(raw):
            im = rgba(raw)
            g = Image.new("RGBA", im.size, grass); g.alpha_composite(im)
            g.resize((im.width * a.scale, im.height * a.scale), Image.NEAREST).save(os.path.join(a.out, name + ".png"))
            html.append(f"<img src='{name}.png'>")
        frames = [rgba(os.path.join(run, f)) for f in sorted(os.listdir(run)) if f.startswith("frame_") and f.endswith(".png")]
        if frames:
            save_gif(frames, os.path.join(a.out, name + ".gif"), a.scale, grass)
            strip(frames, 2, label=False).save(os.path.join(a.out, name + "_frames.png"))
            html += [f"<img src='{name}.gif'>", f"<img src='{name}_frames.png' style='max-width:100%'>"]
        cells.append("<div>" + "".join(html) + "</div>")
    open(os.path.join(a.out, "index.html"), "w").write(
        f"<!doctype html><meta charset=utf-8><title>{a.title}</title><style>body{{background:#1d1d22;color:#ddd;"
        f"font:15px sans-serif;margin:24px}}img{{image-rendering:pixelated;margin:4px;vertical-align:top}}</style>"
        f"<h1>{a.title}</h1>" + "".join(cells))
    print(f"wrote {a.out}/index.html: {len(a.runs)} runs")


# ---- the prompt record: docs/ROME-ART.md from art/jobs -----------------------

PROMPT_SKIP = {"id", "prompt", "description", "lower_description", "upper_description",
               "transition_description", "batches", "_note", "_pack_path", "_review_rule", "pack", "runs"}


def job_engine(d):
    """(engine, its headline settings) of a job file; the PixelLab shapes are
    the record of the primitives that engine made before Retro Diffusion."""
    if "lower_description" in d:
        ts = d.get("tile_size", {})
        size = ts.get("width", ts) if isinstance(ts, dict) else ts
        return "PixelLab create-tileset", f"{size} px, seed {d.get('seed', '-')}"
    if "batches" in d:
        return "PixelLab create-1-direction-object", f"96 px, {sum(len(b) for b in d['batches'])} items"
    if "tile_size" in d:
        return "PixelLab tiles", f"{d.get('tile_size')} px, seed {d.get('seed', '-')}"
    return f"Retro Diffusion {d.get('style', '?')}", f"{d.get('width', '?')}x{d.get('height', '?')}, seed {d.get('seed', '-')}"


def job_prompts(d):
    if "lower_description" in d:
        return [(label, d[key]) for key, label in (("lower_description", "lower"), ("upper_description", "upper"),
                                                   ("transition_description", "where they meet")) if d.get(key)]
    if "batches" in d:
        return [("shared", d.get("description", ""))] + [(f"batch {i}", " · ".join(b)) for i, b in enumerate(d["batches"], 1)]
    return [(label, d[key]) for key, label in (("prompt", "prompt"), ("description", "description")) if d.get(key)]


def job_group(name, d):
    p = d.get("_pack_path", "")
    if name.startswith("intro_"):
        return "Introduction"
    for frag, g in (("art/troops/", "Troops"), ("art/portraits/", "Portraits and faces"), ("art/villains/", "Villains"),
                    ("art/classes/", "Hero classes"), ("art/tiles/", "Map tiles and terrain"), ("art/scenes/", "Scenes"),
                    ("art/ui/", "Screens and UI")):
        if p.startswith(frag):
            return g
    if "lower_description" in d or name.startswith(("t16_", "t32_", "grass16", "grass32")):
        return "Map tiles and terrain"
    if "batches" in d:
        return "Sprite batches (trees, rocks)"
    if name.startswith(("backdrop", "splash", "title")):
        return "Screens and UI"
    if name.startswith("troop_portrait") or "portrait" in name:
        return "Portraits and faces"
    # a step towards a troop (its still, its attack loop): grouped with the troop it names
    if glob.glob(os.path.join(PACK, "art", "troops", name.split("_")[0] + "_*.png")):
        return "Troops"
    return "Animations" if str(d.get("style", "")).startswith("rd_advanced_animation") else "Other"


def job_live(d):
    """A job whose pack path is not in the pack makes nothing the game uses."""
    pp = d.get("_pack_path", "").split(" ")[0].replace("<x>_<y>", "0_0")
    if not pp:
        return True
    import re
    m = re.match(r"^(.*_)(\d+)\.\.(\d+)(\.\w+)$", pp)
    return os.path.exists(os.path.join(PACK, (m.group(1) + m.group(2) + m.group(4)) if m else pp))


@command("prompts", "rebuild docs/ROME-ART.md: every prompt and setting the pack's art was made from",
         A("out", nargs="?", default="docs/ROME-ART.md"))
def cmd_prompts(a):
    """One page, generated from art/jobs/*.json so it cannot drift: each job's
    engine and settings and the prompt exactly as sent. A job with no pack
    path is a step towards one; a job whose output is not in the pack is left
    out. The jobs' _note history stays in the job files."""
    jobs = []
    for f in sorted(os.listdir(JOBS)):
        if f.endswith(".json"):
            try:
                d = load_json(os.path.join(JOBS, f))
            except ValueError:
                continue
            if job_live(d):
                jobs.append((f[:-5], d))
    groups = {}
    for name, d in jobs:
        groups.setdefault(job_group(name, d), []).append((name, d))
    lines = ["# Rome art: every prompt and setting", "",
             "**Generated** by `tools/romeart.py prompts` from `art/jobs/*.json`. Do not",
             "edit by hand: change the job file and run the tool again.", "",
             f"{len(jobs)} jobs. A job with a **Pack path** has produced that file in the pack; a",
             "job without one has produced a step towards it (the still an animation starts",
             "from). The routes themselves -- which engine, which settings, and why -- have",
             "been in `docs/ART-PIPELINE.md`; each job's run history has been in its own",
             "`_note` field.", ""]
    for g in sorted(groups):
        lines += [f"## {g}", ""]
        for name, d in sorted(groups[g]):
            eng, headline = job_engine(d)
            lines += [f"### {name}", "", f"- **Engine:** {eng} ({headline})"]
            if d.get("_pack_path"):
                lines.append(f"- **Pack path:** `{d['_pack_path']}`")
            lines += [f"- **{label}:** {text}" for label, text in job_prompts(d)]
            st = []
            for k in sorted(d):
                if k in PROMPT_SKIP:
                    continue
                v = d[k]
                st.append(f"{k}=<image>" if isinstance(v, dict) and ("base64" in v or "image" in v) else
                          f"{k}=<{len(v)} chars>" if isinstance(v, str) and len(v) > 80 else
                          f"{k}={v if isinstance(v, str) else json.dumps(v)}")
            if st:
                lines.append("- **Settings:** " + ", ".join(f"`{x}`" for x in st))
            lines.append("")
    open(a.out, "w").write("\n".join(lines) + "\n")
    print(f"wrote {a.out}: {len(jobs)} jobs in {len(groups)} groups")


# ==========================================================================
# The record: every shipped art file and how it is rebuilt
# ==========================================================================
#
# A file under assets/glory-of-rome/art/ is accounted for one of four ways:
#   a JOB      a job file's "pack" list names it: it is that generation's output
#              (downscaled, cropped or framed as the job's notes say);
#   a RECIPE   a command composites it from committed inputs (below), and the
#              rebuild check re-runs the deterministic ones against the pack;
#   a SOURCE   it is kept as made, its input lost -- with the reason;
#   EXTERNAL   it came from outside (the font), with its licence beside it.
# `provenance check` fails on a file none of these names, and on any job,
# recipe or source naming a file that is not there.

RECIPES = [   # (glob under art/, the command that makes it)
    ("tiles/galliae/*.png", "zone galliae && install galliae"),
    ("tiles/africa/*.png", "zone africa && install africa"),
    ("tiles/oriens/*.png", "zone oriens && install oriens"),
    ("tiles/*_fill_*.png", "fills"),
    ("tiles/*_apron_*.png", "aprons"),
    ("tiles/detail_[1-4].png", "details"),
    ("tiles/forest_v[12].png", "interiors"), ("tiles/mountain_v[12].png", "interiors"),
    ("tiles/mountain_edge_[01][0-9]_v[12].png", "edgevars"),
    ("tiles/water_sand_edge_*.png", "shore"),
    ("tiles/dock_[nesw].png", "dock"),
    ("tiles/fields_*_edge_*.png", "edges assets/glory-of-rome '' --as fields_wheat=desert --as fields_plough=desert"),
    ("tiles/river_mouth_[ew]_s.png", "zone (the mouths drawn the other way up)"),
    ("combat/field/*.png", "fields"),
    ("combat/castle_spike.png", "combat"), ("combat/cursor_0[1-4].png", "combat"),
    ("ui/class_select_picker_[0-3].png", "classpicker"),
    ("ui/title_words.png", "splashtitle --words"),
    ("tiles/forest.png", "compose art/layouts/forest96_italia.json"),
    ("tiles/forest_edge_[01][0-9].png", "compose art/layouts/forest96_italia.json"),
    ("tiles/mountain.png", "compose art/layouts/mountain96.json"),
    ("tiles/mountain_edge_[01][0-9].png", "compose art/layouts/mountain96.json"),
    ("portraits/*_0[89].png", "pingpong (bounce)"), ("portraits/*_1[0-3].png", "pingpong (bounce)"),
    ("ui/hud_siege_0[4-7].png", "pingpong art/ui/hud_siege 4 --reverse"),
]

SOURCES = [   # (glob under art/, why it is kept as made)
    ("tiles/*.png", "the master set (Italia): its sea, desert, roads and rivers were stitched and swept in #38-#67 "
                    "from PixelLab corner sets that were not kept, its river_forest/river_mountain pieces rebanked "
                    "(art/primitives/italia/BUILD.md), its farmland bases cut by hand in #225"),
    ("troops/hastati_*.png", "made for #38 before the job record"),
    ("troops/lupi_*.png", "made for #38 before the job record"),
    ("troops/praetoriani_*.png", "made for #38 before the job record"),
    ("classes/[a-z]*[a-z].png", "the class portraits, made for #38 before the job record; no recipe was kept"),
    ("ui/class_select_picker.png", "the carousel painting, made for #38 before the job record"),
    ("ui/class_select_highlight.png", "made for #38 before the job record"),
    ("ui/puzzle_cover.png", "made for #38 before the job record"),
    ("ui/scene_column_*.png", "the column pieces, cut by hand in #38 from the scene_column job's strip"),
]

EXTERNAL = ["font/*"]      # the OFL font and its licence


def pack_paths(text):
    """The pack files a job's free-text _pack_path names: every art/...png in
    it, ranges (x_00..07.png) and the siege grid's <x>_<y> written out; the
    notes in brackets ignored."""
    import re
    out = []
    for m in re.finditer(r"art/[\w/<>]+?(?:_(\d+)\.\.(\d+))?\.png", text):
        p = m.group(0)
        if m.group(1):
            a, b = m.group(1), m.group(2)
            stem = p[:p.index(f"_{a}..{b}")]
            out += [f"{stem}_{k:0{len(a)}d}.png" for k in range(int(a), int(b) + 1)]
        elif "<x>_<y>" in p:
            out += [p.replace("<x>_<y>", f"{x}_{y}") for y in range(6) for x in range(6)]
        else:
            out.append(p)
    return out


def job_record(path, **fields):
    """Set fields of a job file in its own layout: only that key's text is
    inserted or replaced, so the rest of the file is byte for byte as it was.
    "pack" merges into the sorted list of pack files; "runs" appends."""
    raw = open(path).read()
    d = json.loads(raw)
    second = raw.split("\n")[1] if "\n" in raw else ""
    pad = " " * ((len(second) - len(second.lstrip(" "))) or 1)
    for k, v in fields.items():
        if k == "pack":
            v = sorted(set(d.get("pack", [])) | {x for x in v if x})
        elif k == "runs":
            v = d.get("runs", []) + [v]
        d[k] = v
        text = json.dumps(v, indent=len(pad), ensure_ascii=False).replace("\n", "\n" + pad)
        at = raw.find(f'\n{pad}"{k}": ')
        if at >= 0:                      # replace the value in place
            start = at + len(f'\n{pad}"{k}": ')
            end = start + json.JSONDecoder().raw_decode(raw[start:])[1]
            raw = raw[:start] + text + raw[end:]
        else:                            # a new last key
            close = raw.rstrip().rindex("}")
            raw = raw[:close].rstrip() + f',\n{pad}"{k}": {text}\n' + raw[close:]
    if json.loads(raw) != d:
        sys.exit(f"romeart: refusing to write {path} -- the edit changed something else")
    open(path, "w").write(raw)


def provenance_claims():
    """[(path or glob, kind, why)] for everything the record names."""
    claims = []
    for f in sorted(glob.glob(os.path.join(JOBS, "*.json"))):
        d = load_json(f)
        for p in d.get("pack", []):
            claims.append((p, "job", os.path.basename(f)[:-5]))
    claims += [(f"art/{g}", "recipe", c) for g, c in RECIPES]
    claims += [(f"art/{g}", "source", w) for g, w in SOURCES]
    claims += [(f"art/{g}", "external", "licence beside it") for g in EXTERNAL]
    return claims


def provenance_rebuild():
    """Re-run every deterministic recipe into a copy of the pack; return the
    shipped files whose pixels the rebuild does not reproduce."""
    tmp = tempfile.mkdtemp()
    pk = os.path.join(tmp, "pack")
    shutil.copytree(PACK, pk)
    tiles = os.path.join(pk, "art", "tiles")
    with open(os.devnull, "w") as null:
        out, sys.stdout = sys.stdout, null
        try:
            for zone, d in for_each_set(pk):
                for f in (fills_set, aprons_set, details_set, interiors_set, edgevars_set):
                    f(zone, d)
                shore_set(d)
            dock_tiles(tiles)
            for lay in ("art/layouts/forest96_italia.json", "art/layouts/mountain96.json"):
                for f in glob.glob(os.path.join(compose_layout(load_json(lay), os.path.join(tmp, "lay")), "*.png")):
                    if os.path.basename(f) != "grass.png":
                        shutil.copyfile(f, os.path.join(tiles, os.path.basename(f)))
            edges_into(tiles, [("fields_wheat", "desert"), ("fields_plough", "desert")])
            paintings = os.path.join(tmp, "fields")
            shutil.copytree(os.path.join("art", "fields"), paintings)
            fields_build(pk, paintings)
            cmd_classpicker(argparse.Namespace(pack=pk))
            cmd_combat(argparse.Namespace(pack=pk, ref="assets/kings-bounty"))
            for prefix in ("emperor_traianus", "informant_market", "pontifex_galliae", "siege_galliae"):
                cmd_pingpong(argparse.Namespace(prefix=f"art/portraits/{prefix}", n=8, reverse=False, pack=pk))
            cmd_pingpong(argparse.Namespace(prefix="art/ui/hud_siege", n=4, reverse=True, pack=pk))
            built = {z: zone_build(z) for z in ZONE_CFG}
        finally:
            sys.stdout = out
    bad = [f"art/fields/{z}.png" for z in FIELD_GRADES
           if not same_pixels(os.path.join(paintings, f"{z}.png"), os.path.join("art", "fields", f"{z}.png"))]
    for root, _, files in os.walk(os.path.join(pk, "art")):
        for f in (f for f in files if f.endswith(".png")):
            rel = os.path.relpath(os.path.join(root, f), pk)
            if not same_pixels(os.path.join(pk, rel), os.path.join(PACK, rel)):
                bad.append(rel)
    for z, out_dir in built.items():
        shipped = os.path.join(PACK, "art", "tiles", z)
        if sorted(os.listdir(out_dir)) != sorted(os.listdir(shipped)):
            bad.append(f"art/tiles/{z}/ (a different set of names)")
        bad += [f"art/tiles/{z}/{f}" for f in sorted(os.listdir(out_dir))
                if not same_pixels(os.path.join(out_dir, f), os.path.join(shipped, f))]
    shutil.rmtree(tmp)
    return bad


@command("pingpong", "extend a loop's frames: bounce back (prefix_N.. = N-2..1) or --reverse (N-1..0)",
         A("prefix", help="e.g. art/portraits/siege_galliae (under the pack)"), A("n", type=int, help="the loop's frames"),
         A("--reverse", action="store_true"), A("--pack", default=PACK))
def cmd_pingpong(a):
    """A loop played forward and back without repeating its ends (the
    portraits' 8 frames to 14), or played forward then reversed whole (the
    HUD's 4 to 8), written as further numbered frames."""
    order = list(range(a.n - 1, -1, -1)) if a.reverse else list(range(a.n - 2, 0, -1))
    width = 2
    for k, src in enumerate(order, start=a.n):
        shutil.copyfile(os.path.join(a.pack, f"{a.prefix}_{src:0{width}d}.png"),
                        os.path.join(a.pack, f"{a.prefix}_{k:0{width}d}.png"))
    print(f"{a.prefix}: frames {a.n:02d}..{a.n + len(order) - 1:02d}")


def provenance_setup(p):
    p.add_argument("action", choices=("check", "normalise", "list"),
                   help="check: every file claimed, every claim present; normalise: fill the jobs' "
                        "\"pack\" lists from _pack_path; list: each file and what claims it")
    p.add_argument("--rebuild", action="store_true", help="check: also re-run the deterministic recipes")


@command("provenance", "the record of how every shipped art file is made", setup=provenance_setup)
def cmd_provenance(a):
    from fnmatch import fnmatch
    if a.action == "normalise":
        n = 0
        for f in sorted(glob.glob(os.path.join(JOBS, "*.json"))):
            d = load_json(f)
            want = sorted(p for p in set(pack_paths(d.get("_pack_path", ""))) if os.path.exists(os.path.join(PACK, p)))
            if want and d.get("pack") != want:
                job_record(f, pack=want)
                n += 1
        print(f"{n} job files given their pack list")
        return 0
    files = sorted(os.path.relpath(os.path.join(r, f), PACK)
                   for r, _, fs in os.walk(os.path.join(PACK, "art")) for f in fs)
    claims = provenance_claims()
    exact = {}
    for p, kind, why in claims:
        if not any(c in p for c in "*?["):
            exact.setdefault(p, []).append((kind, why))
    globs = [(p, kind, why) for p, kind, why in claims if any(c in p for c in "*?[")]
    order = {"job": 0, "recipe": 1, "external": 2, "source": 3}
    owner = {}
    for f in files:
        hits = exact.get(f, []) + [(k, w) for g, k, w in globs if fnmatch(f, g)]
        if hits:
            owner[f] = min(hits, key=lambda h: order[h[0]])
    if a.action == "list":
        for f in files:
            k, w = owner.get(f, ("NONE", ""))
            print(f"{k:8s} {f}  {w}")
        return 0
    bad = [f"unclaimed: {f}" for f in files if f not in owner]
    bad += [f"missing: {p} (claimed by {kind} {why})" for p, hits in exact.items()
            for kind, why in hits if p not in set(files)]
    bad += [f"matches nothing: art/{g} ({kind})" for g, kind, _ in globs if not any(fnmatch(f, g) for f in files)]
    if a.rebuild:
        bad += [f"rebuild differs: {f}" for f in sorted(set(provenance_rebuild()))]
    by = {}
    for k, _ in owner.values():
        by[k] = by.get(k, 0) + 1
    print(f"provenance: {len(files)} files -- " + ", ".join(f"{k} {by[k]}" for k in sorted(by, key=order.get)) +
          (" -- recipes rebuilt" if a.rebuild and not any(b.startswith("rebuild") for b in bad) else ""))
    for b in bad[:60]:
        print("  " + b)
    if len(bad) > 60:
        print(f"  ... and {len(bad) - 60} more")
    return 1 if bad else 0


# ==========================================================================
# PAID (NETWORK): Retro Diffusion, the one generator
# ==========================================================================
#
# Nothing above this line reaches the network. A job is art/jobs/<id>.json;
# `rdgen run <job>` quotes the cost and stops there, `--run` submits and is
# charged. Every run gets a fresh build/art/<id>/runNN (generations are paid
# and not reproducible: the same prompt and seed have given materially
# different results), its task id written BEFORE polling (a lost id is a paid
# result that cannot be fetched), every returned image kept, and the run
# recorded in the job's "runs". cost, balance and reprocess charge nothing.
# The token is read from a file, never the environment or the command line.

RD_API = "https://api.retrodiffusion.ai/v1"
RD_TOKEN = os.path.expanduser("~/.config/retrodiffusion/token")
RD_SENT = ("seed", "remove_bg", "tile_x", "tile_y", "frames_duration", "return_spritesheet", "input_palette",
           "strength", "bypass_prompt_expansion", "return_non_bg_removed")
# Every key a job may carry: "_" keys are notes, never sent; anything else is
# REFUSED, not ignored -- a job once carried a key this did not forward, and a
# paid call went out that could not answer the question it was made for.
RD_KEYS = {"id", "prompt", "style", "width", "height", "target", "raw_only", "figure", "headroom_rows",
           "input_image_path", "input_palette_path", "input_image_keep_alpha", "reference_image_paths",
           "pad_to", "num_images", "pack", "runs"} | set(RD_SENT)
# The line between an attack and a walk, measured over the first 25 runs:
# attacks grew the silhouette 28-34% over frame 0, walks under 7%.
MOTION_GATE_PCT = 25


def rd_token(path):
    if not os.path.exists(path):
        sys.exit(f"rdgen: no token at {path}: write your rdpk- key there, chmod 600")
    tok = open(path).read().strip()
    if not tok.startswith("rdpk-"):
        sys.exit(f"rdgen: {path} does not look like an rdpk- key")
    return tok


def rd_api(token, method, path, payload=None, timeout=60):
    import urllib.error
    import urllib.request
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(RD_API + path, data=data, method=method)
    req.add_header("X-RD-Token", token)
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        try:
            return json.loads(body)
        except ValueError:
            return {"detail": {"code": str(e.code), "message": body[:400]}}


def rd_job(path):
    job = load_json(path)
    missing = [k for k in ("id", "prompt", "style", "width", "height", "target") if k not in job]
    unknown = sorted(k for k in job if not k.startswith("_") and k not in RD_KEYS)
    if missing or unknown:
        sys.exit(f"rdgen: {path}: " + (f"missing {', '.join(missing)}; " if missing else "") +
                 (f"unknown key(s) {', '.join(unknown)} -- forward them or prefix them with '_'" if unknown else ""))
    job["_path"] = path
    return job


def b64png(im):
    import base64
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return base64.b64encode(buf.getvalue()).decode()


def rd_payload(job, check_cost=False):
    """The request a job describes. input_palette_path is NOT a colour
    anchor: it hard-constrains the whole output and collapses its structure
    (measured twice) -- name a colour in the prompt instead. input_image is
    flattened on white (the API wants RGB) unless input_image_keep_alpha;
    pad_to sets the still on a larger clear canvas, centred and standing on
    the bottom, so a figure has room to move (a sprite filling its frame
    animates badly). reference_images steer style and identity, unredrawn."""
    p = {"prompt": job["prompt"], "prompt_style": job["style"], "width": job["width"],
         "height": job["height"], "num_images": job.get("num_images", 1)}
    p.update({k: job[k] for k in RD_SENT if k in job})
    if job.get("input_palette_path"):
        p["input_palette"] = b64png(Image.open(job["input_palette_path"]).convert("RGB"))
    if job.get("input_image_path"):
        src = rgba(job["input_image_path"])
        if job.get("pad_to"):
            pw, ph = (job["pad_to"],) * 2 if isinstance(job["pad_to"], int) else job["pad_to"]
            if pw < src.width or ph < src.height:
                sys.exit(f"rdgen: pad_to {pw}x{ph} is smaller than the source {src.width}x{src.height}")
            canvas = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
            canvas.alpha_composite(src, ((pw - src.width) // 2, ph - src.height))
            src = canvas
        if not job.get("input_image_keep_alpha"):
            flat = Image.new("RGB", src.size, (255, 255, 255))
            flat.paste(src, (0, 0), src)
            src = flat
        p["input_image"] = b64png(src)
    if job.get("reference_image_paths"):
        p["reference_images"] = [b64png(rgba(r)) for r in job["reference_image_paths"]]
    p["check_cost" if check_cost else "async"] = True
    return p


def rd_frames(raw_p, job, work):
    """An animation result split into frame_NN.png: the grid DERIVED from the
    sheet (4 frames are 2x2, 6 are 3x2 -- a hard-coded 2x2 once sliced every
    6-frame run through the middle), a GIF taken as it is."""
    from PIL import ImageSequence
    im = Image.open(raw_p)
    if getattr(im, "n_frames", 1) > 1:
        frames = [f.convert("RGBA") for f in ImageSequence.Iterator(im)]
    else:
        w, h = job["width"], job["height"]
        if im.width % w or im.height % h:
            sys.exit(f"rdgen: sheet {im.size} is not a whole number of {w}x{h} cells")
        cols, rows = im.width // w, im.height // h
        if cols * rows < 2:
            return []
        im = im.convert("RGBA")
        frames = [im.crop((c * w, r * h, c * w + w, r * h + h)) for r in range(rows) for c in range(cols)]
    for i, f in enumerate(frames):
        f.save(os.path.join(work, f"frame_{i:02d}.png"))
    return frames


def rd_finish(token, job, work):
    """Everything after the raw (run and reprocess alike): the frames and
    their motion, the final image, the QA, the contact sheet."""
    raw_p = os.path.join(work, "01_raw.png")
    raw = rgba(raw_p)
    frames = rd_frames(raw_p, job, work)
    if frames:
        widths = [(f.getbbox() or (0, 0, 0, 0))[2] - (f.getbbox() or (0, 0, 0, 0))[0] for f in frames]
        growth = round(100 * (max(widths) - (widths[0] or 1)) / (widths[0] or 1))
        print(f"frames {len(frames)} -> {work}/frame_00..{len(frames) - 1:02d}.png\n  silhouette widths {widths}\n"
              f"  [{'PASS' if growth >= MOTION_GATE_PCT else 'FAIL'}] motion {growth}% over frame 0 (gate {MOTION_GATE_PCT}%)")
    tw, th = job["target"]
    if job.get("raw_only"):
        print("  raw_only: delivering the generated image untouched")
        final = raw
    else:
        stage = raw
        if raw.size != (tw, th):
            # resample only when the raw is larger: resampling a raw already at
            # target size turned a clean 31-colour sprite into 779 colours
            if job.get("figure", True):
                stage = rd_fill(stage, min(raw.width, tw * 2), min(raw.height, th * 2), job.get("headroom_rows", 0))
                stage.save(os.path.join(work, "02_filled.png"))
            stage = rd_downscale(token, stage, tw, th, work)
            stage.save(os.path.join(work, "03_downscaled.png"))
        final = stage.copy()
        px = final.load()
        for y in range(final.height):                 # binary alpha: soft edges halo on unknown ground
            for x in range(final.width):
                r, g, b, al = px[x, y]
                px[x, y] = (r, g, b, 255 if al >= 128 else 0)
        final.save(os.path.join(work, "04_final.png"))
    rows = rd_qa(final, job)
    print(f"\nQA  {job['id']}")
    for name, val, good in rows:
        print(f"  [{'PASS' if good else 'FAIL'}] {name:<20} {val}")
    sheet = Image.new("RGBA", (sum(tw * z + 10 for z in (1, 3, 8)) * 3 + 10, th * 8 + 30), (24, 24, 28, 255))
    x = 10
    for g in ("grass", "forest", "desert"):          # the check that cannot be automated: on real ground
        ground = rgba(os.path.join(PACK, "art", "tiles", f"{g}.png"))
        for z in (1, 3, 8):
            cell = ground.resize((tw, th), Image.NEAREST)
            cell.alpha_composite(final)
            sheet.paste(cell.resize((tw * z, th * z), Image.NEAREST), (x, 20))
            x += tw * z + 10
    sheet.save(os.path.join(work, "contact_sheet.png"))
    print(f"\ncontact sheet: {work}/contact_sheet.png\nfinal: {work}/{'01_raw' if job.get('raw_only') else '04_final'}.png")
    print(f"\nverdict: {'PASS' if all(ok for _, _, ok in rows) else 'NEEDS WORK'} "
          f"(metrics only -- look at the contact sheet before accepting)")


def rd_fill(im, tw, th, headroom=0):
    """Crop to content and scale so the figure fills the height: feet on the
    bottom row, `headroom` rows clear above (asking the model never worked)."""
    a = im.getchannel("A").point(lambda v: 255 if v > 12 else 0)
    bb = a.getbbox()
    if not bb:
        return im.resize((tw, th), Image.NEAREST)
    fig = im.crop(bb)
    avail = th - headroom
    nw = max(1, min(tw, round(fig.width * avail / fig.height)))
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    out.paste(fig.resize((nw, avail), Image.LANCZOS), ((tw - nw) // 2, headroom))
    return out


def rd_downscale(token, im, tw, th, work):
    """The free k-centroid downscale (a nearest resample turns to mush),
    falling back to a local Lanczos."""
    import base64
    r = rd_api(token, "POST", "/edit/tools/k_centroid_downscale", {"input_image": b64png(im), "width": tw, "height": th}, 90)
    save_json(os.path.join(work, "kcentroid_response.json"), {k: v for k, v in r.items() if k != "base64_images"})
    if not r.get("base64_images"):
        print(f"  ! k_centroid failed ({str(r.get('detail'))[:80]}); falling back to local LANCZOS")
        return im.resize((tw, th), Image.LANCZOS)
    return Image.open(io.BytesIO(base64.b64decode(r["base64_images"][0]))).convert("RGBA")


def rd_qa(im, job):
    """The done-criteria, measured. A figure leaves row 0 clear and stands on
    the bottom row: requiring full height rewarded a crest clipped by the top."""
    tw, th = job["target"]
    px = im.load()
    w, h = im.size
    opaque = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] == 255]
    partial = sum(1 for y in range(h) for x in range(w) if 0 < px[x, y][3] < 255)
    colours = {px[x, y][:3] for (x, y) in opaque}
    xs, ys = [p[0] for p in opaque] or [0], [p[1] for p in opaque] or [0]
    rows = [("dimensions", f"{w}x{h}", (w, h) == (tw, th)), ("partial-alpha px", partial, partial == 0),
            ("unique colours", len(colours), 12 <= len(colours) <= 40)]
    if job.get("figure", True):
        fw, fh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        rows += [("top row clear", f"starts row {min(ys)}", min(ys) >= 1), ("fills height", f"{fh}/{th} rows", fh >= th - 3),
                 ("silhouette width", f"{fw}/{tw} cols", fw >= 30), ("feet on bottom row", max(ys) == h - 1, max(ys) == h - 1)]
    return rows


def rd_run_dir(job_id, new):
    base = os.path.join("build", "art", job_id)
    runs = sorted(r for r in os.listdir(base) if r.startswith("run")) if os.path.isdir(base) else []
    if not new:
        return os.path.join(base, runs[-1]) if runs else None
    n = 1
    while os.path.exists(os.path.join(base, f"run{n:02d}")):
        n += 1
    os.makedirs(os.path.join(base, f"run{n:02d}"))
    return os.path.join(base, f"run{n:02d}")


def rdgen_setup(p):
    p.add_argument("--token-file", default=RD_TOKEN, help=f"the rdpk- key's file (default {RD_TOKEN})")
    sub = p.add_subparsers(dest="action", required=True)
    sub.add_parser("balance", help="the credit left")
    sub.add_parser("cost", help="what a job costs (no charge)").add_argument("job")
    r = sub.add_parser("run", help="quote, and with --run submit (PAID)")
    r.add_argument("job")
    r.add_argument("--run", action="store_true", help="submit and be charged; without it only the quote")
    sub.add_parser("reprocess", help="redo frames, transform and QA from the latest raw (no charge)").add_argument("job")


@command("rdgen", "Retro Diffusion generation: cost | run [--run] (PAID) | reprocess | balance", setup=rdgen_setup)
def cmd_rdgen(a):
    import time
    tok = rd_token(a.token_file)
    if a.action == "balance":
        print(json.dumps(rd_api(tok, "GET", "/inferences/credits")))
        return
    job = rd_job(a.job)
    if a.action == "reprocess":
        work = rd_run_dir(job["id"], new=False)
        if not work or not os.path.exists(os.path.join(work, "01_raw.png")):
            sys.exit(f"rdgen: no saved raw for {job['id']}; run it first")
        print(f"reprocessing {job['id']} from {work} (no charge)")
        rd_finish(tok, job, work)
        return
    cost = rd_api(tok, "POST", "/inferences", rd_payload(job, check_cost=True))
    if a.action == "cost":
        print(f"{job['id']}: ${cost.get('balance_cost')}  (remaining ${cost.get('remaining_balance')})")
        return
    print(f"cost ${cost.get('balance_cost')}  balance ${cost.get('remaining_balance')}")
    if not a.run:
        print("dry run; nothing submitted (add --run to submit and be charged)")
        return
    work = rd_run_dir(job["id"], new=True)    # made only for a real submission
    shutil.copy(job["_path"], os.path.join(work, "job.json"))
    payload = rd_payload(job)
    save_json(os.path.join(work, "request.json"), {k: v for k, v in payload.items() if k != "input_image"})
    sub = rd_api(tok, "POST", "/inferences", payload)
    save_json(os.path.join(work, "task.json"), sub)          # before any polling
    task = sub.get("task_id")
    if not task:
        sys.exit(f"rdgen: submit failed: {json.dumps(sub)[:300]}")
    print(f"run dir {work}\ntask {task} (saved to {work}/task.json)")
    result = None
    for i in range(120):
        time.sleep(4)
        s = rd_api(tok, "GET", f"/inferences/tasks/{task}")
        if s.get("status") in ("succeeded", "failed"):
            save_json(os.path.join(work, "poll.json"), {k: v for k, v in s.items() if k != "result"})
            result = s
            print(f"[{(i + 1) * 4}s] {s['status']}")
            break
    if not result or result.get("status") != "succeeded":
        sys.exit(f"rdgen: job did not succeed: {json.dumps(result)[:300]}")
    res = result.get("result") or {}
    imgs = res.get("base64_images") or []
    if not imgs:
        sys.exit(f"rdgen: no image returned: {json.dumps(res)[:300]}")
    import base64
    print(f"charged ${res.get('balance_cost')}  request {res.get('request_id')}")
    for i, b in enumerate(imgs, start=1):     # EVERY image: a second one is paid for too
        p = os.path.join(work, "01_raw.png" if i == 1 else f"01_raw_{i}.png")
        open(p, "wb").write(base64.b64decode(b))
        if i > 1:
            print(f"also returned: {p}")
    job_record(job["_path"], runs={"run": os.path.basename(work), "request": res.get("request_id"),
                                   "cost": res.get("balance_cost"), "date": time.strftime("%Y-%m-%d")})
    rd_finish(tok, job, work)


# ==========================================================================
# main
# ==========================================================================

def main(argv=None):
    ap = argparse.ArgumentParser(prog="romeart.py", description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="command", required=True, metavar="<command>")
    for name, (f, help, args, setup) in COMMANDS.items():
        p = sub.add_parser(name, help=help, description=(f.__doc__ or help).strip(),
                           formatter_class=argparse.RawDescriptionHelpFormatter)
        for flags, kw in args:
            p.add_argument(*flags, **kw)
        if setup:
            setup(p)
        p.set_defaults(_run=f)
    a = ap.parse_args(argv)
    return a._run(a) or 0


if __name__ == "__main__":
    sys.exit(main())
