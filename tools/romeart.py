#!/usr/bin/env python3
"""Rome art: the one tool for every step the pack's art goes through.

    python3 tools/romeart.py <command> [args]

    zone <zone>            build a continent's whole tile set from its primitives
    install <zone>         copy a built set into the pack and list it in game.json
    sheet <zone>           a review page of every tile in a set, at 1:1
    slots <sprites> <out>  search rock-slot arrangements for a mountain lattice
    rebank <old> <new> <dir> <prefix>  river bands carried onto a new interior
    fieldgrade <src> <out> --window x,y,w,h ...  a field painting derived from another
    bridge <tiles> <out>   river bridges from a set's installed road and river tiles
    icon [outdir]          the launcher icon, from the title art (128/512/1024)
    prompts                rebuild docs/ROME-ART.md from art/jobs/*.json

    grass <set> <out> ...  the base grass and its variants
    stitch <set> <terrain> <out>      a PixelLab corner set into 96 px tiles
    edges [pack] [tile-set]           the terrain edges over that set's bases
    fills [pack]                      inner-corner fills for woods and ranges
    lattice <out.json> --sprites ...  a forest/mountain layout (border contract)
    seamcheck <layout.json>           check a layout against that contract
    compose <layout.json> <out>       the layout's tiles, sprites over ground
    sweep <set> <out> --sweep ...     road / river / bridge pieces
    mouth <coast> <river_ew> <grass> <sea> <out>
    tile2x2 <in> <out>                lay a 48 px tile 2x2 into 96
    mirror <in> <out> <half>          mirror half a tile over the other
    crop <in> <out> [size] [top]      centre-crop a still to the pack size

    siegeslice <scene> <out> [--grid | --field <prefix>]
                                      castle picture into siege pieces / cells
    siegewalls [pack] [ref-pack]      the combat wall set from the original pieces
    fieldcalm <painting> <out> <prefix> [--level ..] [--fill ..] [--mask ..]
                                      calm a field painting and cut its cells
    splashlogo <emblem> <out>         the publisher splash
    splashtitle <eagle> <out> | --words <out>   the title screen
    classpicker [pack]                the class-select carousel frames

    loopreview <run-dir> [--scale N]  review page for an animation run
    introtheme <out.ogg> [--length S] [--level L]
                                      the Introduction's theme, synthesised

  Paid (network):
    rdgen cost|run|reprocess <job>    Retro Diffusion generation
    rdgen balance                     the Retro Diffusion credit left
    pltileset <out> <request.json>    one PixelLab create-tileset call
    pltilespro <body.json> <out>      one PixelLab Tiles Pro call
    sprites <job> <out>               a PixelLab rock/tree sprite batch

THE PAID COMMANDS ARE THE ONLY ONES THAT REACH THE NETWORK, and they live in
the last section of this file. rdgen run, pltileset, pltilespro and sprites
spend money: each prints what it would post and what that costs, and posts
only when given --run. rdgen cost and balance, and rdgen reprocess's free
downscale, charge nothing. Every prompt and setting a generation is given is
recorded in art/jobs/*.json and docs/ROME-ART.md.

Every other command only composites what generation returned: it may be
re-run at any time, and the same inputs give the same output (fieldcalm's
fill excepted -- see its notes). Each section below is one step, and each
encodes a contract set by measurement (the lattice border rule, the road
sweep's joining pattern, the edge masks, the grass variants' shared border).
"""
import argparse
import glob
import json
import math
import os
import random
import sys
from collections import deque

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACK = "assets/glory-of-rome"


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
# tile2x2.py -- lay a 48 px tile 2x2 into the 96 px pack tile
# ==========================================================================

def _tile2x2(argv):
    """Lay a 48x48 seamless tile 2x2 into the 96x96 pack tile (ART-PIPELINE, base terrain).

    python3 tools/romeart.py tile2x2 build/art/grass/run01/01_raw.png build/art/grass/run01/01_96.png
    """

    src = Image.open(argv[1]).convert("RGBA")
    w, h = src.size
    out = Image.new("RGBA", (w * 2, h * 2))
    for y in (0, h):
        for x in (0, w):
            out.paste(src, (x, y))
    out.save(argv[2])
    print(argv[2], out.size)


# ==========================================================================
# mirrorhalf.py -- mirror one half of a tile over the other
# ==========================================================================

def _mirrorhalf(argv):
    """Make a tile symmetric by mirroring one half over the other.

    python3 tools/romeart.py mirror in.png out.png bottom|top|left|right

The named half is kept and its mirror replaces the opposite half, so the
result is exactly symmetric about the tile's centre line. Used on the Rome
bridge tiles (2026-09-07) whose generated kerbs were thicker on one side.
    """

    src, dst, keep = argv[1], argv[2], argv[3]
    im = Image.open(src)
    w, h = im.size
    if keep in ("bottom", "top"):
        half = im.crop((0, h // 2, w, h)) if keep == "bottom" else im.crop((0, 0, w, h // 2))
        mirror = half.transpose(Image.FLIP_TOP_BOTTOM)
        im.paste(half, (0, h // 2) if keep == "bottom" else (0, 0))
        im.paste(mirror, (0, 0) if keep == "bottom" else (0, h // 2))
    else:
        half = im.crop((w // 2, 0, w, h)) if keep == "right" else im.crop((0, 0, w // 2, h))
        mirror = half.transpose(Image.FLIP_LEFT_RIGHT)
        im.paste(half, (w // 2, 0) if keep == "right" else (0, 0))
        im.paste(mirror, (0, 0) if keep == "right" else (w // 2, 0))
    im.save(dst)


# ==========================================================================
# cropcentre.py -- centre-crop a still to the pack size
# ==========================================================================

def _cropcentre(argv):
    """Centre-crop a generated still to the pack size.

    python3 tools/romeart.py crop in.png out.png [size] [top]

The rd_pro__default engine draws a painted frame round most 96x96 portraits.
Generating at 128x128 and keeping the centre 96x96 discards up to 16px of
frame on each side with no resampling. Approved for villain portraits only
(2026-09-07), and for the eight town portraits (2026-09-13).
    """

    src, dst = argv[1], argv[2]
    size = int(argv[3]) if len(argv) > 3 else 96
    im = Image.open(src)
    x = (im.width - size) // 2
    y = (im.height - size) // 2
    # An optional 4th argument pins the crop's top row instead of centring it
    # vertically, so a head near the top is kept and only the bottom is cut.
    if len(argv) > 4:
        y = int(argv[4])
    im.crop((x, y, x + size, y + size)).save(dst)


# ==========================================================================
# rivermouth.py -- a river mouth: the river's water blended into the sea
# ==========================================================================

def _rivermouth(argv):
    """A river mouth: a coast tile with a river running into its sea, the river's
water blending into the sea's across the tile.

    python3 tools/romeart.py mouth <coast.png> <river_ew.png> <grass.png> <sea.png> <out.png>

The river enters from the WEST edge (a coast with its sea to the east). The
band is the river_ew piece's own shape -- every pixel where that piece differs
from the plain grass it was swept on -- so it joins the river_ew beside it
exactly. Across the tile each band pixel is mixed from the river piece's colour
at the west edge to the sea tile's colour at the east edge, on a smoothstep.

Processing generated art: Dan's one-off exception (2026-09-16), approved on the
river map mockup, because no generator gave a river-to-sea blend (two
create-tileset calls both drew a shoreline between the waters).
    """


    coast_p, river_p, grass_p, sea_p, out_p = argv[1:6]
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


# ==========================================================================
# tileedges.py -- composite the terrain edge tiles from the bases
# ==========================================================================

def _tileedges(argv):
    """Composite the 48 terrain edge tiles from the finished base tiles.

For each terrain T in water/forest/mountain/desert and variant 01..12, the
reference edge 
(the original 48x34 tiles, kept under art/reference/edges/ at the repo root, water numbered 00-11 and the rest 01-12 as the pack codes them) is read as a shape: each pixel is terrain or grass by which base's colours it
is nearest. That mask is resized to the pack tile size and filled with the
new T base where it is terrain and the new grass base elsewhere, so the
edges seam with their bases by construction (ART-PIPELINE, terrain edges;
OPENBOUNTY-SPEC REQ-229). No generation.

    python3 tools/romeart.py edges [pack-dir] [tile-set] [--as <art>=<terrain> ...]

Default pack assets/glory-of-rome; with a tile set the bases are read from
and the edges written to art/tiles/<set>/. --as builds ONLY another base's
edges, from a reference terrain's shapes (fields_wheat=desert: a wheat field
fading into grass the way sand does), writing <art>_edge_01..12; the
terrains' own edges are left alone.
    """


    AS = [argv[i + 1].split("=", 1) for i, a in enumerate(argv) if a == "--as"]
    pos = [a for i, a in enumerate(argv) if not a.startswith("--") and (i == 0 or argv[i - 1] != "--as")]
    PACK = pos[1] if len(pos) > 1 else "assets/glory-of-rome"
    SET = pos[2] if len(pos) > 2 else ""
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
        if AS:
            for art, ref in AS:
                base = Image.open(os.path.join(TILES, f"{art}.png")).convert("RGBA")
                t_cols = colours(os.path.join(REF, f"{ref}.png"))
                for name in sorted(os.listdir(REF)):
                    if not name.startswith(f"{ref}_edge_"):
                        continue
                    m = mask_from(os.path.join(REF, name), t_cols, g_cols, base.size)
                    out = grass.copy()
                    out.paste(base, (0, 0), m)
                    out.save(os.path.join(TILES, name.replace(f"{ref}_edge_", f"{art}_edge_")))
                    n += 1
            print(f"wrote {n} edge tiles to {TILES}")
            return
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

    main()




# ==========================================================================
# fills.py -- inner-corner fills for woods and ranges
# ==========================================================================

def _fills(argv):
    """Inner-corner fills (#63): where a grass (or sand) cell has a wood or
range on two adjacent sides, the shell draws a fill of that terrain's own
trees or rocks into the cell's corner, so a concave corner rounds off and a
staircase reads as a slope.

A fill is the set's own lattice continued into the cell: the whole sprites of
the plain tile's arrangement whose ink centre lies in the cell within 50 px of
the corner, drawn on a 288 px canvas with the cell in the middle (the shell
draws it one cell up and left), so it joins the neighbours' trees and rocks
and no sprite is cut. A set whose sprites are not kept (Italia's forest)
takes the plain tile's own pixels within a ragged radius of the corner.

    python3 tools/romeart.py fills [pack-dir]

Writes art/tiles/[<set>/]<terrain>[_sand]_fill_<ne|nw|se|sw>.png.
    """
    import math, os, random, tempfile
    from PIL import Image
    PACK = argv[1] if len(argv) > 1 else "assets/glory-of-rome"
    TILES = os.path.join(PACK, "art", "tiles")
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    CORNER = {"se": (96, 96), "ne": (96, 0), "sw": (0, 96), "nw": (0, 0)}
    R = 50
    n = 0

    def lattice_for(zone, t):
        prim = os.path.join(ROOT, "art", "primitives", zone)
        spr = os.path.join(prim, "trees" if t == "forest" else "rocks")
        if not os.path.isdir(spr):
            return None, None
        args = ["romeart", "", "--sprites", spr, "--name", t]
        if t == "forest":
            args += ["--crown", "1" if zone == "oriens" else "0"]
        else:
            args += ["--terrain", "mountain"]
            sl = os.path.join(prim, "rock_slots.json")
            if os.path.exists(sl):
                args += ["--slots", sl]
        tmp = tempfile.NamedTemporaryFile(suffix=".json", delete=False).name
        args[1] = tmp
        import contextlib, io
        with contextlib.redirect_stdout(io.StringIO()):
            _forestlattice(args)
        lay = json.load(open(tmp))
        os.remove(tmp)
        return lay["tiles"][t]["sprites"], spr

    for s in ["", "galliae", "africa", "oriens"]:
        d = os.path.join(TILES, s)
        zone = s or "italia"
        for t in ("forest", "mountain"):
            sprites, spr = lattice_for(zone, t)
            for fam, ground in (("", "grass"), ("_sand", "desert")):
                if not os.path.exists(os.path.join(d, f"{ground}.png")):
                    continue
                if fam and not os.path.exists(os.path.join(d, f"{t}_sand_edge_01.png")):
                    continue
                for corner, (cx, cy) in CORNER.items():
                    out = Image.new("RGBA", (288, 288), (0, 0, 0, 0))
                    if sprites:
                        keep = []
                        for (i, x, y) in sprites:
                            im = Image.open(os.path.join(spr, f"tile_{i:02d}.png")).convert("RGBA")
                            l, tp, r, b = im.getbbox()
                            mx, my = x + (l + r) / 2, y + (tp + b) / 2
                            if 0 <= mx < 96 and 0 <= my < 96 and math.hypot(mx - cx, my - cy) <= R:
                                keep.append((y, x, im))
                        for (y, x, im) in sorted(keep, key=lambda k: (k[0], k[1])):
                            out.alpha_composite(im, (x + 96, y + 96))
                    elif os.path.exists(os.path.join(d, f"{t}{fam}_edge_19.png")):
                        # no sprites kept: the island piece's free-standing
                        # clump (its pixels unlike the ground), set into the
                        # corner with its middle 26 px in from the vertex
                        isl = Image.open(os.path.join(d, f"{t}{fam}_edge_19.png")).convert("RGB")
                        gr = Image.open(os.path.join(d, f"{ground}.png")).convert("RGB")
                        ip, gp_ = isl.load(), gr.load()
                        clump = Image.new("RGBA", isl.size, (0, 0, 0, 0)); cp = clump.load()
                        for yy in range(isl.height):
                            for xx in range(isl.width):
                                if ip[xx, yy] != gp_[xx, yy]:
                                    cp[xx, yy] = ip[xx, yy] + (255,)
                        l, tp, r, b = clump.getbbox()
                        clump = clump.crop((l, tp, r, b))
                        mx = cx - 26 if cx else cx + 26
                        my = cy - 26 if cy else cy + 26
                        out.alpha_composite(clump, (int(mx - clump.width / 2) + 96, int(my - clump.height / 2) + 96))
                    else:
                        plain = Image.open(os.path.join(d, f"{t}.png")).convert("RGBA")
                        rng = random.Random(f"{s}{fam}{t}{corner}")
                        lobes = [rng.uniform(-7, 7) for _ in range(6)]
                        pp, op = plain.load(), out.load()
                        for y in range(96):
                            for x in range(96):
                                dx, dy = x - cx, y - cy
                                a = math.atan2(abs(dy), abs(dx)) / (math.pi / 2) * (len(lobes) - 1)
                                i0 = int(a); f = a - i0
                                rad = 44 + lobes[i0] * (1 - f) + lobes[min(i0 + 1, len(lobes) - 1)] * f
                                if math.hypot(dx, dy) <= rad:
                                    op[x + 96, y + 96] = pp[x, y][:3] + (255,)
                    a_ = out.getchannel("A").point(lambda v: 255 if v > 127 else 0)
                    out.putalpha(a_)
                    out.save(os.path.join(d, f"{t}{fam}_fill_{corner}.png"))
                    n += 1
    print(f"wrote {n} fills")



def _aprons(argv):
    """Aprons (#63): loose rocks and stray trees on the grass along a range's
or a wood's side, so a straight side no longer reads as a cut-out line.

Where a grass or sand cell has a wood or range beside it (north, east, south
or west), the shell draws one of three aprons for that side into the cell, or
none, picked per cell (src/map_render.c draw_aprons). An apron is one to
three of the set's own rocks or trees at half size, straddling the shared
line with most of each sprite on the grass, on a 288 px canvas with the cell
in the middle (drawn one cell up and left, like the inner-corner fills). Its
background is clear, so it stands on grass and sand alike. A set whose tree
sprites are not kept (Italia's forest) takes the island piece's clump.

    python3 tools/romeart.py aprons [pack-dir]

Writes art/tiles/[<set>/]<forest|mountain>_apron_<n|e|s|w>_<1..3>.png.
    """
    import os, random
    from PIL import Image
    PACK = argv[1] if len(argv) > 1 else "assets/glory-of-rome"
    TILES = os.path.join(PACK, "art", "tiles")
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    n = 0

    def whole(im):
        # a sprite drawn to its frame edge (a ledge or crag cut square to
        # straddle a tile line) would stand on the grass cut off
        l, t, r, b = im.getbbox()
        return l > 0 and t > 0 and r < im.width and b < im.height

    def crop(im):
        return im.crop(im.getbbox())

    def half(im):
        return crop(im)       # at full size: a bulge of the mass itself

    for st in ["", "galliae", "africa", "oriens"]:
        d = os.path.join(TILES, st)
        zone = st or "italia"
        for t in ("forest", "mountain"):
            spr = os.path.join(ROOT, "art", "primitives", zone, "trees" if t == "forest" else "rocks")
            if os.path.isdir(spr):
                ims = {int(f[5:7]): Image.open(os.path.join(spr, f)).convert("RGBA")
                       for f in sorted(os.listdir(spr)) if f.startswith("tile_") and f.endswith(".png")}
                if t == "forest":
                    # only the trees the wood itself is made of (its plain tile)
                    args = ["romeart", "", "--sprites", spr, "--name", t,
                            "--crown", "1" if zone == "oriens" else "0"]
                    import contextlib, io, tempfile
                    args[1] = tempfile.NamedTemporaryFile(suffix=".json", delete=False).name
                    with contextlib.redirect_stdout(io.StringIO()):
                        _forestlattice(args)
                    used = {i for (i, _, _) in json.load(open(args[1]))["tiles"][t]["sprites"]}
                    os.remove(args[1])
                    ims = {i: im for i, im in ims.items() if i in used}
                pool = [half(im) for im in ims.values() if whole(im)]
            else:
                # the island piece's free-standing clump: its pixels unlike the grass
                isl = Image.open(os.path.join(d, f"{t}_edge_19.png")).convert("RGB")
                gr = Image.open(os.path.join(d, "grass.png")).convert("RGB")
                ip, gp = isl.load(), gr.load()
                clump = Image.new("RGBA", isl.size, (0, 0, 0, 0)); cp = clump.load()
                for yy in range(isl.height):
                    for xx in range(isl.width):
                        if ip[xx, yy] != gp[xx, yy]:
                            cp[xx, yy] = ip[xx, yy] + (255,)
                pool = [half(clump), half(clump.transpose(Image.FLIP_LEFT_RIGHT))]
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
    print(f"wrote {n} aprons")


def _details(argv):
    """Small detail (#63): now and then a grass or sand cell draws a bush,
a stone or two -- the set's own trees and rocks at half size -- so open
country is not bare cloth. Cosmetic and walkable: the cell stays what it is.

The shell draws detail_<1..4> on about one plain grass or sand cell in
twelve with no wood, range or sea beside it, picked per cell
(src/map_render.c draw_details; `map render --tiles` the same).

    python3 tools/romeart.py details [pack-dir]

Writes art/tiles/[<set>/]detail_<1..4>.png (one cell, 96 px).
    """
    import os, random
    from PIL import Image
    PACK = argv[1] if len(argv) > 1 else "assets/glory-of-rome"
    TILES = os.path.join(PACK, "art", "tiles")
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    def whole(im):
        l, t, r, b = im.getbbox()
        return l > 0 and t > 0 and r < im.width and b < im.height

    def small(im, f):
        im = im.crop(im.getbbox())
        return im.resize((max(1, int(im.width * f)), max(1, int(im.height * f))), Image.NEAREST)

    def sprites(zone, kind):
        d = os.path.join(ROOT, "art", "primitives", zone, kind)
        if not os.path.isdir(d):
            return []
        ims = [Image.open(os.path.join(d, f)).convert("RGBA") for f in sorted(os.listdir(d))
               if f.startswith("tile_") and f.endswith(".png")]
        return [im for im in ims if whole(im)]

    n = 0
    for st in ["", "galliae", "africa", "oriens"]:
        zone = st or "italia"
        d = os.path.join(TILES, st)
        trees = []
        tdir = os.path.join(ROOT, "art", "primitives", zone, "trees")
        if os.path.isdir(tdir):
            # only the trees the set's woods are made of (its plain tile)
            import contextlib, io, tempfile
            tmp = tempfile.NamedTemporaryFile(suffix=".json", delete=False).name
            with contextlib.redirect_stdout(io.StringIO()):
                _forestlattice(["romeart", tmp, "--sprites", tdir, "--name", "forest",
                                "--crown", "1" if zone == "oriens" else "0"])
            used = sorted({i for (i, _, _) in json.load(open(tmp))["tiles"]["forest"]["sprites"]})
            os.remove(tmp)
            trees = [im for im in (Image.open(os.path.join(tdir, f"tile_{i:02d}.png")).convert("RGBA")
                                   for i in used) if whole(im)]
        if not trees:
            # Italia keeps no tree sprites: the island piece's clump
            isl = Image.open(os.path.join(d, "forest_edge_19.png")).convert("RGB")
            gr = Image.open(os.path.join(d, "grass.png")).convert("RGB")
            ip, gp = isl.load(), gr.load()
            c = Image.new("RGBA", isl.size, (0, 0, 0, 0)); cp = c.load()
            for yy in range(isl.height):
                for xx in range(isl.width):
                    if ip[xx, yy] != gp[xx, yy]:
                        cp[xx, yy] = ip[xx, yy] + (255,)
            trees = [c]
        rocks = sprites(zone, "rocks")
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
    print(f"wrote {n} details")


def _edgevars(argv):
    """Variants of each set's mountain west and east sides (#63), so a long
side stops repeating one arrangement tile after tile: mountain_edge_09/10
_v1 and _v2. As the interiors do, a variant keeps every rock that straddles
the tile's lines (the neighbours draw those too, so tiles still join) and
redraws the rocks fully inside the tile, each flipped or nudged a few pixels,
one in three near the open side left out (seeded per set and variant). They
are composed like the edge pieces themselves (compose, with the same contact
shadow). Listed as the codes' `variants`, so the shell picks one per cell
(src/tilevar.c).

    python3 tools/romeart.py edgevars [pack-dir]

Writes art/tiles/[<set>/]mountain_edge_<09|10>_v<1|2>.png.
    """
    import os, random, tempfile, contextlib, io, shutil
    from PIL import Image
    PACK = argv[1] if len(argv) > 1 else "assets/glory-of-rome"
    TILES = os.path.join(PACK, "art", "tiles")
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    n = 0
    for st in ["", "galliae", "africa", "oriens"]:
        zone = st or "italia"
        d = os.path.join(TILES, st)
        prim = os.path.join(ROOT, "art", "primitives", zone)
        spr = os.path.join(prim, "rocks")
        args = ["romeart", "", "--sprites", spr, "--terrain", "mountain", "--name", "mountain",
                "--ragged", "4,20,34,0,28" if zone == "italia" else "6,20,34,12,28"]
        sl = os.path.join(prim, "rock_slots.json")
        if os.path.exists(sl):
            args += ["--slots", sl]
        tmp = tempfile.NamedTemporaryFile(suffix=".json", delete=False).name
        args[1] = tmp
        with contextlib.redirect_stdout(io.StringIO()):
            _forestlattice(args)
        lay = json.load(open(tmp))
        out_tiles = {}
        extra = {}
        for code, side in ((9, "E"), (10, "W")):
            sprites = lay["tiles"][f"mountain_edge_{code:02d}"]["sprites"]
            for v in (1, 2):
                rng = random.Random(f"edgevar-{zone}-{code}-{v}")
                places = []
                for (i, x, y) in sprites:
                    im = Image.open(os.path.join(spr, f"tile_{i:02d}.png")).convert("RGBA")
                    l, tp, r, b = im.getbbox()
                    inside = x + l >= 0 and y + tp >= 0 and x + r <= 96 and y + b <= 96
                    if not inside:
                        places.append([i, x, y]); continue
                    near = (x + l < 30) if side == "W" else (x + r > 66)
                    if near and rng.random() < 0.34:
                        continue
                    j = i
                    if rng.random() < 0.5:
                        j = 100 + i              # the flipped copy
                        extra[j] = im.transpose(Image.FLIP_LEFT_RIGHT)
                        jl, jt, jr, jb = extra[j].getbbox()
                    else:
                        jl, jt, jr, jb = l, tp, r, b
                    nx = x + l - jl + rng.randint(-6, 6)
                    ny = y + tp - jt + rng.randint(-5, 5)
                    nx = max(-jl, min(96 - jr, nx)); ny = max(-jt, min(96 - jb, ny))
                    places.append([j, nx, ny])
                out_tiles[f"mountain_edge_{code:02d}_v{v}"] = {"wrap": "", "sprites": places}
        sd = tempfile.mkdtemp()
        for f in os.listdir(spr):
            if f.startswith("tile_") and f.endswith(".png"):
                shutil.copy(os.path.join(spr, f), sd)
        for j, im in extra.items():
            im.save(os.path.join(sd, f"tile_{j:02d}.png"))
        outd = tempfile.mkdtemp()
        json.dump({"sprites": sd, "grass": os.path.join(d, "grass.png"), "shadow": MOUNTAIN_SHADOW,
                   "tiles": out_tiles}, open(tmp, "w"))
        with contextlib.redirect_stdout(io.StringIO()):
            _treetile(["romeart", tmp, outd])
        for name in out_tiles:
            shutil.copy(os.path.join(outd, name + ".png"), os.path.join(d, name + ".png")); n += 1
        os.remove(tmp); shutil.rmtree(sd); shutil.rmtree(outd)
    print(f"wrote {n} edge variants")

# ==========================================================================
# interiors.py -- plain forest and mountain variants, so a mass is no grid
# ==========================================================================

def _interiors(argv):
    """Two more versions of each set's plain forest and mountain tile (#63),
listed as that code's `variants` so the shell picks one per cell
(src/tilevar.c) and a wood or range stops repeating one arrangement.

A variant keeps the plain tile's sprites that straddle its borders -- the
neighbours draw those too, so tiles still join -- and redraws only the
sprites fully inside the tile, each flipped or nudged a few pixels within
the tile (seeded per set and variant). Italia's forest, whose sprites are not kept, takes the
plain tile's interior mirrored instead. No generation.

    python3 tools/romeart.py interiors [pack-dir]

Writes art/tiles/[<set>/]<terrain>_v1.png and _v2.png.
    """
    import os, random, tempfile, contextlib, io
    from PIL import Image
    PACK = argv[1] if len(argv) > 1 else "assets/glory-of-rome"
    TILES = os.path.join(PACK, "art", "tiles")
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    n = 0
    for s in ["", "galliae", "africa", "oriens"]:
        zone = s or "italia"
        d = os.path.join(TILES, s)
        prim = os.path.join(ROOT, "art", "primitives", zone)
        for t in ("forest", "mountain"):
            spr = os.path.join(prim, "trees" if t == "forest" else "rocks")
            plain_p = os.path.join(d, f"{t}.png")
            if not os.path.exists(plain_p):
                continue
            plain = Image.open(plain_p).convert("RGBA")
            if not os.path.isdir(spr):
                # no sprites: the interior mirrored, its 12 px border kept
                for v, op in ((1, Image.FLIP_LEFT_RIGHT), (2, Image.FLIP_TOP_BOTTOM)):
                    out = plain.copy()
                    inner = plain.crop((12, 12, 84, 84)).transpose(op)
                    out.paste(inner, (12, 12))
                    out.save(os.path.join(d, f"{t}_v{v}.png")); n += 1
                continue
            args = ["romeart", "", "--sprites", spr, "--name", t]
            if t == "forest":
                args += ["--crown", "1" if zone == "oriens" else "0"]
            else:
                args += ["--terrain", "mountain"]
                sl = os.path.join(prim, "rock_slots.json")
                if os.path.exists(sl):
                    args += ["--slots", sl]
            tmp = tempfile.NamedTemporaryFile(suffix=".json", delete=False).name
            args[1] = tmp
            with contextlib.redirect_stdout(io.StringIO()):
                _forestlattice(args)
            lay = json.load(open(tmp)); os.remove(tmp)
            sprites = lay["tiles"][t]["sprites"]
            ims = {}
            def im(i):
                if i not in ims:
                    ims[i] = Image.open(os.path.join(spr, f"tile_{i:02d}.png")).convert("RGBA")
                return ims[i]
            ids = sorted(int(f[5:7]) for f in os.listdir(spr) if f.startswith("tile_") and f.endswith(".png"))
            boxes = {i: im(i).getbbox() for i in ids}
            ground = Image.open(os.path.join(d, "grass.png")).convert("RGBA")
            for v in (1, 2):
                rng = random.Random(f"{zone}-{t}-{v}")
                out = ground.copy()
                for (i, x, y) in sprites:
                    l, tp, r, b = boxes[i]
                    inside = x + l >= 0 and y + tp >= 0 and x + r <= 96 and y + b <= 96
                    if not inside:
                        # straddler: drawn exactly as the plain tile has it
                        can = Image.new("RGBA", (288, 288)); can.alpha_composite(im(i), (x + 96, y + 96))
                        out.alpha_composite(can.crop((96, 96, 192, 192)))
                        continue
                    # the same sprite (the set's art is one family per slot:
                    # swapping in a bush or a cliff left holes), flipped and
                    # nudged so the arrangement no longer repeats
                    j = i
                    sp = im(j)
                    if rng.random() < 0.5:
                        sp = sp.transpose(Image.FLIP_LEFT_RIGHT)
                    jl, jt, jr, jb = sp.getbbox()
                    nx = x + l - jl + rng.randint(-5, 5)
                    ny = y + tp - jt + rng.randint(-4, 4)
                    nx = max(-jl, min(96 - jr, nx)); ny = max(-jt, min(96 - jb, ny))
                    can = Image.new("RGBA", (288, 288)); can.alpha_composite(sp, (nx + 96, ny + 96))
                    out.alpha_composite(can.crop((96, 96, 192, 192)))
                out.save(os.path.join(d, f"{t}_v{v}.png")); n += 1
    print(f"wrote {n} interior variants")

# ==========================================================================
# stitch96.py -- stitch a PixelLab corner tileset into 96 px tiles
# ==========================================================================

def _stitch96(argv):
    """Stitch a PixelLab 16px corner tileset into the pack's 96px terrain tiles.

    python3 tools/romeart.py stitch <set-dir> <terrain> <out-dir> [--seed N] [--invert]
                              [--pool DIR --pool-rate R] [--decor DIR --decor-rate R]

A 96px tile is a 6x6 grid of 16px sub-tiles chosen from the set's corner
tiles, so every pixel is one pixel of art. The set's "lower" is grass and its
"upper" is the terrain; --invert swaps them for a set the model painted the
other way round (the desert set).

Shape rule. A tile's four corners (from the engine's edge-code tables below)
say which neighbours are grass. The terrain FILLS the tile: on a border edge
whose two corners are both grass every vertex is grass, on a mixed edge only
the grass corner itself is grass, and every interior vertex is terrain. So the
terrain reaches the tile edge and grass is only the fringe the neighbour
needs, which is what makes walking up to a shore or a cliff feel close.

Imperfection. Interior vertices next to that fringe flip to grass at random
(seeded), only where at least two orthogonal neighbours are already grass, so
the fringe bulges and nicks instead of running straight. --rough R also lets
a vertex with ONE grass neighbour flip at rate R, which opens bays off a
straight shore; the creep then spreads from the bay, so the coast wanders.
Two passes, so a bay can be two sub-tiles deep. Border vertices never
change: they are shared with the neighbouring tile.

Cliff sets (transition_size 1.0, 25 tiles) carry a third corner value,
"transition": the wall below a south-facing boundary. Wherever terrain sits
over grass in a column the vertex between them becomes the wall, so the
cliff face takes the two sub-tile rows above the fringe. Sub-tiles are chosen
by the set's 4x4 pattern (rows above and below included, 255 = wildcard) as
the API documentation says to do, which is what separates a wall-continuation
tile from its twin.

Crags: in a cliff set, runs of columns along a south fringe bite two rows
deeper at random, so the wall steps up and down instead of running straight.
A step is always two rows or more, because the set has a side piece for a
full-cell face but none for a one-row stagger.

--pool DIR: independent variation tiles (Tiles Pro, full squares of any
whole number of sub-tiles); an all-terrain block is drawn from the pool at
--pool-rate instead of the set's plateau tile. --decor DIR: sprites with
transparency (peaks, boulders) laid over all-terrain blocks at --decor-rate,
never over a wall so they do not sit on the cliff face. --decor-gap is the
clear space kept round each sprite in sub-tiles (1 default; 0 lets them
touch, -1 lets them overlap by one). --decor-cover instead places a sprite at every
terrain position, back to front, so the sprites cover the tile edge to edge.

--sprite-only: no terrain layer at all. The base is the set's grass and the
terrain is only the --decor sprites, laid on one jittered grid with a 96 px
period (--sprite-pitch) shared by every tile of that terrain, drawn back to
front. A sprite that crosses a cell border is drawn in both cells at the same
place, so the cover continues seamlessly from cell to cell. The grass line is
a wobbled arc round each grass corner and a wobbled margin along each grass
edge, both functions of the shared corner and position only, so neighbours
draw the same line and nothing is square.
There is no creep in this mode, so neighbours agree about every border.
--skip-clipped leaves out sprites whose art runs off their own canvas, which
would otherwise show as a straight cut line.

Writes <terrain>.png (all terrain), grass.png (all grass) and
<terrain>_edge_NN.png for every code, plus sheet.png.
    """


    STD = {1: "lllu", 2: "lull", 3: "llul", 4: "ulll", 5: "uuul", 6: "uluu",
           7: "uulu", 8: "luuu", 9: "ulul", 10: "lulu", 11: "lluu", 12: "uull",
           # spits and strips (REQ-229e): given by their OPEN SIDES, not corners
           13: "S:NS", 14: "S:EW", 15: "S:NES", 16: "S:ESW", 17: "S:SWN", 18: "S:WNE", 19: "S:NESW"}
    WATER = {0: "llul", 1: "lllu", 2: "lull", 3: "ulll", 4: "uuul", 5: "uluu",
             6: "uulu", 7: "luuu", 8: "ulul", 9: "lulu", 10: "lluu", 11: "uull",
             12: "S:NS", 13: "S:EW", 14: "S:NES", 15: "S:ESW", 16: "S:SWN", 17: "S:WNE", 18: "S:NESW"}
    VAL = {"l": 0, "u": 1, "t": 2}


    def arg(name, default, conv=str):
        return conv(argv[argv.index(name) + 1]) if name in argv else default


    src, terrain, out = argv[1], argv[2], argv[3]
    seed = arg("--seed", 1, int)
    rough = arg("--rough", 0.0, float)   # bays: a vertex beside a straight fringe creeps to grass at this rate
    invert = "--invert" in argv
    pool_dir, pool_rate = arg("--pool", None), arg("--pool-rate", 0.5, float)
    decor_dir, decor_rate = arg("--decor", None), arg("--decor-rate", 0.2, float)
    decor_gap = arg("--decor-gap", 1, int)
    decor_cover = "--decor-cover" in argv   # every spot, back to front, edge to edge
    sprite_only = "--sprite-only" in argv   # no terrain layer: sprites on grass, seamless across cells
    sprite_pitch = arg("--sprite-pitch", 24, int)
    skip_clipped = "--skip-clipped" in argv   # leave out sprites whose art touches their canvas edge   # sub-tiles kept clear round a sprite; -1 lets them overlap by one
    os.makedirs(out, exist_ok=True)

    meta = json.load(open(os.path.join(src, "tiles_meta.json")))
    SWAP = str.maketrans("lu", "ul")
    setl = []   # (pattern rows as lists of ints, image)
    for i, t in enumerate(meta):
        p = t["pattern_4x4"]
        # the outer columns are the left/right neighbours, wildcard in every tile
        rows = [list(p[f"row_{r}"])[1:3] for r in range(4)]
        if invert:
            rows = [[(1 - v if v in (0, 1) else v) for v in r] for r in rows]
        setl.append((rows, Image.open(os.path.join(src, f"tile_{i:02d}.png")).convert("RGBA")))
    has_wall = any(2 in r[1] + r[2] for r, _ in setl)
    setl_grass = next(im for rows, im in setl if rows[1] == [0, 0] and rows[2] == [0, 0])
    S = setl[0][1].width          # 16
    N = 96 // S                   # 6
    codes = WATER if terrain == "water" else STD
    pool = [Image.open(f).convert("RGBA") for f in sorted(glob.glob(os.path.join(pool_dir, "tile_*.png")))] if pool_dir else []
    decor = [Image.open(f).convert("RGBA") for f in sorted(glob.glob(os.path.join(decor_dir, "tile_*.png")))] if decor_dir else []


    def clipped(im):
        a = im.getchannel("A").load()
        w, h = im.size
        return (any(a[x, 0] for x in range(w)) or any(a[x, h - 1] for x in range(w)) or
                any(a[0, y] for y in range(h)) or any(a[w - 1, y] for y in range(h)))


    if skip_clipped:
        decor = [d for d in decor if not clipped(d)]


    def pick(rows):
        """Best set tile for a 4x4 pattern: rows 1-2 exact, rows 0 and 3 must not
        contradict a non-wildcard, and more non-wildcard agreement wins."""
        best, score = None, -1
        for prow, im in setl:
            if prow[1] != rows[1] or prow[2] != rows[2]:
                continue
            s = 0
            ok = True
            for r in (0, 3):
                for a, b in zip(prow[r], rows[r]):
                    if a == 255:
                        continue
                    if a == b:
                        s += 1
                    else:
                        ok = False
            if ok and s > score:
                best, score = im, s
        if best is None:
            raise SystemExit(f"no tile for pattern {rows}")
        return best


    def sides_vertices(open_sides):
        """A cell given by its open (grass) sides: every vertex on an open side
        is grass, so a corner is grass when either side at it is open, and the
        rest is terrain. A neighbour across a closed side derives the same
        border from its own sides, so the two agree vertex for vertex."""
        v = [["u"] * (N + 1) for _ in range(N + 1)]
        for i in range(N + 1):
            if "N" in open_sides: v[0][i] = "l"
            if "S" in open_sides: v[N][i] = "l"
        for j in range(N + 1):
            if "W" in open_sides: v[j][0] = "l"
            if "E" in open_sides: v[j][N] = "l"
        return v


    def vertices(corners, rng):
        if corners == "llll":
            return [["l"] * (N + 1) for _ in range(N + 1)]
        if corners.startswith("S:"):
            v = sides_vertices(corners[2:])
        else:
            nw, ne, sw, se = corners
            v = [["u"] * (N + 1) for _ in range(N + 1)]   # v[j][i]
            for i in range(N + 1):
                if nw == "l" and ne == "l": v[0][i] = "l"
                if sw == "l" and se == "l": v[N][i] = "l"
            for j in range(N + 1):
                if nw == "l" and sw == "l": v[j][0] = "l"
                if ne == "l" and se == "l": v[j][N] = "l"
            v[0][0], v[0][N], v[N][0], v[N][N] = nw, ne, sw, se
        # Imperfection: interior vertices beside the fringe creep to grass. In a
        # cliff set a creep is only allowed where the vertex above is already
        # grass, so it never opens a new wall of its own.
        for _ in range(2):
            for j in range(1, N):
                for i in range(1, N):
                    if v[j][i] == "u":
                        near = sum(v[jj][ii] == "l" for ii, jj in
                                   ((i - 1, j), (i + 1, j), (i, j - 1), (i, j + 1)))
                        if has_wall and v[j - 1][i] != "l":
                            continue
                        if near >= 2 and rng.random() < 0.35:
                            v[j][i] = "l"
                        elif near == 1 and rough > 0 and rng.random() < rough:
                            v[j][i] = "l"           # a bay opens off a straight shore
        if has_wall:
            # Crags: along a south fringe, runs of columns bite two rows deeper,
            # so the wall steps. A step must be two rows or more: the set has a
            # side piece for a full-cell face but none for a one-row stagger.
            i = 1
            while i < N:
                if v[N][i] == "l" and v[N - 1][i] == "u" and v[N - 3][i] == "u" and rng.random() < 0.3:
                    run = rng.choice((1, 1, 2))
                    for k in range(i, min(i + run, N)):
                        v[N - 1][k] = "l"; v[N - 2][k] = "l"
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


    def cells(v):
        for b in range(N):
            for a in range(N):
                r1 = [VAL[v[b][a]], VAL[v[b][a + 1]]]
                r2 = [VAL[v[b + 1][a]], VAL[v[b + 1][a + 1]]]
                r0 = [VAL[v[b - 1][a]], VAL[v[b - 1][a + 1]]] if b > 0 else list(r1)
                r3 = [VAL[v[b + 2][a]], VAL[v[b + 2][a + 1]]] if b + 2 <= N else list(r2)
                yield a, b, [r0, r1, r2, r3]


    def flat_region(v, a, b, k):
        """True when the k x k sub-tile block at (a, b) is all terrain and the
        row under it is not a wall, so a bigger piece can sit there."""
        if a + k > N or b + k > N:
            return False
        for jj in range(b, b + k + 1):
            for ii in range(a, a + k + 1):
                if v[jj][ii] != "u":
                    return False
        return b + k + 1 > N or all(v[b + k + 1][ii] != "t" for ii in range(a, a + k + 1))


    def region(corners):
        """Terrain region without creep: the fill rule alone, so neighbouring
        cells agree exactly about every border sub-tile."""
        if corners == "llll":
            return [["l"] * (N + 1) for _ in range(N + 1)]
        nw, ne, sw, se = corners
        v = [["u"] * (N + 1) for _ in range(N + 1)]
        for i in range(N + 1):
            if nw == "l" and ne == "l": v[0][i] = "l"
            if sw == "l" and se == "l": v[N][i] = "l"
        for j in range(N + 1):
            if nw == "l" and sw == "l": v[j][0] = "l"
            if ne == "l" and se == "l": v[j][N] = "l"
        v[0][0], v[0][N], v[N][0], v[N][N] = nw, ne, sw, se
        return v


    def in_terrain(v, x, y):
        """Is the point (pixels, may lie outside the cell) on terrain? Outside
        the cell the answer is read off the nearest border sub-tile, which the
        neighbour shares, so both cells draw the same overhang."""
        a = min(max(x // S, 0), N - 1)
        b = min(max(y // S, 0), N - 1)
        return all(v[jj][ii] == "u" for ii in (a, a + 1) for jj in (b, b + 1))


    def master_layout(rng):
        """One sprite layout for the whole terrain, on a jittered grid with a
        96 px period. Every cell draws the same layout, so a sprite crossing a
        cell border is continued exactly by the neighbour."""
        lay = []
        for gy in range(0, 96, sprite_pitch):
            for gx in range(0, 96, sprite_pitch):
                x = gx + rng.randint(-sprite_pitch // 4, sprite_pitch // 4)
                y = gy + rng.randint(-sprite_pitch // 4, sprite_pitch // 4)
                lay.append((x % 96, y % 96, rng.randrange(len(decor))))
        return lay



    # The grass line for sprite terrains is drawn round the tile's grass corners
    # and along its grass edges, wobbled by a fixed wave so it is never straight.
    # Both are functions of the shared corner and of position only, so the two
    # cells either side of a border draw the same line.
    CORNER_R, CORNER_WOBBLE = 40.0, 12.0     # bite round a grass corner, pixels
    EDGE_M, EDGE_WOBBLE = 14.0, 8.0          # margin along a grass edge


    def wobble(t, k):
        return (math.sin(t * 3.0 + k) + 0.5 * math.sin(t * 7.0 + 2.0 * k)) / 1.5


    def terrain_at(corners, x, y):
        nw, ne, sw, se = corners
        if corners == "llll":
            return False
        for (cx, cy, c, k) in ((0, 0, nw, 0.3), (96, 0, ne, 1.7), (0, 96, sw, 2.9), (96, 96, se, 4.1)):
            if c == "l":
                ang = math.atan2(y - cy, x - cx)
                if math.hypot(x - cx, y - cy) < CORNER_R + CORNER_WOBBLE * wobble(ang, k):
                    return False
        per = 2 * math.pi / 96
        if nw == "l" and ne == "l" and y < EDGE_M + EDGE_WOBBLE * wobble(x * per, 0.5):
            return False
        if sw == "l" and se == "l" and 96 - y < EDGE_M + EDGE_WOBBLE * wobble(x * per, 1.5):
            return False
        if nw == "l" and sw == "l" and x < EDGE_M + EDGE_WOBBLE * wobble(y * per, 2.5):
            return False
        if ne == "l" and se == "l" and 96 - x < EDGE_M + EDGE_WOBBLE * wobble(y * per, 3.5):
            return False
        return True


    def build_sprites(corners, lay):
        im = Image.new("RGBA", (96, 96))
        grass = setl_grass
        for b in range(N):
            for a in range(N):
                im.paste(grass, (a * S, b * S))
        d = decor[0].width
        draws = []
        for x, y, k in lay:
            for ox in (-96, 0, 96):
                for oy in (-96, 0, 96):
                    px, py = x + ox, y + oy
                    if px + d <= 0 or py + d <= 0 or px >= 96 or py >= 96:
                        continue
                    # Ragged edge: each sprite has its own reach (0..15 px, fixed by
                    # its slot so both cells agree) and stands if any of five
                    # points within that reach is on terrain.
                    r = (k * 7 + (x + y) * 3) % S
                    cx, cy = px + d // 2, py + d // 2
                    if not any(terrain_at(corners, cx + dx, cy + dy) for dx, dy in
                               ((0, 0), (r, 0), (-r, 0), (0, r), (0, -r))):
                        continue
                    draws.append((py + d, px, py, k))
        for _, px, py, k in sorted(draws):
            im.alpha_composite(decor[k], (px, py))
        return im


    def build(corners, rng):
        # The imperfection pass can ask for a corner combination the set does not
        # carry; then draw the vertices again rather than ship a hole.
        for _ in range(60):
            v = vertices(corners, rng)
            try:
                chosen = [(a, b, pick(rows)) for a, b, rows in cells(v)]
                break
            except SystemExit:
                continue
        else:
            raise SystemExit(f"no layout for {corners}")
        im = Image.new("RGBA", (96, 96))
        for a, b, tile in chosen:
            im.paste(tile, (a * S, b * S))
        covered = set()
        if pool:
            k = pool[0].width // S
            for b in range(N):
                for a in range(N):
                    if any((a + dx, b + dy) in covered for dx in range(k) for dy in range(k)):
                        continue
                    if flat_region(v, a, b, k) and rng.random() < pool_rate:
                        im.paste(rng.choice(pool), (a * S, b * S))
                        covered.update((a + dx, b + dy) for dx in range(k) for dy in range(k))
        if decor and decor_cover:
            # Edge to edge: a sprite at every sub-tile position whose block is
            # terrain, drawn top row first so lower sprites overlap the ones
            # behind them. The base tile only shows where nothing can stand.
            k = decor[0].width // S
            for b in range(N):
                for a in range(N):
                    if flat_region(v, a, b, k):
                        im.alpha_composite(rng.choice(decor), (a * S, b * S))
        elif decor:
            k = decor[0].width // S
            spots = [(a, b) for b in range(N) for a in range(N) if flat_region(v, a, b, k)]
            rng.shuffle(spots)
            taken = set()
            for a, b in spots:
                if rng.random() >= decor_rate:
                    continue
                if any((a + dx, b + dy) in taken for dx in range(-decor_gap, k + decor_gap) for dy in range(-decor_gap, k + decor_gap)):
                    continue
                im.alpha_composite(rng.choice(decor), (a * S, b * S))
                taken.update((a + dx, b + dy) for dx in range(k) for dy in range(k))
        return im


    rng = random.Random(seed)
    if sprite_only:
        lay = master_layout(rng)
        made = {terrain: build_sprites("uuuu", lay), "grass": build_sprites("llll", lay)}
        for code, corners in codes.items():
            made[f"{terrain}_edge_{code:02d}"] = build_sprites(corners, lay)
    else:
        made = {terrain: build("uuuu", rng), "grass": build("llll", rng)}
        for code, corners in codes.items():
            made[f"{terrain}_edge_{code:02d}"] = build(corners, rng)
    for name, im in made.items():
        im.save(os.path.join(out, name + ".png"))
    names = list(made)
    sheet = Image.new("RGBA", (7 * 100, ((len(names) + 6) // 7) * 100), (40, 40, 40, 255))
    for i, n in enumerate(names):
        sheet.paste(made[n], ((i % 7) * 100, (i // 7) * 100))
    sheet.save(os.path.join(out, "sheet.png"))
    print(f"{len(made)} tiles in {out}")


# ==========================================================================
# grassvar.py -- grass variants: the grass with a patch of a second grass
# ==========================================================================

def _grassvar(argv):
    """Grass variants: the pack grass with a patch of a second grass inside.

    python3 tools/romeart.py grass <set-dir> <out-dir> [--count N] [--seed S] [--set DIR ...]

The set is a PixelLab 16 px tileset whose lower terrain is the pack grass
and whose upper is a detail (weeds, pebbles, flowers, dry grass). More
sets, chained to the same grass, may be given with --set; each variant
takes its patches from one or two of them at random. Each variant is a 96 px
tile built like a terrain tile (romeart.py stitch): a 7x7 vertex grid, the
set's corner tile per 2x2. The border vertices are always the lower grass,
so every variant's edges are the plain grass and any two variants, or a
variant and the plain tile, join without a seam. The patch is a random
blob grown from a seed vertex inside the tile, a different shape per
variant, and one variant may carry two small patches.

Writes grass_01.png .. grass_NN.png and sheet.png (a field mixing them).
    """


    src, out = argv[1], argv[2]
    count = int(argv[argv.index("--count") + 1]) if "--count" in argv else 3
    seed = int(argv[argv.index("--seed") + 1]) if "--seed" in argv else 1
    extra = [argv[i + 1] for i, a in enumerate(argv) if a == "--set"]
    # --decor DIR --decor-ids 0,1,2 [--decor-n 2] [--patch-rate 0.5]: small transparent
    # objects (32 px) laid fully inside each variant, never crossing a tile line,
    # on top of the grass; a variant carries a patch only at --patch-rate.
    decor_dir = argv[argv.index("--decor") + 1] if "--decor" in argv else None
    decor_ids = [int(x) for x in argv[argv.index("--decor-ids") + 1].split(",")] if "--decor-ids" in argv else []
    decor_n = int(argv[argv.index("--decor-n") + 1]) if "--decor-n" in argv else 2
    decor_rate = float(argv[argv.index("--decor-rate") + 1]) if "--decor-rate" in argv else 1.0   # share of variants that get objects
    patch_rate = float(argv[argv.index("--patch-rate") + 1]) if "--patch-rate" in argv else 1.0
    # --mottle N: N small one- or two-vertex patches of the first set's upper
    # terrain scattered through EVERY tile, base included, so the ground reads as
    # a soft mottle of two close tones instead of one flat colour
    mottle = int(argv[argv.index("--mottle") + 1]) if "--mottle" in argv else 0
    os.makedirs(out, exist_ok=True)


    def load(d):
        meta = json.load(open(os.path.join(d, "tiles_meta.json")))
        out = []
        for i, t in enumerate(meta):
            p = t["pattern_4x4"]
            rows = [list(p[f"row_{r}"])[1:3] for r in range(4)]
            out.append((rows, Image.open(os.path.join(d, f"tile_{i:02d}.png")).convert("RGBA")))
        return out


    SETS = [load(src)] + [load(d) for d in extra]
    setl = SETS[0]
    S = setl[0][1].width
    N = 96 // S
    VAL = {"l": 0, "u": 1}


    def pick(rows, setl=None):
        setl = setl or SETS[0]
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


    def blob(rng, size):
        """A random connected set of interior vertices (1..N-1), `size` of them."""
        v = [["l"] * (N + 1) for _ in range(N + 1)]
        sx, sy = rng.randint(1, N - 1), rng.randint(1, N - 1)
        cells = {(sx, sy)}
        while len(cells) < size:
            x, y = rng.choice(sorted(cells))
            dx, dy = rng.choice(((1, 0), (-1, 0), (0, 1), (0, -1)))
            nx, ny = x + dx, y + dy
            if 1 <= nx <= N - 1 and 1 <= ny <= N - 1:
                cells.add((nx, ny))
        for x, y in cells:
            v[y][x] = "u"
        return v


    def build(layers):
        """layers: [(vertex grid, set)], drawn in order; a sub-tile whose four
        vertices are all grass in a layer is left to the layers below."""
        im = Image.new("RGBA", (96, 96))
        for b in range(N):
            for a in range(N): im.paste(SETS[0][0][1] if False else grass, (a * S, b * S))
        for v, setl in layers:
            for b in range(N):
                for a in range(N):
                    r1 = [VAL[v[b][a]], VAL[v[b][a + 1]]]
                    r2 = [VAL[v[b + 1][a]], VAL[v[b + 1][a + 1]]]
                    if r1 == [0, 0] and r2 == [0, 0]: continue
                    r0 = [VAL[v[b - 1][a]], VAL[v[b - 1][a + 1]]] if b > 0 else list(r1)
                    r3 = [VAL[v[b + 2][a]], VAL[v[b + 2][a + 1]]] if b + 2 <= N else list(r2)
                    im.paste(pick([r0, r1, r2, r3], setl), (a * S, b * S))
        return im


    rng = random.Random(seed)
    grass = next(im for rows, im in setl if rows[1] == [0, 0] and rows[2] == [0, 0])
    plain = Image.new("RGBA", (96, 96))
    for b in range(N):
        for a in range(N): plain.paste(grass, (a * S, b * S))
    made = {}
    patch_size = int(argv[argv.index("--patch-size") + 1]) if "--patch-size" in argv else 0   # vertices per patch, 0 = the default mix
    sizes = [patch_size] * 6 if patch_size else ([3, 5, 4, 6, 2, 7] if N >= 6 else [1, 2, 3, 2, 1, 4])
    for k in range(count):
        # one patch from one set; every other variant adds a small patch from another set
        a = SETS[k % len(SETS)]
        layers = [(blob(rng, sizes[k % len(sizes)]), a)]
        if len(SETS) > 1 and k % 2 == 1:
            b_ = SETS[(k + 1) % len(SETS)]
            layers.append((blob(rng, 2), b_))
        if rng.random() > patch_rate:
            layers = []
        if mottle:
            v = [["l"] * (N + 1) for _ in range(N + 1)]
            for _ in range(mottle):
                b = blob(rng, rng.choice((1, 1, 2)))
                for yy in range(N + 1):
                    for xx in range(N + 1):
                        if b[yy][xx] == "u": v[yy][xx] = "u"
            layers = [(v, SETS[0])] + layers
        im = build(layers)
        if decor_dir and decor_ids and rng.random() < decor_rate:
            placed = []
            for _ in range(rng.randint(1, decor_n)):
                i = rng.choice(decor_ids)
                sp = Image.open(os.path.join(decor_dir, f"tile_{i:02d}.png")).convert("RGBA")
                bb = sp.getbbox() or (0, 0, sp.width, sp.height)
                for _try in range(20):
                    x = rng.randint(-bb[0], 96 - bb[2]); y = rng.randint(-bb[1], 96 - bb[3])
                    box = (x + bb[0], y + bb[1], x + bb[2], y + bb[3])
                    if all(box[2] <= q[0] or box[0] >= q[2] or box[3] <= q[1] or box[1] >= q[3] for q in placed):
                        im.alpha_composite(sp, (x, y)); placed.append(box); break
        made[f"grass_{k + 1:02d}"] = im
    for name, im in made.items():
        im.save(os.path.join(out, name + ".png"))
    if mottle:
        v = [["l"] * (N + 1) for _ in range(N + 1)]
        for _ in range(mottle):
            b = blob(rng, rng.choice((1, 1, 2)))
            for yy in range(N + 1):
                for xx in range(N + 1):
                    if b[yy][xx] == "u": v[yy][xx] = "u"
        plain = build([(v, SETS[0])])
    plain.save(os.path.join(out, "grass.png"))

    # a field: plain and variants mixed as the shell would, 8x6 cells
    choices = [plain] * len(made) + list(made.values())   # the pack lists the base as often as the variants together
    frng = random.Random(seed + 100)
    field = Image.new("RGBA", (8 * 96, 6 * 96))
    for y in range(6):
        for x in range(8):
            field.paste(frng.choice(choices), (x * 96, y * 96))
    field.save(os.path.join(out, "sheet.png"))
    print(len(made), "variants ->", out)


# ==========================================================================
# forestlattice.py -- forest and mountain layouts under the border contract
# ==========================================================================

def _forestlattice(argv):
    """Forest and mountain tiles from one lattice, under the border contract.

    python3 tools/romeart.py lattice <out.json> --sprites DIR --crown N --name forest
    python3 tools/romeart.py lattice <out.json> --sprites DIR --terrain mountain --name mountain

The contract (2026-09-10), checked mechanically by romeart.py seamcheck:

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

--ragged S,A,B (#63): terminal sides end raggedly instead of in a line. Every
sprite fully inside the tile whose ink comes within A px (B px, alternating)
of an open side's line is taken out, and small sprites S are laid loose in
the gap, covering no straddler, fully inside the tile and 4 px clear of every
line. Straddlers keep their places, so tiles still join (seamcheck).

Codes are the engine's (OPENBOUNTY-SPEC REQ-229a). Output is a layout for
romeart.py compose with wrap off, every sprite listed, negatives included.
    """


    out = argv[1]
    SPR = argv[argv.index("--sprites") + 1]
    NAME = argv[argv.index("--name") + 1] if "--name" in argv else "forest"
    TERRAIN = argv[argv.index("--terrain") + 1] if "--terrain" in argv else "forest"
    GRASS = "assets/glory-of-rome/art/tiles/grass.png"   # the pack grass itself (96, from the 32 px set t32_grass_a, 2026-09-10)

    _bbox = {}
    def bbox(i):
        if i not in _bbox:
            _bbox[i] = Image.open(f"{SPR}/tile_{i:02d}.png").convert("RGBA").getbbox()
        return _bbox[i]


    if TERRAIN == "forest":
        CROWN = int(argv[argv.index("--crown") + 1])
        UPPER = [[CROWN, CROWN], [CROWN, CROWN]]
        LOWER = [[CROWN, CROWN], [CROWN, CROWN]]
        # edge crowns on a terminal west/east side: (sprite, inset from the line, place)
        EDGE_W = [(CROWN, 0, "top"), (CROWN, 12, "mid"), (CROWN, 5, "bottom")]
        EDGE_E = [(CROWN, 8, "top"), (CROWN, 0, "mid"), (CROWN, 14, "bottom")]
        LEDGE = None
    else:
        UPPER = [[6, 0], [7, 1]]      # rocks per slot, the same in every tile; the boulder (0)
        LOWER = [[6, 5], [5, 7]]      # and the crag (1) straddle east: their top and bottom points close the corners
        EDGE_W = [(7, 4, "top"), (3, 12, "mid"), (5, 0, "bottom")]
        EDGE_E = [(3, 0, "top"), (6, 14, "mid"), (0, 6, "bottom")]
        LEDGE = None                  # no cliff slab: its straight bottom and ends squared the outer corners (2026-09-10)
        # --slots FILE: another rock set's own slots, {"upper", "lower", "edge_w",
        # "edge_e"} in the shapes above, since which rock fits a slot depends on
        # its ink box (Galliae's rocks, 2026-09-19).
        if "--slots" in argv:
            _s = json.load(open(argv[argv.index("--slots") + 1]))
            UPPER, LOWER = _s["upper"], _s["lower"]
            EDGE_W = [tuple(e) for e in _s["edge_w"]]
            EDGE_E = [tuple(e) for e in _s["edge_e"]]

    OPEN = {11: "N", 12: "S", 9: "E", 10: "W", 1: "NW", 3: "NE", 2: "SW", 4: "SE",
            5: "", 6: "", 7: "", 8: "", 0: "",
            # spits and strips (2026-09-10, REQ-229e): opposite sides open, three
            # sides open (named by the attached side's opposite), and an island
            13: "NS", 14: "EW", 15: "NES", 16: "ESW", 17: "SWN", 18: "WNE", 19: "NESW"}


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

    LOWER_Y = (36, 58)
    # the lower rows stop short of the west and east lines by these insets
    # (row, side), so no shared sprite ever lies along a west or east line and
    # a terminal side's silhouette is not the line
    LOWER_INSET = ((12, 8), (8, 12)) if TERRAIN != "forest" else ((0, 0), (0, 0))   # a tree closes no corner from its trunk, so its lower rows stay flush


    def period():
        """One 96 period of the lattice: (sprite, x, y, layer). Layer 0 is the
        lower rows (drawn first), 1 the upper rows."""
        pts = []
        # lower rows: inset from the west and east lines by ink box
        for k in range(2):
            for j in range(2):
                i = LOWER[k][j]; l, t, r, b = bbox(i); ins = LOWER_INSET[k][j]
                pts.append((i, -l + ins if j == 0 else 96 - r - ins, LOWER_Y[k], 0))
        # upper top row: ink tops on the north line; the straddler's topmost
        # point sits on the east line, closing the corner from below
        for j in range(2):
            i = UPPER[0][j]; l, t, r, b = bbox(i)
            pts.append((i, 0 if j == 0 else 96 - extreme_col(i, True), -t, 1))
        # upper bottom row: the inside sprite ends 1 px above the south line;
        # the straddler's lowest point sits on the east line at the south line,
        # closing the corner from above (a round-bottomed sprite: short run)
        i = UPPER[1][0]; l, t, r, b = bbox(i)
        pts.append((i, 0, 95 - b, 1))
        i = UPPER[1][1]; l, t, r, b = bbox(i)
        pts.append((i, 96 - extreme_col(i, False), 96 - b, 1))
        return pts


    JITTER = int(argv[argv.index("--jitter") + 1]) if "--jitter" in argv else 1


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
        # Terminal west/east: edge sprites fully inside the tile, each at its
        # own inset and height, so the silhouette is rocks and notches, not the
        # line. On an outer corner the sprite nearest the corner is left out,
        # which cuts the corner back.
        for side, spec in (("W", EDGE_W), ("E", EDGE_E)):
            if side not in open_sides: continue
            for k_, (i, ins, place) in enumerate(spec):
                if RAGGED and place == "mid" and code < 13:
                    continue          # a notch midway: the side is not one wall
                if place == "top" and "N" in open_sides: continue
                if place == "bottom" and "S" in open_sides: continue
                l, t, r, b = bbox(i)
                y = {"top": 1 - t, "mid": 48 - (t + b) // 2, "bottom": 95 - b}[place]
                if RAGGED and code < 13:
                    # set back from the open line, by two depths, so down a
                    # side the outline steps in and out instead of standing
                    # as a wall
                    ins += {"top": RAG_TOP, "mid": 0, "bottom": RAG_BOT}[place]
                x = -l + ins if side == "W" else 96 - r - ins
                x = max(-l, min(96 - r, x))          # fully inside, whatever the inset
                keep.append((i, x, y, 2))
        if LEDGE is not None and "S" in open_sides:
            lL, lT, lR, lB = bbox(LEDGE)
            ly = 96 - lB
            # one slab, fully inside, drawn BEHIND the rocks (it sorts first):
            # the row 12 rocks stand on it and hide the slab ends at the lines.
            # Nothing may be drawn in front of a straddler that the neighbour
            # does not draw too, so the face cannot be in front.
            keep.append((LEDGE, (96 - (lR - lL)) // 2 - lL, ly, -1))
        # strips and spits (two opposite sides open) keep their full rows: thinned
        # from both sides they come apart into a string of beads
        if RAGGED and open_sides and code < 13:
            keep = ragged(keep, open_sides)
        for (i, x, y, _) in keep:
            assert not (crossings(i, x, y) & set(open_sides))
        # Draw order, the same in every tile: the face, the lower rows (which
        # straddle south), then the upper rows (inside or straddling east),
        # then the edge sprites. So nothing that is only drawn on one side of
        # a line ever sits in front of a straddler with an edge on the line.
        keep = sorted(set(keep), key=lambda p: (min(p[3], 0), p[2], p[1]))
        return {"wrap": "", "sprites": [[i, x, y] for (i, x, y, _) in keep]}


    RAGGED = [int(v) for v in argv[argv.index("--ragged") + 1].split(",")] if "--ragged" in argv else None
    # --ragged S,A,B[,T,U]: the west and east edge sprites set back T px (the
    # top one) and U px (the bottom one) from the open line
    RAG_TOP, RAG_BOT = (RAGGED[3:5] if RAGGED and len(RAGGED) >= 5 else (0, 0))
    if RAGGED:
        RAGGED = RAGGED[:3]

    def ragged(keep, open_sides):
        """Terminal sides end raggedly: every sprite fully inside the tile whose
        ink comes within REACH px of an open side's line is taken out (a
        straddler stays: both tiles draw it), and small loose sprites are laid
        in the gap where they cover no straddler, fully inside the tile and
        4 px clear of every line."""
        small, reach_a, reach_b = RAGGED
        strad = [ink(i, x, y) for (i, x, y, lay) in keep if crossings(i, x, y)]

        def clear(box, others):
            l, t, r, bb = box
            return all(r <= a_ or l >= c_ or bb <= b_ or t >= d_ for (a_, b_, c_, d_) in others)
        out, n = [], 0
        for (i, x, y, lay) in keep:
            l, t, r, bb = ink(i, x, y)
            if crossings(i, x, y):
                out.append((i, x, y, lay)); continue
            if lay == 2:
                # the side's own edge sprite (a crag, a snow peak): kept
                out.append((i, x, y, lay)); continue
            reach = reach_a if n % 2 == 0 else reach_b
            n += 1
            near = (("N" in open_sides and t < reach) or ("S" in open_sides and bb > 96 - reach) or
                    ("W" in open_sides and l < reach) or ("E" in open_sides and r > 96 - reach))
            if not near:
                out.append((i, x, y, lay))
        # loose sprites in the gap: try a grid of spots, keep the ones whose ink
        # covers no straddler's ink and no other loose sprite
        sl, st, sr, sb = bbox(small)
        w, h = sr - sl, sb - st
        inkpx = set()
        for (i, x, y, lay) in keep:
            if crossings(i, x, y):
                a_ = alpha(i); l_, t_, r_, b_ = bbox(i)
                for yy in range(t_, b_):
                    for xx in range(l_, r_):
                        if a_[xx, yy] > 0:
                            inkpx.add((x + xx, y + yy))
        sa = alpha(small)
        mine = [(xx - sl, yy - st) for yy in range(st, sb) for xx in range(sl, sr) if sa[xx, yy] > 0]

        def clear(box, others):
            px_, py_ = box[0], box[1]
            if others is strad:
                return not any((px_ + dx_, py_ + dy_) in inkpx for dx_, dy_ in mine)
            l, t, r, bb = box
            return all(r <= a_ or l >= c_ or bb <= b_ or t >= d_ for (a_, b_, c_, d_) in others)
        placed = []
        spots = []
        for side in "NESW":
            if side not in open_sides:
                continue
            for f in (0.22, 0.5, 0.78):
                c = int(96 * f)
                spots.append({"N": (c - w // 2, 6), "S": (c - w // 2, 90 - h),
                              "W": (6, c - h // 2), "E": (90 - w, c - h // 2)}[side])
        for k, (px, py) in enumerate(spots):
            if k % 2:                       # every other spot: a gap of grass
                continue
            px = max(4, min(92 - w, px)); py = max(4, min(92 - h, py))
            box = (px, py, px + w, py + h)
            if clear(box, strad) and clear(box, placed):
                placed.append(box)
                out.append((small, px - sl, py - st, 3))
        return out

    check_lattice()
    lay = {"sprites": SPR, "grass": GRASS, "tiles": {}}
    lay["tiles"][NAME] = tile(0)
    for code in range(1, 20):
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


# ==========================================================================
# treetile.py -- compose 96 px tiles from hand-placed sprites
# ==========================================================================

def _treetile(argv):
    """Compose 96px terrain tiles from hand-placed 32px sprites.

    python3 tools/romeart.py compose <layout.json> <out-dir>

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


    lay = json.load(open(argv[1]))
    out = argv[2]
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
        if lay.get("shadow") and "_edge_" in name:
            # a soft contact shadow on the ground under an edge piece's
            # sprites (#63; the plain tile keeps none, so what is built from
            # it -- variants, fills, river pieces -- still matches):
            # their ink shifted dx, dy, blurred, at alpha a, and faded to
            # nothing within 4 px of every tile line, so it never makes a
            # seam with the neighbour
            dx_, dy_, a_ = lay["shadow"]
            ink = Image.new("RGBA", (96, 96))
            for i, x, y in places:
                blit(ink, sprite(i), x + dx_, y + dy_)
            from PIL import ImageFilter
            m = ink.getchannel("A").point(lambda v: 255 if v else 0).filter(ImageFilter.GaussianBlur(2))
            mp = m.load()
            for yy in range(96):
                for xx in range(96):
                    f = min(4, xx, yy, 95 - xx, 95 - yy) / 4
                    mp[xx, yy] = int(mp[xx, yy] * a_ * f)
            sh = Image.new("RGBA", (96, 96), (0, 0, 0, 0)); sh.putalpha(m)
            im.alpha_composite(sh)
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


# ==========================================================================
# seamcheck.py -- check a lattice layout against the border contract
# ==========================================================================

def _seamcheck(argv):
    """Check a lattice layout against the border contract, mechanically.

    python3 tools/romeart.py seamcheck <layout.json>

For every ordered pair of tile codes that the engine can place side by side
(horizontally and vertically), take every sprite whose INK straddles the
shared border in tile A and require tile B to hold the same sprite at the
mirrored position (x - 96 or y - 96), and vice versa. A terminal border
must have no straddler at all. And what is drawn IN FRONT of a straddler
must be the same on both sides: every sprite drawn after a straddler whose
ink overlaps the straddler's ink must itself be held by the other tile at
the mirrored position, or the straddler shows a cut where the cover ends
at the line. Reports every violation.

Which pairs can be adjacent: A's east side is an interface iff A's code
does not open east; then B's west side must not open west. The same for
south/north. Codes: 11 N, 12 S, 9 E, 10 W, 1 NW, 3 NE, 2 SW, 4 SE, 5..8
diagonal-only (all sides interfaces), 0 plain.
    """


    lay = json.load(open(argv[1]))
    spr = lay["sprites"]
    OPEN = {11: "N", 12: "S", 9: "E", 10: "W", 1: "NW", 3: "NE", 2: "SW", 4: "SE",
            5: "", 6: "", 7: "", 8: "", 0: "",
            # spits and strips (2026-09-10, REQ-229e): opposite sides open, three
            # sides open (named by the attached side's opposite), and an island
            13: "NS", 14: "EW", 15: "NES", 16: "ESW", 17: "SWN", 18: "WNE", 19: "NESW"}
    name = [k for k in lay["tiles"] if "_edge_" not in k][0]
    tiles = {0: lay["tiles"][name]}
    for c in range(1, 20):
        tiles[c] = lay["tiles"][f"{name}_edge_{c:02d}"]
    _bb = {}
    def bbox(i):
        if i not in _bb: _bb[i] = Image.open(f"{spr}/tile_{i:02d}.png").convert("RGBA").getbbox()
        return _bb[i]
    def ink(s):
        i, x, y = s; l, t, r, b = bbox(i)
        return (x + l, y + t, x + r, y + b)

    bad = 0
    def straddlers(code, side):
        out = []
        for s in tiles[code]["sprites"]:
            l, t, r, b = ink(s)
            if side == "E" and l < 96 < r: out.append(tuple(s))
            if side == "W" and l < 0 < r: out.append(tuple(s))
            if side == "S" and t < 96 < b: out.append(tuple(s))
            if side == "N" and t < 0 < b: out.append(tuple(s))
        return set(out)

    for a in tiles:
        for side in "ESWN":
            if side in OPEN[a]:
                st = straddlers(a, side)
                if st:
                    bad += len(st); print(f"code {a:2d} {side} is TERMINAL but {len(st)} sprite(s) straddle it: {sorted(st)[:4]}")
    for a in tiles:
        for b in tiles:
            # A east of B: A's E interface meets B's W interface
            if "E" not in OPEN[a] and "W" not in OPEN[b]:
                sa = straddlers(a, "E"); sb = straddlers(b, "W")
                ma = {(i, x - 96, y) for (i, x, y) in sa}
                if ma != sb:
                    bad += 1; print(f"H pair {a:2d}|{b:2d}: A crosses {sorted(sa)} vs B holds {sorted(sb)}")
            if "S" not in OPEN[a] and "N" not in OPEN[b]:
                sa = straddlers(a, "S"); sb = straddlers(b, "N")
                ma = {(i, x, y - 96) for (i, x, y) in sa}
                if ma != sb:
                    bad += 1; print(f"V pair {a:2d}/{b:2d}: A crosses {sorted(sa)} vs B holds {sorted(sb)}")

    def overlaps(a, b):
        return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]

    def covers(code, s):
        """Sprites drawn after s in the tile whose ink overlaps s's ink."""
        lst = [tuple(t) for t in tiles[code]["sprites"]]
        k = lst.index(s)
        return [u for u in lst[k + 1:] if overlaps(ink(u), ink(s))]

    # A sprite drawn in front of a straddler is fine when its ink stops short
    # of the line (an ordinary overlap inside the tile), or when it straddles
    # too (then both tiles draw it), or when it is flush to the line and the
    # neighbour draws the same sprite flush on its side of the same row (the
    # two abut, as the forest's lower rows do), or when it only touches the
    # line at a point: a round top or side meeting the line over a run of at
    # most FLUSH_RUN pixels is an ordinary outline, not a cut. A cover whose
    # ink lies along the line over a longer run (a slab end, a flat side)
    # with nothing to meet it cuts the straddler at the line.
    FLUSH_RUN = 8
    _al = {}
    def alpha(i):
        if i not in _al: _al[i] = Image.open(f"{spr}/tile_{i:02d}.png").convert("RGBA").split()[3].load()
        return _al[i]
    def run_on_line(u, side):
        """How many ink pixels of sprite u lie on the tile line for that side."""
        i, x, y = u
        a = alpha(i)
        if side in "EW":
            col = (96 if side == "E" else 0) - 1 - x if side == "E" else -x
            if side == "E": col = 95 - x
            return sum(1 for yy in range(96) if 0 <= col < 96 and a[col, yy] > 0)
        row = 95 - y if side == "S" else -y
        return sum(1 for xx in range(96) if 0 <= row < 96 and a[xx, row] > 0)
    def flush_cover_ok(code, other, side, u):
        l, t, r, b = ink(u)
        i, x, y = u
        held = {tuple(t) for t in tiles[other]["sprites"]}
        li, lt, lr, lb = bbox(i)
        if side == "E":
            if r != 96: return True           # short of the line, or a straddler (checked as one)
            return (i, -li, y) in held or run_on_line(u, side) <= FLUSH_RUN
        if side == "S":
            if b != 96: return True
            return (i, x, -lt) in held or run_on_line(u, side) <= FLUSH_RUN
        if side == "W":
            if l != 0: return True
            return (i, 96 - lr, y) in held or run_on_line(u, side) <= FLUSH_RUN
        if side == "N":
            if t != 0: return True
            return (i, x, 96 - lb) in held or run_on_line(u, side) <= FLUSH_RUN

    for a in tiles:
        for b in tiles:
            for side, other, dx, dy in (("E", "W", -96, 0), ("S", "N", 0, -96)):
                if side in OPEN[a] or other in OPEN[b]: continue
                for s in straddlers(a, side):
                    m = (s[0], s[1] + dx, s[2] + dy)
                    if m not in {tuple(t) for t in tiles[b]["sprites"]}: continue
                    badc = [u for u in covers(a, s) if not flush_cover_ok(a, b, side, u)]
                    badc += [u for u in covers(b, m) if not flush_cover_ok(b, a, other, u)]
                    if badc:
                        bad += 1
                        print(f"cover {side} pair {a:2d}|{b:2d}: straddler {s} cut by flush cover(s) {badc}")
    print("violations:", bad)


# ==========================================================================
# roadtile.py -- sweep a terrain set into the road/river/bridge pieces
# ==========================================================================

def _roadtile(argv):
    """Road tiles from a PixelLab 16 px dirt-over-grass tileset.

    python3 tools/romeart.py sweep <set-dir> <out-dir> [--seed N]
    python3 tools/romeart.py sweep <set-dir> <out-dir> --sweep [--rim N --rim-shade F]
    python3 tools/romeart.py sweep <set-dir> <out-dir> --sweep --prefix river   (river_*.png)
    python3 tools/romeart.py sweep <set-dir> <out-dir> --sweep --fill PAVING.png --grass RIVER.png
        (a bridge deck: the band filled with a 96 px tile over another piece)

A road piece is a 96 px tile built the way romeart.py stitch builds a
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

Pieces (24): road_ns, road_ew; curves road_ne, road_es, road_sw, road_wn
(named by their two exits); diagonals road_nesw, road_nwse; joins from a
straight exit to a diagonal corner road_n_sw, road_n_se, road_s_nw,
road_s_ne, road_e_nw, road_e_sw, road_w_ne, road_w_se; companions; and the
four ENDS road_n, road_e, road_s, road_w, named by their one exit -- the
road enters through that side at the full contract width and feathers away
to nothing inside the tile, so a run can stop in open grass.

Imperfection: an interior vertex with two or more grass neighbours flips
to grass at random (seeded), which nicks inner corners; border vertices
never change, they are the contract.
    """


    src, out = argv[1], argv[2]
    seed = int(argv[argv.index("--seed") + 1]) if "--seed" in argv else 1
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


    HW = int(argv[argv.index("--width") + 1]) // 2 if "--width" in argv else 16   # half width of a straight band, centred at 48
    DW = 32          # |x + y - 96| <= DW: a diagonal band; 32 keeps it continuous across a corner at 16 px vertices


    def ns(x, y): return abs(x - 48) <= HW
    def ew(x, y): return abs(y - 48) <= HW
    def d_nesw(x, y): return abs(x + y - 96) <= DW
    def d_nwse(x, y): return abs(x - y) <= DW
    def arc(x, y, cx, cy):
        """A quarter-ring curve: the straight band bent round the tile corner
        (cx, cy), radii 32..64, so it leaves both sides on the straight contract."""
        return 32 <= ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 <= 64


    # An end: the road enters through one side at the contract width, runs at that
    # width for the first half of the tile, then narrows and frays away, so the
    # paving peters out instead of stopping square.
    #
    # END_FULL matters twice over: the band must be at EXACTLY the contract width
    # where it leaves the tile or the piece beside it does not line up, and a road
    # that starts narrowing immediately reads as a deliberate wedge -- a
    # spearhead -- rather than a road that ends. Half the tile at full width, a
    # little over a third narrowing, the rest grass.
    END_TIP  = 12.0    # px of clear grass beyond the tip
    END_FULL = 48.0    # px of full-width band at the entry edge
    # How much harder the edge noise bites at the tip than at the entry edge. The
    # fray is what turns the last of the band into scattered stones; it is scaled
    # by how far along the run a pixel is, so it is exactly zero where the piece
    # has to meet its neighbour.
    END_FRAY = 3.5
    # Below about one stone's width there is no room for a stone, so the band stops
    # being paving and becomes a line of joint colour -- a crack, not a road. The
    # half width never goes under roughly one cobble; the front and the fray end
    # the run instead.
    END_MIN_W = 6.0
    # What an end must NOT look like: a band narrowing evenly on both sides of a
    # straight centreline down to a point. That is one tapered object, not a road
    # running out, and at a road's width it reads worse than that.
    #
    # So narrowing does very little of the work here. The run stays near full
    # width and is CUT OFF at a slanted front, which the fray then breaks up: the
    # paving simply stops, on a ragged diagonal. On top of that each side of the
    # band has its own (gentle) power, the centreline leans off true as the run
    # dies, and the slant tilts a different way for each of the four pieces, so no
    # two ends are rotations of one shape.
    #
    # All four terms are scaled by u, so at the entry edge every end is exactly
    # the straight contract: both half widths HW, centre 48, front not yet biting.
    #   (power of the low edge, power of the high edge, lean px, slant)
    END_ASYM = {
        "n": (0.45, 0.30, +5.0, +0.60),
        "e": (0.30, 0.50, -4.0, -0.55),
        "s": (0.50, 0.32, -6.0, -0.65),
        "w": (0.32, 0.48, +4.0, +0.50),
    }


    def end_run(side, x, y):
        """(u, off): how far along the run, and the signed offset off its centre.

        u is 1 or more where the band is at full contract width and 0 at the tip.
        """
        span = 96.0 - END_TIP - END_FULL
        if   side == "s": return (y - END_TIP) / span, x - 48.0
        elif side == "n": return ((96 - y) - END_TIP) / span, x - 48.0
        elif side == "e": return (x - END_TIP) / span, y - 48.0
        else:             return ((96 - x) - END_TIP) / span, y - 48.0


    def sd_end(side):
        p_lo, p_hi, lean, slant = END_ASYM[side]
        span = 96.0 - END_TIP - END_FULL

        def f(x, y):
            u, off = end_run(side, x, y)
            if u <= 0.0: return 96.0                  # past the tip: all grass
            t = min(u, 1.0)
            off -= lean * (1.0 - t)                   # the centreline drifts
            w_lo = max(HW * (t ** p_lo), END_MIN_W)
            w_hi = max(HW * (t ** p_hi), END_MIN_W)
            # Three ways to be outside the road: past either edge of the band, or
            # past the slanted front where the paving stops. The front is a line
            # across the run, not square to it, so the end is a diagonal.
            front = slant * off - u * span
            return max(off - w_hi, -off - w_lo, front)
        return f


    def fray_end(side):
        """Noise multiplier for an end piece: 1 at the entry edge, END_FRAY at the
        tip, so the join stays exact and only the dying part of the run breaks up."""
        def f(x, y):
            u, _ = end_run(side, x, y)
            t = 1.0 - max(0.0, min(1.0, u))
            return 1.0 + (END_FRAY - 1.0) * t
        return f


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
        "road_n": lambda x, y: sd_end("n")(x, y) <= 0,
        "road_e": lambda x, y: sd_end("e")(x, y) <= 0,
        "road_s": lambda x, y: sd_end("s")(x, y) <= 0,
        "road_w": lambda x, y: sd_end("w")(x, y) <= 0,
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
    SWEEP = "--sweep" in argv
    # --prefix NAME: write NAME_ns.png etc. instead of road_ns.png -- the same 24
    # shapes filled with another set's plain tile (a river is a road of water).
    PREFIX = argv[argv.index("--prefix") + 1] if "--prefix" in argv else "road"


    def out_name(name):
        return PREFIX + name[len("road"):] if name.startswith("road") else name
    RAG = float(argv[argv.index("--rag") + 1]) if "--rag" in argv else 3.0
    RIM = float(argv[argv.index("--rim") + 1]) if "--rim" in argv else 0.0   # px of rim just inside the edge
    # --rim-shade F: the rim is the road's own colour at that pixel times F, so the
    # border is predictably "the paving, a shade darker" whatever the set returned.
    # Without it the rim takes rim_colour(), the colour the set itself paints where
    # the two terrains meet.
    RIM_SHADE = float(argv[argv.index("--rim-shade") + 1]) if "--rim-shade" in argv else 0.0


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
        "road_n": sd_end("n"), "road_e": sd_end("e"),
        "road_s": sd_end("s"), "road_w": sd_end("w"),
    }
    # Only the ends fray; every other piece keeps one noise amplitude end to end.
    FRAY = {
        "road_n": fray_end("n"), "road_e": fray_end("e"),
        "road_s": fray_end("s"), "road_w": fray_end("w"),
    }
    NOISE_CELL = 8
    _lat = [None]      # the noise lattice, built on first use (was a module global)
    def noise(x, y):
        """Value noise, bilinear on an 8 px lattice, periodic every 96 px, in -1..1."""
        if _lat[0] is None:
            r = random.Random(seed * 7919 + 1)
            n = 96 // NOISE_CELL
            _lat[0] = [[r.uniform(-1, 1) for _ in range(n)] for _ in range(n)]
        n = 96 // NOISE_CELL
        fx, fy = (x % 96) / NOISE_CELL, (y % 96) / NOISE_CELL
        i, j = int(fx) % n, int(fy) % n
        tx, ty = fx - int(fx), fy - int(fy)
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a, b = _lat[0][j][i], _lat[0][j][(i + 1) % n]
        c, d = _lat[0][(j + 1) % n][i], _lat[0][(j + 1) % n][(i + 1) % n]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


    def sweep(name, dirt96, g96):
        im = g96.copy(); px = im.load(); dp = dirt96.load()
        sd = SD[name]
        fray = FRAY.get(name)
        # rim_colour() reads the set's transition tiles, so only pay for it when
        # the rim actually wants that colour.
        rc = (rim_colour() + (255,)) if (RIM > 0 and not RIM_SHADE) else None
        for y in range(96):
            for x in range(96):
                amp = fray(x + 0.5, y + 0.5) if fray else 1.0
                d = sd(x + 0.5, y + 0.5) + RAG * amp * noise(x + 0.5, y + 0.5)
                if d <= 0:
                    if d > -RIM:
                        if RIM_SHADE:
                            r, g, b, a = dp[x, y]
                            px[x, y] = (int(r * RIM_SHADE), int(g * RIM_SHADE),
                                        int(b * RIM_SHADE), a)
                        else:
                            px[x, y] = rc
                    else:
                        px[x, y] = dp[x, y]
        return im


    # --pro DIR: pieces from a PixelLab Tiles Pro "roads" set (18 tiles of
    # 32 px, edge rule, mask bits N=1 E=2 S=4 W=8). A 96 px piece is a 3x3 of
    # those sub-tiles on the pack's grass; the road runs down the MIDDLE
    # sub-tile of a straight side, so any piece meets any other. A diagonal is
    # a 32 px staircase that leaves through the bottom corner sub-tile, and
    # the cell BELOW it carries the COMPANION (road_c_ne under a NW-SE run,
    # road_c_nw under a NE-SW run): one sub-tile of road in its top corner
    # that bends into the next diagonal cell. Two companions only.
    PRO = argv[argv.index("--pro") + 1] if "--pro" in argv else None
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
        GRASS_PATH = argv[argv.index("--grass") + 1] if "--grass" in argv else "assets/glory-of-rome/art/tiles/grass.png"
        g96 = Image.open(GRASS_PATH).convert("RGBA")
        # --fill PATH: fill the band with this 96 px tile instead of the set's plain
        # upper tile (a bridge's paving across a river piece).
        if "--fill" in argv:
            d96 = Image.open(argv[argv.index("--fill") + 1]).convert("RGBA").resize((96, 96))
    g96.save(os.path.join(out, "grass.png"))
    for name in SHAPES:
        if SWEEP:
            made[name] = sweep(name, d96, g96); made_v[name] = vertices(SHAPES[name], random.Random(0))
        else:
            made[name], made_v[name] = build(name, rng)
        made[name].save(os.path.join(out, out_name(name) + ".png"))

    names = list(SHAPES)
    sheet = Image.new("RGBA", (5 * 100, ((len(names) + 4) // 5) * 100), (40, 40, 40, 255))
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
        "......wrgg1",
        "...2.......",
        "...f...4gg3",
        "...f.......",
        "...5.......",
    ]
    W = max(len(r) for r in MOCK); H = len(MOCK)
    code = {"f": "road_ns", "g": "road_ew", "h": "road_ne", "i": "road_es", "j": "road_sw", "k": "road_wn",
            "l": "road_nesw", "m": "road_nwse", "n": "road_n_sw", "o": "road_n_se", "p": "road_s_nw", "q": "road_s_ne",
            "r": "road_e_nw", "s": "road_e_sw", "t": "road_w_ne", "u": "road_w_se",
            "v": "road_c_nw", "w": "road_c_ne", "x": "road_c_sw", "y": "road_c_se",
            "1": "road_w", "2": "road_s", "3": "road_w", "4": "road_e", "5": "road_n"}
    mock = Image.new("RGBA", (W * 96, H * 96))
    for j, row in enumerate(MOCK):
        for i in range(W):
            c = row[i] if i < len(row) else "."
            mock.paste(g96 if c == "." else made[code[c]], (i * 96, j * 96))
    mock.save(os.path.join(out, "mock1x.png"))
    print(len(made), "road pieces ->", out, "| contract sides with extra patterns:", check_contract(made_v))


# ==========================================================================
# artprompts.py -- rebuild docs/ROME-ART.md from art/jobs
# ==========================================================================

def _artprompts(argv):
    """Rebuild docs/ROME-ART.md: every prompt and setting the pack's art was made from.

    python3 tools/romeart.py prompts [out.md]

One page, generated from art/jobs/*.json, so it cannot drift from the jobs
themselves. Each entry carries the engine and its settings and the prompt
exactly as it is sent. A job whose output is in the pack is marked INSTALLED;
a job with no pack path is a step towards one (the still an animation starts
from). A job whose pack path is NOT in the pack produces nothing the game
uses and is left out. The jobs' `_note` fields -- the history of each run --
stay in the job files and are not repeated here.

Two engines make everything (docs/ART-PIPELINE.md): Retro Diffusion draws
figures, screens and objects; PixelLab makes the terrain sets and sprite
batches. The reference groups by what the art IS, not by engine.
    """


    JOBS = "art/jobs"
    PACK = "assets/glory-of-rome"
    OUT = argv[1] if len(argv) > 1 else "docs/ROME-ART.md"


    def engine_of(d):
        if "lower_description" in d:
            ts = d.get("tile_size", {})
            size = ts.get("width", ts) if isinstance(ts, dict) else ts
            return "PixelLab create-tileset", f"{size} px, seed {d.get('seed', '-')}"
        if "batches" in d:
            return "PixelLab create-1-direction-object", f"96 px, {sum(len(b) for b in d['batches'])} items"
        if "tile_size" in d:
            return "PixelLab tiles", f"{d.get('tile_size')} px, seed {d.get('seed', '-')}"
        style = str(d.get("style", "?"))
        return f"Retro Diffusion {style}", f"{d.get('width', '?')}x{d.get('height', '?')}, seed {d.get('seed', '-')}"


    # Keys that are prompts, notes or image blobs: everything else is a setting,
    # and the settings are half the record (the style, the frame count, whether
    # prompt expansion was bypassed, what it chained to).
    SKIP = {"id", "prompt", "description", "lower_description", "upper_description",
            "transition_description", "batches", "_note", "_pack_path", "_review_rule"}


    def settings_of(d):
        out = []
        for k in sorted(d):
            if k in SKIP:
                continue
            v = d[k]
            if isinstance(v, dict) and ("base64" in v or "image" in v):
                out.append(f"{k}=<image>")
                continue
            if isinstance(v, str) and len(v) > 80:
                out.append(f"{k}=<{len(v)} chars>")
                continue
            out.append(f"{k}={json.dumps(v) if not isinstance(v, str) else v}")
        return out


    def prompts_of(d):
        """Every prompt a job sends, labelled."""
        out = []
        if "lower_description" in d:
            for key, label in (("lower_description", "lower"),
                               ("upper_description", "upper"),
                               ("transition_description", "where they meet")):
                if d.get(key):
                    out.append((label, d[key]))
        elif "batches" in d:
            out.append(("shared", d.get("description", "")))
            for i, b in enumerate(d["batches"], 1):
                out.append((f"batch {i}", " · ".join(b)))
        else:
            for key, label in (("prompt", "prompt"), ("description", "description")):
                if d.get(key):
                    out.append((label, d[key]))
        return out


    def group_of(name, d):
        p = d.get("_pack_path", "")
        if name.startswith("intro_"):
            return "Introduction"
        for frag, g in (("art/troops/", "Troops"), ("art/portraits/", "Portraits and faces"),
                        ("art/villains/", "Villains"), ("art/classes/", "Hero classes"),
                        ("art/tiles/", "Map tiles and terrain"), ("art/scenes/", "Scenes"),
                        ("art/ui/", "Screens and UI")):
            if p.startswith(frag):
                return g
        if "lower_description" in d or name.startswith(("t16_", "t32_", "grass16", "grass32")):
            return "Map tiles and terrain"
        if "batches" in d:
            return "Sprite batches (trees, rocks)"
        if name.startswith("backdrop") or name.startswith("splash") or name.startswith("title"):
            return "Screens and UI"
        if name.startswith("troop_portrait") or "portrait" in name:
            return "Portraits and faces"
        # A job with no pack path is a step towards one: a troop still, its attack
        # loop, a pose the animation starts from. Group it with the troop it names.
        import glob as _g
        stem = name.split("_")[0]
        for f in _g.glob(os.path.join(PACK, "art", "troops", stem + "_*.png")):
            return "Troops"
        if str(d.get("style", "")).startswith("rd_advanced_animation"):
            return "Animations"
        return "Other"


    def main():
        jobs = []
        for f in sorted(os.listdir(JOBS)):
            if not f.endswith(".json"):
                continue
            try:
                d = json.load(open(os.path.join(JOBS, f)))
            except Exception:
                continue
            jobs.append((f[:-5], d))

        # A job whose pack path is not in the pack makes nothing the game uses.
        def live(d):
            pp = d.get("_pack_path", "").split(" ")[0].replace("<x>_<y>", "0_0")
            if not pp:
                return True
            import re as _re
            rng = _re.match(r"^(.*_)(\d+)\.\.(\d+)(\.\w+)$", pp)
            probe = (rng.group(1) + rng.group(2) + rng.group(4)) if rng else pp
            return os.path.exists(os.path.join(PACK, probe))
        jobs = [(n, d) for n, d in jobs if live(d)]
        kept = len(jobs)

        groups = {}
        for name, d in jobs:
            groups.setdefault(group_of(name, d), []).append((name, d))

        lines = [
            "# Rome art: every prompt and setting",
            "",
            "**Generated** by `tools/romeart.py prompts` from `art/jobs/*.json`. Do not",
            "edit by hand: change the job file and run the tool again.",
            "",
            f"{kept} jobs. A job with a **Pack path** has produced that file in the pack; a",
            "job without one has produced a step towards it (the still an animation starts",
            "from). The routes themselves -- which engine, which settings, and why -- have",
            "been in `docs/ART-PIPELINE.md`; each job's run history has been in its own",
            "`_note` field.",
            "",
        ]
        for g in sorted(groups):
            lines += [f"## {g}", ""]
            for name, d in sorted(groups[g]):
                eng, settings = engine_of(d)
                pack = d.get("_pack_path", "")
                head = f"### {name}"
                lines += [head, "", f"- **Engine:** {eng} ({settings})"]
                if pack:
                    lines.append(f"- **Pack path:** `{pack}`")
                for label, text in prompts_of(d):
                    lines.append(f"- **{label}:** {text}")
                st = settings_of(d)
                if st:
                    lines.append("- **Settings:** " + ", ".join(f"`{x}`" for x in st))
                lines.append("")
        open(OUT, "w").write("\n".join(lines) + "\n")
        print(f"wrote {OUT}: {len(jobs)} jobs in {len(groups)} groups")

    main()


# ==========================================================================
# The recipe: a continent's whole tile set, from its primitives to the pack
# ==========================================================================
#
# The inputs are what the generation calls returned, kept under
# art/primitives/<zone>/: the grass, sea, desert, cobble and river tilesets,
# and the tree and rock sprite batches. The outputs are the pack's tiles.
# Recorded per zone in art/primitives/<zone>/BUILD.md.

def _plain_tile(set_dir, which):
    """The set's plain lower (grass) or upper (the other surface) tile."""
    meta = json.load(open(os.path.join(set_dir, "tiles_meta.json")))
    idx = [n for n, t in enumerate(meta)
           if all(t["corners"][k] == which for k in ("NW", "NE", "SW", "SE"))][0]
    return Image.open(os.path.join(set_dir, f"tile_{idx:02d}.png")).convert("RGBA")


def _pack_names(prefix):
    """The art names the pack already ships with that prefix, so a zone set
    holds exactly the same names as the master set and no more."""
    out = []
    for f in sorted(os.listdir(os.path.join(PACK, "art", "tiles"))):
        if f.endswith(".png") and f.startswith(prefix):
            out.append(f)
    return out


def _zone_cfg(zone):
    """A zone's build settings: which tree crown, which rock slots, whether it
    has desert. Everything else is the same for every continent."""
    prim = os.path.join("art", "primitives", zone)
    cfg = {"prim": prim,
           "crown": {"galliae": 0, "africa": 0, "oriens": 1}.get(zone, 0),
           "slots": os.path.join(prim, "rock_slots.json")}
    if not os.path.exists(cfg["slots"]):
        cfg["slots"] = None
    return cfg


MOUNTAIN_SHADOW = [2, 3, 0.35]     # compose's "shadow" for every set's mountain edges (#63)


def cmd_zone(argv):
    """Build every tile of a continent's set into build/art/<zone>_tiles/out."""
    if not argv:
        sys.exit("usage: romeart.py zone <zone>")
    zone = argv[0]
    cfg = _zone_cfg(zone)
    prim, stage = cfg["prim"], os.path.join("build", "art", f"{zone}_tiles")
    out = os.path.join(stage, "out")
    if not os.path.isdir(prim):
        sys.exit(f"romeart: no primitives for {zone} (looked in {prim})")
    os.makedirs(out, exist_ok=True)

    # 1. The grass: the set's plain lower tile, laid to the pack tile.
    g = _plain_tile(os.path.join(prim, "grass"), "lower")
    base = Image.new("RGBA", (96, 96))
    for y in range(0, 96, g.height):
        for x in range(0, 96, g.width):
            base.paste(g, (x, y))
    base.save(os.path.join(out, "grass.png"))

    # 2. Its variants, and the named variant the pack draws as `grass_variant`.
    _grassvar(["romeart", os.path.join(prim, "grass"), os.path.join(stage, "grassvar"),
               "--count", "10", "--seed", "9", "--patch-rate", "0.3", "--patch-size", "1"])
    for f in glob.glob(os.path.join(stage, "grassvar", "grass_*.png")):
        Image.open(f).save(os.path.join(out, os.path.basename(f)))
    Image.open(os.path.join(out, "grass_01.png")).save(os.path.join(out, "grass_variant.png"))

    # 3. Sea, and desert where the continent has one: the stitched edges.
    for src, terrain in (("sea", "water"), ("desert", "desert")):
        if not os.path.isdir(os.path.join(prim, src)):
            continue
        _stitch96(["romeart", os.path.join(prim, src), terrain,
                   os.path.join(stage, terrain), "--seed", "3"])
        for f in _pack_names(terrain):
            p = os.path.join(stage, terrain, f)
            if os.path.exists(p):
                Image.open(p).save(os.path.join(out, f))

    # 4. Forest and mountain: one lattice each, then the tiles composed over
    #    this zone's own grass.
    for terrain, sprites, extra in (("forest", "trees", ["--crown", str(cfg["crown"])]),
                                    ("mountain", "rocks",
                                     ["--terrain", "mountain"] +
                                     (["--slots", cfg["slots"]] if cfg["slots"] else []) +
                                     # ragged rock edges (#63), the loose rock rock 6,
                                     # the side crags set back 12 and 28 px
                                     ["--ragged", "6,20,34,12,28"])):
        lay = os.path.join(stage, f"{terrain}.json")
        _forestlattice(["romeart", lay, "--sprites", os.path.join(prim, sprites),
                        "--name", terrain] + extra)
        d = json.load(open(lay))
        d["grass"] = os.path.join(out, "grass.png")
        if terrain == "mountain":
            d["shadow"] = MOUNTAIN_SHADOW      # a soft contact shadow (#63)
        json.dump(d, open(lay, "w"), indent=1)
        _treetile(["romeart", lay, os.path.join(stage, terrain)])
        print(f"  {terrain}: ", end="")
        _seamcheck(["romeart", lay])
        for f in _pack_names(terrain):
            p = os.path.join(stage, terrain, f)
            if os.path.exists(p):
                Image.open(p).save(os.path.join(out, f))
        # The same pieces over the set's desert, where it has one: a wood or
        # range whose open sides all face sand fades to sand (`map build` picks
        # <terrain>_sand_edge_NN, #63). Forest 07 and 08 are left out: their
        # codes went to the vista landmarks.
        if os.path.exists(os.path.join(out, "desert.png")):
            d["grass"] = os.path.join(out, "desert.png")
            json.dump(d, open(lay, "w"), indent=1)
            _treetile(["romeart", lay, os.path.join(stage, terrain + "_sand")])
            for k in range(1, 13):
                if terrain == "forest" and k in (7, 8):
                    continue
                Image.open(os.path.join(stage, terrain + "_sand", f"{terrain}_edge_{k:02d}.png")).save(
                    os.path.join(out, f"{terrain}_sand_edge_{k:02d}.png"))

    # 5. Roads and rivers, swept over this zone's grass; the rivers again over
    #    its forest and mountain for the pieces that run through them.
    sweeps = [("cobble", "road", os.path.join(out, "grass.png"), "roads"),
              ("river", "river", os.path.join(out, "grass.png"), "rivers"),
              ("river", "river_forest", os.path.join(out, "forest.png"), "rivers_forest"),
              ("river", "river_mountain", os.path.join(out, "mountain.png"), "rivers_mountain")]
    for src, prefix, ground, dst in sweeps:
        if not os.path.isdir(os.path.join(prim, src)):
            continue
        _roadtile(["romeart", os.path.join(prim, src), os.path.join(stage, dst),
                   "--sweep", "--prefix", prefix, "--rim", "2", "--rim-shade", "0.8",
                   "--grass", ground])
        for f in _pack_names(prefix + "_"):
            p = os.path.join(stage, dst, f)
            if os.path.exists(p):
                Image.open(p).save(os.path.join(out, f))

    # 6. The two river bridges: the road carried across the river between
    #    parapets, its shadow on the water (cmd_bridge, style c).
    if os.path.exists(os.path.join(out, "river_ns.png")) and os.path.exists(os.path.join(out, "road_ew.png")):
        cmd_bridge([out, os.path.join(stage, "bridges")])
        for name in ("bridge_river_ew.png", "bridge_river_ns.png"):
            Image.open(os.path.join(stage, "bridges", "c", name)).save(os.path.join(out, name))

    # 7. The river mouths: east built, west its mirror (as Italia's were).
    if os.path.exists(os.path.join(out, "water_edge_02.png")):
        _rivermouth(["romeart", os.path.join(out, "water_edge_02.png"),
                     os.path.join(out, "river_ew.png"), os.path.join(out, "grass.png"),
                     os.path.join(out, "water.png"), os.path.join(out, "river_mouth_e.png")])
        Image.open(os.path.join(out, "river_mouth_e.png")).transpose(
            Image.FLIP_LEFT_RIGHT).save(os.path.join(out, "river_mouth_w.png"))

    n = len([f for f in os.listdir(out) if f.endswith(".png")])
    print(f"built {n} tiles in {out}")
    return out


def cmd_install(argv):
    """Copy a built set into the pack and name every tile in game.json."""
    if not argv:
        sys.exit("usage: romeart.py install <zone>")
    zone = argv[0]
    out = os.path.join("build", "art", f"{zone}_tiles", "out")
    if not os.path.isdir(out):
        sys.exit(f"romeart: nothing built for {zone} (run: romeart.py zone {zone})")
    dst = os.path.join(PACK, "art", "tiles", zone)
    os.makedirs(dst, exist_ok=True)
    arts = []
    for f in sorted(os.listdir(out)):
        if not f.endswith(".png"):
            continue
        Image.open(os.path.join(out, f)).save(os.path.join(dst, f))
        arts.append(f[:-4])

    # game.json is hand-formatted: edit the zone's two keys in place, never
    # reprint the file.
    p = os.path.join(PACK, "game.json")
    s = open(p).read()
    want = json.loads(s)
    zi = [i for i, z in enumerate(want["zones"]) if z["id"] == zone][0]
    listing = '["' + '", "'.join(arts) + '"]'
    if want["zones"][zi].get("tile_set_arts"):
        old = ('\t\t\t"tile_set_arts":\t["'
               + '", "'.join(want["zones"][zi]["tile_set_arts"]) + '"],\n')
        if s.count(old) != 1:
            sys.exit("romeart: game.json's tile_set_arts is not where expected")
        s = s.replace(old, f'\t\t\t"tile_set_arts":\t{listing},\n')
    else:
        anchor = f'\t\t\t"map":\t"maps/{zone}.dat",\n'
        if s.count(anchor) != 1:
            sys.exit("romeart: game.json's map line is not where expected")
        s = s.replace(anchor, anchor + f'\t\t\t"tile_set":\t"{zone}",\n'
                                       f'\t\t\t"tile_set_arts":\t{listing},\n')
    want["zones"][zi]["tile_set"] = zone
    want["zones"][zi]["tile_set_arts"] = arts
    if json.loads(s) != want:
        sys.exit("romeart: refusing to write game.json -- the edit changed something else")
    open(p, "w").write(s)
    print(f"installed {len(arts)} tiles as the {zone} set")


def cmd_sheet(argv):
    """A review page of a set: every tile at 1:1, on that zone's own grass."""
    if not argv:
        sys.exit("usage: romeart.py sheet <zone> [out-dir]")
    zone = argv[0]
    out_dir = argv[1] if len(argv) > 1 else os.path.join("build", "art", f"{zone}_sheet")
    src = os.path.join(PACK, "art", "tiles", zone)
    if not os.path.isdir(src):
        sys.exit(f"romeart: {zone} has no tile set in the pack")
    os.makedirs(out_dir, exist_ok=True)
    names = sorted(f for f in os.listdir(src) if f.endswith(".png"))
    cols = 8
    cell = 96 + 8
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell + 8, rows * (cell + 10) + 8), (25, 25, 25))
    for i, f in enumerate(names):
        im = Image.open(os.path.join(src, f)).convert("RGBA")
        cellim = Image.new("RGBA", (96, 96), (30, 30, 30, 255))
        cellim.alpha_composite(im)
        sheet.paste(cellim.convert("RGB"),
                    (8 + (i % cols) * cell, 8 + (i // cols) * (cell + 10)))
    sheet.save(os.path.join(out_dir, "tiles.png"))
    print(f"wrote {out_dir}/tiles.png: {len(names)} tiles")



# ==========================================================================
# icon -- the launcher icon, composed from the pack's own title pieces
# ==========================================================================

def cmd_icon(argv):
    """Build the launcher icon from the title art. No generation, no filtering.

    python3 tools/romeart.py icon build/art/icon

    The title screen composes art/ui/title_battle.png (the legion on the ridge)
    and art/ui/title_eagle.png (the aquila standard) at runtime; the icon is the
    same two files, squared at 128x128 with the menu and the wordmarks left out,
    and then doubled to 512 and 1024 with nearest-neighbour -- each pixel
    becomes a 4x4 or 8x8 block, so the result is the game's own art at icon
    size rather than an upscale of anything.

    Opaque on purpose: Apple rejects an icon with an alpha channel.

    Writes icon_128.png, icon_512.png (Play) and icon_1024.png (App Store).
    """
    out = argv[1] if len(argv) > 1 else "build/art/icon"
    os.makedirs(out, exist_ok=True)

    battle = Image.open(f"{PACK}/art/ui/title_battle.png").convert("RGBA")
    eagle = Image.open(f"{PACK}/art/ui/title_eagle.png").convert("RGBA")

    # The square is the middle of the ridge -- ranks in front, the enemy line
    # behind -- with the standard stood in it, cropped where the pole leaves
    # the frame so the SPQR plaque is the lowest thing in the icon.
    icon = Image.new("RGBA", (128, 128))
    icon.paste(battle.crop((64, 36, 192, 164)), (0, 0))
    standard = eagle.crop((0, 0, 96, 128))
    icon.paste(standard, (16, 0), standard)

    icon = icon.convert("RGB")
    icon.save(f"{out}/icon_128.png")
    for n in (4, 8):
        icon.resize((128 * n, 128 * n), Image.NEAREST).save(f"{out}/icon_{128 * n}.png")
    print(f"icon: {out}/icon_128.png, icon_512.png, icon_1024.png")



def cmd_slots(argv):
    """Search rock-slot arrangements for a mountain lattice.

    python3 tools/romeart.py slots <sprites-dir> <out-slots.json> [--tries N] [--seed S] [--straddle 4567]

Which rock fits a slot depends on its ink box, so a new rock set needs its own
rock_slots.json (Galliae, 2026-09-19: chosen by hand for seamcheck 0 and the fewest
grass pixels showing). This draws N random arrangements, runs `lattice` and
`seamcheck` on each, and writes the arrangement with zero violations and the fewest
grass pixels showing; --straddle names the sprites allowed in the slots that
straddle a border (the east straddlers and the lower rows), which must be small
enough to cross one border only (Italia's rocks 4-7, 2026-09-27, #67). Prints the
ten best. Nothing is generated; this only arranges sprites already kept.
    """
    import random, re, subprocess, tempfile
    if len(argv) < 2:
        sys.exit("usage: romeart.py slots <sprites-dir> <out-slots.json> [--tries N] [--seed S] [--straddle 4567]")
    spr, out = argv[0], argv[1]
    tries = int(argv[argv.index("--tries") + 1]) if "--tries" in argv else 300
    rnd = random.Random(int(argv[argv.index("--seed") + 1]) if "--seed" in argv else 1)
    n = len([f for f in os.listdir(spr) if f.startswith("tile_") and f.endswith(".png")])
    ALL = list(range(n))
    SMALL = [int(c) for c in argv[argv.index("--straddle") + 1]] if "--straddle" in argv else ALL
    INS = [0, 4, 6, 8, 12, 14]
    tmp = tempfile.mkdtemp(); lay = os.path.join(tmp, "lay.json"); sl = os.path.join(tmp, "slots.json")
    res = []
    for _ in range(tries):
        slots = {"upper": [[rnd.choice(ALL), rnd.choice(SMALL)], [rnd.choice(ALL), rnd.choice(SMALL)]],
                 "lower": [[rnd.choice(SMALL), rnd.choice(SMALL)], [rnd.choice(SMALL), rnd.choice(SMALL)]],
                 "edge_w": [[rnd.choice(ALL), rnd.choice(INS), p] for p in ("top", "mid", "bottom")],
                 "edge_e": [[rnd.choice(ALL), rnd.choice(INS), p] for p in ("top", "mid", "bottom")]}
        json.dump(slots, open(sl, "w"))
        r = subprocess.run([sys.executable, __file__, "lattice", lay, "--sprites", spr, "--terrain", "mountain", "--name", "mountain", "--slots", sl], capture_output=True, text=True)
        m = re.search(r"grass showing in the plain tile: (\d+) px", r.stdout + r.stderr)
        if not m:
            continue
        s = subprocess.run([sys.executable, __file__, "seamcheck", lay], capture_output=True, text=True)
        v = re.search(r"violations: (\d+)", s.stdout + s.stderr)
        res.append((int(v.group(1)) if v else 9999, int(m.group(1)), slots))
    res.sort(key=lambda t: (t[0], t[1]))
    print(f"tried {tries}, layouts {len(res)}, zero-violation {sum(1 for r in res if r[0] == 0)}")
    for viol, grass, s in res[:10]:
        print(f"  violations {viol:4d} grass {grass:4d}  upper {s['upper']} lower {s['lower']} w {[e[0] for e in s['edge_w']]} e {[e[0] for e in s['edge_e']]}")
    if res and res[0][0] == 0:
        json.dump(res[0][2], open(out, "w"), indent=1); print("wrote", out)
    else:
        sys.exit("no zero-violation arrangement found; raise --tries or change --straddle")



def cmd_rebank(argv):
    """Carry river bands onto a new ground: river_<terrain>_* tiles rebuilt from a new interior.

    python3 tools/romeart.py rebank <old-ground.png> <new-ground.png> <tiles-dir> <prefix>

A river-through-terrain tile is the terrain's interior with the river band
swept over it (`sweep --grass <ground>`), so every pixel that differs from the
old interior is band or rim. This keeps exactly those pixels and lays them on
the new interior, for a set whose river primitives were never kept (Italia,
2026-09-27, #67). Rewrites <tiles-dir>/<prefix>_*.png in place and prints the
band pixel count of each.
    """
    if len(argv) < 4:
        sys.exit("usage: romeart.py rebank <old-ground.png> <new-ground.png> <tiles-dir> <prefix>")
    import numpy as np
    old = np.array(Image.open(argv[0]).convert("RGBA")); new = np.array(Image.open(argv[1]).convert("RGBA"))
    d, prefix = argv[2], argv[3]
    for f in sorted(os.listdir(d)):
        if not (f.startswith(prefix + "_") and f.endswith(".png")):
            continue
        t = np.array(Image.open(os.path.join(d, f)).convert("RGBA"))
        band = np.any(t != old, axis=2)
        out = new.copy(); out[band] = t[band]
        Image.fromarray(out).save(os.path.join(d, f))
        print(f"{f}: {int(band.sum())} band px")


def cmd_fieldgrade(argv):
    """Derive a continent's field painting from another's: a window, a flip, a colour grade.

    python3 tools/romeart.py fieldgrade <src.png> <out.png> --window x,y,w,h [--flip h|v|180]
                                        [--hue N] [--sat F] [--val F] [--tint R,G,B,W]

Cuts the window (w:h should be 6:5 so `siegeslice --field` keeps all of
it), flips it, shifts hue by N (OpenCV's 0..180 scale), scales saturation
and value by F, and blends W of the tint colour over the result. The three
non-Italian fields of The Glory of Rome are windows of the calmed Italia
painting graded this way (2026-09-28, #64; the exact calls are in
art/fields/BUILD.md). Deterministic: the same call gives the same file.
    """
    if len(argv) < 2:
        sys.exit("usage: romeart.py fieldgrade <src.png> <out.png> --window x,y,w,h [--flip h|v|180] [--hue N] [--sat F] [--val F] [--tint R,G,B,W]")
    import numpy as np
    import cv2
    src, out = argv[0], argv[1]
    opt = {"--window": None, "--flip": None, "--hue": "0", "--sat": "1", "--val": "1", "--tint": None}
    i = 2
    while i < len(argv):
        if argv[i] in opt and i + 1 < len(argv):
            opt[argv[i]] = argv[i + 1]; i += 2
        else:
            sys.exit(f"fieldgrade: bad argument {argv[i]}")
    if not opt["--window"]:
        sys.exit("fieldgrade: --window x,y,w,h is required")
    x, y, w, h = [int(v) for v in opt["--window"].split(",")]
    im = cv2.imread(src, cv2.IMREAD_UNCHANGED)
    if im is None:
        sys.exit(f"fieldgrade: cannot read {src}")
    crop = im[y:y + h, x:x + w, :3].copy()
    if opt["--flip"] == "h":
        crop = crop[:, ::-1]
    elif opt["--flip"] == "v":
        crop = crop[::-1, :]
    elif opt["--flip"] == "180":
        crop = crop[::-1, ::-1]
    elif opt["--flip"]:
        sys.exit("fieldgrade: --flip takes h, v or 180")
    hsv = cv2.cvtColor(np.ascontiguousarray(crop), cv2.COLOR_BGR2HSV).astype(np.float32)
    hsv[:, :, 0] = (hsv[:, :, 0] + float(opt["--hue"])) % 180
    hsv[:, :, 1] = np.clip(hsv[:, :, 1] * float(opt["--sat"]), 0, 255)
    hsv[:, :, 2] = np.clip(hsv[:, :, 2] * float(opt["--val"]), 0, 255)
    res = cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR).astype(np.float32)
    if opt["--tint"]:
        r, g, b, wt = [float(v) for v in opt["--tint"].split(",")]
        res = res * (1 - wt) + np.array([b, g, r], np.float32) * wt
    res = np.clip(res, 0, 255).astype(np.uint8)
    cv2.imwrite(out, np.dstack([res, np.full((h, w), 255, np.uint8)]))
    m = res.reshape(-1, 3).mean(axis=0)
    print(f"{out}: {w}x{h} from ({x},{y}) flip {opt['--flip'] or 'none'}; mean RGB {int(m[2])},{int(m[1])},{int(m[0])}")


def cmd_bridge(argv):
    """River bridges built from a set's installed road and river tiles.

    python3 tools/romeart.py bridge <tiles-dir> <out-dir>

Writes <out-dir>/<style>/bridge_river_ew.png and bridge_river_ns.png for
three styles, each one step on from the last:
  a  the road's own pixels laid over the river piece (road_ew over river_ns,
     road_ns over river_ew), so the deck is the road's cobbles and its ends
     are the road's edge: it joins the road by construction.
  b  over the river, plus 4 px of bank each side, the road's ragged edge
     gives way to a straight deck between two 5 px parapets on the road's
     outer edge: the road's stone mixed 40/60 with pale travertine, a dark
     outer line and a joint every 8 px.
  c  b, and the deck's shadow on the water below it (right of it, crossing
     north-south): 3 px at 0.6.
Reads grass.png, road_ew/ns.png and river_ns/ew.png from <tiles-dir> (the
pack's art/tiles for Italia, art/tiles/<zone> for the others). Deterministic.
    """
    if len(argv) < 2:
        sys.exit("usage: romeart.py bridge <tiles-dir> <out-dir>")
    import numpy as np
    tiles, out = argv[0], argv[1]
    load = lambda n: np.array(Image.open(os.path.join(tiles, n)).convert("RGBA")).astype(np.int32)
    G = load("grass.png")
    P, LINE, SHADOW, BANK = 5, 0.55, 0.6, 4
    PALE = (196, 188, 168)    # a warm travertine, mixed 60/40 into the road's stone

    def build(road, river, grass, style):
        # Everything below is for a deck running east-west; north-south is
        # built transposed and turned back.
        on_road = (road[:, :, :3] != grass[:, :, :3]).any(axis=2)
        wet = (river[:, :, :3] != grass[:, :, :3]).any(axis=2)
        im = river.copy()
        im[on_road] = road[on_road]
        if style == "a":
            return im
        cols = np.where(wet.any(axis=0))[0]
        x0, x1 = max(0, cols.min() - BANK), min(95, cols.max() + BANK)
        tops = [np.where(on_road[:, x])[0].min() for x in range(x0, x1 + 1)]
        bots = [np.where(on_road[:, x])[0].max() for x in range(x0, x1 + 1)]
        # The parapets stand on the road's outer edge; between them the deck
        # is the road's cobbles, borrowed from the road's solid middle where
        # its ragged edge dipped.
        t, b = min(tops) - 1, max(bots) + 1
        mt, mb = max(tops), min(bots)
        stone = road[on_road][:, :3].mean(axis=0)
        light = np.clip(stone * 0.4 + np.array(PALE) * 0.6, 0, 255)
        for x in range(x0, x1 + 1):
            im[:t, x] = river[:t, x]
            im[b + 1:, x] = river[b + 1:, x]
            for y in range(t + P, b - P + 1):
                if not on_road[y, x]:
                    im[y, x] = road[min(max(y, mt), mb), x]
            for y0, outer in ((t, t), (b - P + 1, b)):
                for y in range(y0, y0 + P):
                    c = light * (0.8 if x % 8 == 0 else 1.0)
                    if y == outer:
                        c = stone * LINE
                    im[y, x, :3] = c.astype(np.int32)
            if style == "c":
                for y in range(b + 1, min(96, b + 4)):
                    if wet[y, x]:
                        im[y, x, :3] = (river[y, x, :3] * SHADOW).astype(np.int32)
        return im

    for style in "abc":
        d = os.path.join(out, style)
        os.makedirs(d, exist_ok=True)
        ew = build(load("road_ew.png"), load("river_ns.png"), G, style)
        tr = lambda a: a.transpose(1, 0, 2)
        ns = tr(build(tr(load("road_ns.png")), tr(load("river_ew.png")), tr(G), style))
        for name, a in (("bridge_river_ew.png", ew), ("bridge_river_ns.png", ns)):
            Image.fromarray(a.astype(np.uint8), "RGBA").save(os.path.join(d, name))
    print(f"bridges a, b, c -> {out}")


# ==========================================================================
# siegeslice.py -- slice an overhead castle picture into the combat pieces and the siege screen
# ==========================================================================

def _siegeslice(argv):
    """Slice a 384x384 overhead castle picture into the 96px combat pieces and
compose the siege screen from them.

    python3 tools/romeart.py siegeslice <scene.png> <out-dir>          (recipe mode)
    python3 tools/romeart.py siegeslice <scene.png> <out-dir> --grid   (36 cells, untouched)
    python3 tools/romeart.py siegeslice <field.png> <out-dir> --field <prefix>
                                    (30 open-field cells: the largest centred 6:5 rectangle
                                     of content, scaled to 576x480)

The picture is a 4x4 grid of 96 cells: the top row is the back wall (with
its two corners), the side columns are the left and right walls, the bottom
row is the front wall with its two corners and the two broken ends either
side of the breach.

The straight runs are rebuilt from feature-free strips of the picture,
because the generated walls carry a gatehouse, trees and torches that would
repeat on every cell; the recipe below records which strips (settled on
build/art/siege_scene_map/run02, 2026-09-07):

  field_grass          cell (1,1), a plain interior cell
  castle_wall_back     wall rows from x 224..296 and x 48..72 of the top
                       wall (both have the wall at rows 22..69); the outside
                       band above and the courtyard foot below are plain grass
  castle_wall_04       left wall: x 0..48 of cell (0,2) over plain grass,
                       the outside strip x 0..24 also plain grass
  castle_wall_05       the left piece mirrored
  corners, broken ends, back corners: the picture's own cells

Pieces written (pack names, combat.c codes):
  castle_wall_back_l, castle_wall_back, castle_wall_back_r   (band above row 0)
  castle_wall_04 (left, 8), castle_wall_05 (right, 9)
  castle_wall_01 (bottom-left corner, 5), castle_wall_03 (broken end left, 7),
  castle_wall_06 (broken end right, 10), castle_wall_02 (bottom-right corner, 6)
  field_grass

Also writes siege_mock.png: the 6x5 grid plus the back band, laid out as
combat.c's castle_omap lays it out, with a few troop sprites on the field.
"""
    import os
    import sys
    from PIL import Image

    src = argv[1]
    out = argv[2]
    os.makedirs(out, exist_ok=True)
    im = Image.open(src).convert("RGBA")

    if "--field" in argv:
        # Field mode: an open-field ground picture (sprites.ui.field_grid, or a
        # zone's field_grid). The largest 6x5 rectangle of content centred in the
        # picture -- a meadow painted on white keeps its white out -- is scaled
        # down (Lanczos) to the 576x480 board and cut into the 6x5 grid of 96 px
        # cells, <prefix>_<x>_<y>.png. A picture with no margin uses its whole
        # width.
        prefix = argv[argv.index("--field") + 1]
        W, H, T = 6, 5, 96
        bw, bh = W * T, H * T
        px = im.load()
        def content(x, y):
            r, g, b, a = px[x, y]
            return a > 16 and not (r > 235 and g > 235 and b > 235)
        cx, cy = im.width // 2, im.height // 2
        # Shrink a centred 6:5 rectangle until every pixel on its edge is content.
        rw = min(im.width, im.height * W // H)
        rh = rw * H // W
        def edge_ok(rw, rh):
            x0, y0 = cx - rw // 2, cy - rh // 2
            x1, y1 = x0 + rw - 1, y0 + rh - 1
            if x0 < 0 or y0 < 0 or x1 >= im.width or y1 >= im.height: return False
            step = 4
            for x in range(x0, x1 + 1, step):
                if not content(x, y0) or not content(x, y1): return False
            for y in range(y0, y1 + 1, step):
                if not content(x0, y) or not content(x1, y): return False
            return True
        while rw > bw and not edge_ok(rw, rh):
            rw -= 6; rh = rw * H // W
        x0, y0 = cx - rw // 2, cy - rh // 2
        board = im.crop((x0, y0, x0 + rw, y0 + rh)).resize((bw, bh), Image.LANCZOS)
        for y in range(H):
            for x in range(W):
                board.crop((x * T, y * T, x * T + T, y * T + T)).save(
                    os.path.join(out, f"{prefix}_{x}_{y}.png"))
        print(f"{W * H} cells of {T}x{T}: the centre {rw}x{rh} of {im.size[0]}x{im.size[1]} scaled to {bw}x{bh}, in {out}")
        return

    if "--grid" in argv:
        # Grid mode: the whole picture is the siege board plus its back band, a
        # 6x6 grid of equal cells (sprites.ui.siege_grid). Every cell is written
        # untouched as cell_<x>_<y>.png at the picture's own cell size; the shell
        # scales each to the combat cell. No assembly, no mock.
        W, H = 6, 6
        assert im.width % W == 0 and im.height % H == 0, im.size
        cw, ch = im.width // W, im.height // H
        for y in range(H):
            for x in range(W):
                im.crop((x * cw, y * ch, x * cw + cw, y * ch + ch)).save(
                    os.path.join(out, f"cell_{x}_{y}.png"))
        print(f"{W * H} cells of {cw}x{ch} in {out}")
        return

    assert im.size == (384, 384), im.size
    T = 96


    def cell(cx, cy):
        return im.crop((cx * T, cy * T, cx * T + T, cy * T + T))


    grass = cell(1, 1)

    back = grass.copy()
    back.paste(im.crop((224, 0, 296, T)), (0, 0))
    back.paste(im.crop((48, 0, 72, T)), (72, 0))
    back.paste(grass.crop((0, 0, T, 22)), (0, 0))        # outside band above the wall
    back.paste(grass.crop((0, 72, T, T)), (0, 72))       # courtyard foot below it

    left = grass.copy()
    left.paste(cell(0, 2).crop((0, 0, 48, T)), (0, 0))
    left.paste(grass.crop((0, 0, 24, T)), (0, 0))        # outside strip
    right = left.transpose(Image.FLIP_LEFT_RIGHT)

    pieces = {
        "castle_wall_back_l": cell(0, 0),
        "castle_wall_back":   back,
        "castle_wall_back_r": cell(3, 0),
        "castle_wall_04":     left,
        "castle_wall_05":     right,
        "castle_wall_01":     cell(0, 3),
        "castle_wall_03":     cell(1, 3),
        "castle_wall_06":     cell(2, 3),
        "castle_wall_02":     cell(3, 3),
        "field_grass":        grass,
    }
    for name, p in pieces.items():
        p.save(os.path.join(out, name + ".png"))

    code = {8: "castle_wall_04", 9: "castle_wall_05", 5: "castle_wall_01",
            7: "castle_wall_03", 10: "castle_wall_06", 6: "castle_wall_02"}
    omap = [[8, 0, 0, 0, 0, 9]] * 4 + [[5, 7, 0, 0, 10, 6]]
    W, H = 6, 5
    mock = Image.new("RGBA", (W * T, (H + 1) * T))
    for x in range(W):
        name = "castle_wall_back_l" if x == 0 else "castle_wall_back_r" if x == W - 1 else "castle_wall_back"
        mock.paste(pieces["field_grass"], (x * T, 0))
        mock.paste(pieces[name], (x * T, 0), pieces[name])
    for y in range(H):
        for x in range(W):
            mock.paste(pieces["field_grass"], (x * T, (y + 1) * T))
            c = omap[y][x]
            if c:
                mock.paste(pieces[code[c]], (x * T, (y + 1) * T), pieces[code[c]])

    # a few troops where combat.c's castle_umap puts them (defenders top, attackers bottom)
    troops = "assets/glory-of-rome/art/troops"
    placements = {(1, 0): "praetoriani_00", (2, 0): "hastati_00", (3, 0): "velites_00", (4, 0): "equites_00",
                  (2, 1): "sarmatae_00", (1, 3): "ligures_00", (2, 3): "numidae_00", (3, 3): "gigantes_00",
                  (2, 4): "lares_00", (3, 4): "tirones_00"}
    for (x, y), n in placements.items():
        p = os.path.join(troops, n + ".png")
        if os.path.exists(p):
            s = Image.open(p).convert("RGBA")
            if x >= 1 and y <= 1:
                s = s.transpose(Image.FLIP_LEFT_RIGHT)   # defenders face left
            mock.paste(s, (x * T, (y + 1) * T), s)
    mock.save(os.path.join(out, "siege_mock.png"))
    print(f"{len(pieces)} pieces and siege_mock.png in {out}")


# ==========================================================================
# siegewalls.py -- remake the combat wall set from the original pieces
# ==========================================================================

def _siegewalls(argv):
    """Remake the Rome pack's combat set at the 96x96 cell from the original
48x34 pieces, programmatically: each original pixel is classified into a
material, the material map is scaled to the new cell (2x across, 96/34
down), and every material is re-rendered at pixel scale with the rules
pixel artists use (grout first, bricks in a three-tone bevel, two-tone
merlons over a black shadow band, a lighter top face, dithered banks,
scattered rubble). Layout, proportions, the moat's wobble, the corner step
and the breach diagonal therefore match the original exactly.

Output, art/combat/:
    castle_wall_01..06      as the engine's siege layout uses them
    castle_wall_back, _back_l, _back_r   the top-down back wall band
    castle_spike, obstacle_01..03, cursor_01..04, field_grass

    python3 tools/romeart.py siegewalls [pack-dir] [reference-pack-dir]
"""
    import math
    import os
    import shutil
    import sys
    from PIL import Image, ImageDraw

    PACK = argv[1] if len(argv) > 1 else "assets/glory-of-rome"
    REF = argv[2] if len(argv) > 2 else "assets/kings-bounty"
    OUT = os.path.join(PACK, "art", "combat")
    RC = os.path.join(REF, "art", "combat")
    N = 96
    CLEAR = (0, 0, 0, 0)

    # ---- palette (the original's stone, black and moat) ---------------------------
    MOAT = (93, 97, 255, 255)
    MOAT_DK = (0, 0, 154, 255)
    MOAT_LT = (150, 152, 255, 255)
    BLACK = (0, 0, 0, 255)
    GROUT = (32, 32, 32, 255)
    DARKLINE = (44, 44, 44, 255)
    STONE = (105, 105, 105, 255)
    STONE_DK = (77, 77, 77, 255)
    GREY = (85, 85, 85, 255)
    HILITE = (121, 121, 121, 255)
    MER_BRIGHT = (223, 223, 223, 255)
    MER_LIGHT = (178, 178, 178, 255)
    OLIVE = (65, 65, 0, 255)
    BROWN = (138, 89, 48, 255)
    BROWN_DK = (40, 32, 12, 255)
    CHAR = (60, 60, 60, 255)
    TREE = (0, 89, 89, 255)
    TREE_DK = (0, 65, 65, 255)
    TRUNK = (130, 93, 0, 255)
    POND_DK = (0, 93, 158, 255)


    def lcg(seed):
        s = seed & 0xFFFFFFFF
        while True:
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            yield s


    # ---- classify the original --------------------------------------------------

    def classify(p):
        """Material code for one original pixel; an unknown opaque colour is
    kept as itself (a tuple), so nothing the original drew is lost."""
        r, g, b, a = p
        if a == 0:
            return "."
        if (r, g, b) == (0, 0, 0):
            return "#"
        if b > 200 and r < 140:
            return "W"                       # moat water
        if (r, g, b) in ((0, 0, 154), (0, 125, 207), (0, 93, 158)):
            return "w"                       # dark water edge
        if g > 140 and r < 100 and b < 100:
            return " "                       # grass
        if (r, g, b) == (223, 223, 223):
            return "M"
        if (r, g, b) == (178, 178, 178):
            return "m"
        if (r, g, b) == (121, 121, 121):
            return "h"
        if (r, g, b) == (105, 105, 105):
            return "S"
        if (r, g, b) == (85, 85, 85):
            return "s"
        if (r, g, b) == (77, 77, 77):
            return "d"
        if (r, g, b) in ((32, 32, 32), (44, 44, 44)):
            return "g"
        if (r, g, b) == (65, 65, 0):
            return "o"
        if (r, g, b) == (60, 60, 60):
            return "c"
        if r > 100 and g < 100 and b < 80:
            return "B"                       # brown rubble
        if (r, g, b) == (0, 89, 89):
            return "T"
        if (r, g, b) == (0, 65, 65):
            return "t"
        if (r, g, b) == (130, 93, 0):
            return "k"                       # trunk
        return (r, g, b, 255)


    def material_map(name):
        im = Image.open(os.path.join(RC, name + ".png")).convert("RGBA")
        px = im.load()
        return [[classify(px[x, y]) for x in range(im.width)] for y in range(im.height)]


    def scaled(mm):
        """Nearest-neighbour scale of a material map to N x N."""
        h, w = len(mm), len(mm[0])
        return [[mm[min(h - 1, y * h // N)][min(w - 1, x * w // N)] for x in range(N)] for y in range(N)]


    # ---- re-render materials ------------------------------------------------------

    FLAT = {"#": BLACK, "M": MER_BRIGHT, "m": MER_LIGHT, "h": HILITE, "S": STONE, "s": GREY,
            "d": STONE_DK, "g": GROUT, "o": OLIVE, "c": CHAR, "B": BROWN, "T": TREE, "t": TREE_DK,
            "k": TRUNK, "?": None, ".": None, " ": None}


    def render(mm, seed=1, relay_bricks=True):
        """Draw a scaled material map at N x N with pixel-scale detail."""
        sm = scaled(mm)
        im = Image.new("RGBA", (N, N), CLEAR)
        px = im.load()
        r = lcg(seed)
        # 1. flat pass
        for y in range(N):
            for x in range(N):
                c = sm[y][x]
                if isinstance(c, tuple):
                    px[x, y] = c
                elif c == "W":
                    px[x, y] = MOAT
                elif c == "w":
                    px[x, y] = MOAT_DK
                elif FLAT.get(c):
                    px[x, y] = FLAT[c]
        # 2. moat: ragged bank (dither the water/grass boundary) and ripples
        for y in range(N):
            for x in range(N):
                if isinstance(sm[y][x], str) and sm[y][x] in "Ww":
                    nb = [sm[yy][xx] for yy, xx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1))
                          if 0 <= yy < N and 0 <= xx < N]
                    if " " in nb or "." in nb:
                        px[x, y] = MOAT_DK if (x + y) % 2 == 0 else (CLEAR if next(r) % 3 == 0 else MOAT_DK)
                    elif sm[y][x] == "W" and next(r) % 23 == 0:
                        for k in range(3):
                            if x + k < N and sm[y][x + k] == "W":
                                px[x + k, y] = MOAT_LT
        # 3. brick faces: re-lay courses inside the brick mask (S/d/g/o runs below a grout line)
        if relay_bricks:
            brick = [[isinstance(sm[y][x], str) and sm[y][x] in "Sdgo" and y >= 40 for x in range(N)] for y in range(N)]
            course = 8
            row = 0
            for y0 in range(40, N, course):
                off = 6 if row % 2 else 0
                for x0 in range(-off, N, 12):
                    for y in range(y0, min(y0 + course, N)):
                        tone = STONE if y - y0 < 3 else (STONE_DK if y - y0 < 6 else GROUT)
                        for x in range(x0, x0 + 12):
                            if 0 <= x < N and brick[y][x]:
                                if x == x0 + 11 or x == x0 + 10:
                                    px[x, y] = GROUT
                                else:
                                    px[x, y] = tone
                    # an olive stone now and then
                    if next(r) % 7 == 0 and 0 <= x0 + 2 < N:
                        for y in range(y0 + 3, min(y0 + 6, N)):
                            for x in range(x0 + 2, x0 + 9):
                                if 0 <= x < N and brick[y][x]:
                                    px[x, y] = OLIVE
                row += 1
        return im



    # ---- the moat as a shape ----------------------------------------------------
    MOAT_A, MOAT_B = 8, 30          # the water band lies between these, from the outer edge
    R_OUT = 38                      # rounded outer corner radius
    R_IN = 14                       # rounded inner bank radius


    def wobble(t, phase=0.0):
        """Bank wander along a run. Period N, so neighbouring cells continue it."""
        return round(2.5 * math.sin(2 * math.pi * t / N + phase) + 1.5 * math.sin(4 * math.pi * t / N + 2.1 + phase))


    def water_side(xo, y):
        """Water for a run along the left edge (xo measured from that edge)."""
        return MOAT_A + wobble(y, 0.7) <= xo < MOAT_B + wobble(y, 2.9)


    def water_top(x, y):
        return MOAT_A + wobble(x, 1.3) <= y < MOAT_B + wobble(x, 4.0)


    def water_corner(xo, y):
        """The moat turning a corner: outside both outer banks, inside either
    inner bank (or the fillet that rounds the inner corner), and inside the
    arc that rounds the outer corner."""
        if xo < MOAT_A + wobble(y, 0.7) or y < MOAT_A + wobble(xo, 1.3):
            return False                                   # beyond an outer bank
        if xo < R_OUT and y < R_OUT and (R_OUT - xo) ** 2 + (R_OUT - y) ** 2 > R_OUT * R_OUT:
            return False                                   # the rounded outer corner
        if xo < MOAT_B + wobble(y, 2.9) or y < MOAT_B + wobble(xo, 4.0):
            return True                                    # inside an inner bank
        cx = cy = MOAT_B + R_IN
        return xo < cx and y < cy and (cx - xo) ** 2 + (cy - y) ** 2 > R_IN * R_IN   # the inner fillet


    def paint_moat(im, member, left=True):
        """Fill the member(xo, y) region with water and give it a clean two-pixel
    dark bank, as the original: flat water, dark edge, the odd lighter fleck."""
        px = im.load()
        water = [[member(x if left else N - 1 - x, y) for x in range(N)] for y in range(N)]
        r = lcg(5)
        for y in range(N):
            for x in range(N):
                if not water[y][x]:
                    continue
                edge = False
                for dy in (-2, -1, 0, 1, 2):
                    for dx in (-2, -1, 0, 1, 2):
                        xx, yy = x + dx, y + dy
                        if 0 <= xx < N and 0 <= yy < N and not water[yy][xx] and abs(dx) + abs(dy) <= 2:
                            edge = True
                px[x, y] = MOAT_DK if edge else (POND_DK if next(r) % 41 == 0 else MOAT)
        return im


    def clear_moat_region(im, left=True):
        """Remove the scaled original's moat pixels so the shaped moat replaces them."""
        px = im.load()
        for y in range(N):
            for x in range(N):
                if px[x, y] in (MOAT, MOAT_DK, MOAT_LT, POND_DK, CLEAR):
                    px[x, y] = CLEAR
        return im


    def flip(im):
        return im.transpose(Image.FLIP_LEFT_RIGHT)


    def wall_piece(name, seed):
        im = render(material_map(name), seed)
        if name in ("castle_wall_04", "castle_wall_01"):
            paint_moat(clear_moat_region(im), water_side, left=True)
        if name in ("castle_wall_05", "castle_wall_02"):
            paint_moat(clear_moat_region(im), water_side, left=False)
        return im


    def back_wall():
        # the side wall turned so the moat lies along the top
        im = render(material_map("castle_wall_04"), 21)
        clear_moat_region(im)
        im = im.transpose(Image.ROTATE_270)   # left edge to the top
        return paint_moat(im, lambda xo, y: water_top(xo, y))


    def back_corner(left):
        """The band's end cell: the back wall across the top joined to the side
    wall coming down, with the moat turning the corner on a curve: a rounded
    outer corner at the cell's corner and a rounded inner bank, both with
    the same ragged dithered edge as the straight runs."""
        top = back_wall()
        side = wall_piece("castle_wall_04" if left else "castle_wall_05", 23)
        im = Image.new("RGBA", (N, N), CLEAR)
        im.alpha_composite(side)
        mask = Image.new("L", (N, N), 0)
        ImageDraw.Draw(mask).rectangle((0, 0, N - 1, 67), fill=255)
        im.paste(top, (0, 0), mask)
        # the two wall bands meet on a mitre: in the corner square where both
        # bands run (36..67 on each axis, measured from the outer edge), pixels
        # below the diagonal belong to the side band, above it to the back band,
        # so every stripe of one band turns the corner into the same stripe of
        # the other.
        sp, tp, ip = side.load(), top.load(), im.load()
        for y in range(36, 68):
            for xo in range(0, 68):
                x = xo if left else N - 1 - xo
                if xo < 36:
                    ip[x, y] = sp[x, y]                       # outside the side wall: its moat gap
                else:
                    ip[x, y] = sp[x, y] if (y - 36) > (xo - 36) else tp[x, y]
        clear_moat_region(im)
        paint_moat(im, water_corner, left=left)
        return im


    def soften(im, seed=7):
        """Dither the boundary between any two different colours so a scaled
    piece does not read as 2x blocks: along each boundary, swap every other
    pixel with its neighbour's colour."""
        px = im.load()
        r = lcg(seed)
        src = im.copy().load()
        for y in range(1, N - 1):
            for x in range(1, N - 1):
                here = src[x, y]
                for nx, ny in ((x + 1, y), (x, y + 1)):
                    there = src[nx, ny]
                    if there != here and (x + y) % 2 == 0 and next(r) % 2 == 0:
                        px[x, y] = there
        return im


    def obstacle(name, seed):
        return soften(render(material_map(name), seed, relay_bricks=False), seed)



    # ---- obstacles, drawn: rubble, bushes, a broken wall ----------------------------

    BUSH = (34, 110, 40, 255)
    BUSH_LT = (70, 150, 60, 255)
    BUSH_DK = (18, 70, 28, 255)
    SHADOW = (0, 0, 0, 90)


    def cast_shadow(d, box):
        x0, y0, x1, y1 = box
        d.ellipse((x0, y0, x1, y1), fill=SHADOW)


    def stone_block(d, box, seed, tones=(HILITE, STONE, STONE_DK)):
        """One rounded rubble stone with a lit top, mid body, dark underside and an ink edge."""
        x0, y0, x1, y1 = box
        d.rounded_rectangle((x0, y0, x1, y1), radius=3, fill=tones[1], outline=GROUT)
        d.line((x0 + 2, y0 + 1, x1 - 2, y0 + 1), fill=tones[0])
        d.line((x0 + 2, y1 - 1, x1 - 2, y1 - 1), fill=tones[2])
        d.line((x1 - 1, y0 + 2, x1 - 1, y1 - 2), fill=tones[2])


    def rubble():
        """A heap of fallen masonry: large blocks on top of small debris, olive
    stones among them, a shadow underneath."""
        im = Image.new("RGBA", (N, N), CLEAR)
        d = ImageDraw.Draw(im)
        r = lcg(51)
        cast_shadow(d, (10, 58, 88, 90))
        for _ in range(90):
            x, y = 14 + next(r) % 68, 44 + next(r) % 42
            if (x - 48) ** 2 / 36 ** 2 + (y - 68) ** 2 / 20 ** 2 < 1:
                d.point((x, y), fill=(STONE_DK, GROUT, OLIVE, STONE)[next(r) % 4])
        blocks = [(16, 62, 40, 78), (44, 66, 70, 82), (30, 48, 56, 64), (60, 52, 82, 66),
                  (22, 74, 44, 86), (52, 76, 78, 88), (38, 36, 60, 50), (66, 42, 84, 54)]
        for i, b in enumerate(blocks):
            tone = (HILITE, STONE, STONE_DK) if i % 3 else (STONE, STONE_DK, GROUT)
            if i == 4:
                tone = (OLIVE, OLIVE, GROUT)
            stone_block(d, b, seed=i, tones=tone)
        return im


    def bushes():
        """A clump of rounded bushes with a lit crown, a dark underside, leaf
    flecks, and a shadow on the ground."""
        im = Image.new("RGBA", (N, N), CLEAR)
        d = ImageDraw.Draw(im)
        r = lcg(61)
        cast_shadow(d, (12, 66, 84, 90))
        for (cx, cy, rx, ry) in ((30, 60, 22, 18), (62, 56, 24, 20), (46, 44, 18, 15), (46, 70, 16, 12)):
            d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=BUSH, outline=BUSH_DK)
            d.chord((cx - rx, cy - ry, cx + rx, cy + ry), 20, 160, fill=BUSH_DK)
            d.ellipse((cx - rx + 4, cy - ry + 3, cx + rx - 8, cy - 2), fill=BUSH_LT)
            for _ in range(40):
                x, y = cx - rx + next(r) % (2 * rx), cy - ry + next(r) % (2 * ry)
                if (x - cx) ** 2 / rx ** 2 + (y - cy) ** 2 / ry ** 2 < 0.8:
                    d.point((x, y), fill=(BUSH_LT, BUSH_DK, BUSH)[next(r) % 3])
        return im


    def broken_wall():
        """A stub of ruined wall: brick courses standing on a base, the top edge
    broken to a jagged line, rubble at its feet."""
        im = Image.new("RGBA", (N, N), CLEAR)
        d = ImageDraw.Draw(im)
        px = im.load()
        r = lcg(71)
        cast_shadow(d, (8, 70, 90, 92))
        x0, x1 = 14, 82
        tops = []
        h = 40
        for x in range(x0, x1):
            if next(r) % 5 == 0:
                h += (next(r) % 9) - 4
            h = max(28, min(56, h))
            tops.append(h)
        course = 8
        row = 0
        for y in range(24, 86, course):
            off = 6 if row % 2 else 0
            for bx in range(x0 - off, x1, 12):
                for x in range(bx, bx + 12):
                    if not (x0 <= x < x1):
                        continue
                    for yy in range(max(y, tops[x - x0]), min(y + course, 84)):
                        tone = HILITE if yy - y < 2 else (STONE if yy - y < 5 else STONE_DK)
                        if x >= bx + 10 or yy >= y + course - 1:
                            tone = GROUT
                        px[x, yy] = tone
            row += 1
        for x in range(x0, x1):
            t = tops[x - x0]
            px[x, t] = GROUT
            px[x, t + 1] = MER_LIGHT
        d.line((x0, tops[0], x0, 84), fill=GROUT)
        d.line((x1 - 1, tops[-1], x1 - 1, 84), fill=GROUT)
        d.rectangle((x0, 84, x1 - 1, 87), fill=GROUT)
        for _ in range(60):
            x, y = 6 + next(r) % 84, 80 + next(r) % 12
            px[x, y] = (STONE_DK, STONE, GROUT, OLIVE)[next(r) % 4]
        for b in ((4, 78, 16, 86), (78, 80, 92, 88), (40, 84, 54, 92)):
            stone_block(d, b, 1)
        return im


    def burst():
        return soften(render(material_map("castle_spike"), 31, relay_bricks=False), 31)


    def cursor(name):
        im = Image.open(os.path.join(RC, name + ".png")).convert("RGBA")
        # the ring, re-drawn at the new size from its bounding box
        bb = im.getbbox()
        col = None
        px = im.load()
        for y in range(im.height):
            for x in range(im.width):
                if px[x, y][3] and col is None:
                    col = px[x, y]
        out = Image.new("RGBA", (N, N), CLEAR)
        d = ImageDraw.Draw(out)
        w = (bb[2] - bb[0]) * 2
        h = round((bb[3] - bb[1]) * N / 34)
        rad = min(w, h) // 2
        for k in range(3):
            d.ellipse((48 - rad + k, 48 - rad + k, 48 + rad - k, 48 + rad - k), outline=col)
        return out


    def main():
        os.makedirs(OUT, exist_ok=True)
        pieces = {
            "castle_wall_01": wall_piece("castle_wall_01", 1),
            "castle_wall_02": wall_piece("castle_wall_02", 2),
            "castle_wall_03": wall_piece("castle_wall_03", 3),
            "castle_wall_04": wall_piece("castle_wall_04", 4),
            "castle_wall_05": wall_piece("castle_wall_05", 5),
            "castle_wall_06": wall_piece("castle_wall_06", 6),
            "castle_wall_back": back_wall(),
            "castle_wall_back_l": back_corner(True),
            "castle_wall_back_r": back_corner(False),
            "castle_spike": burst(),
            # obstacle_01..03 are generated (art/jobs/obstacle_0N.json), not drawn
            "cursor_01": cursor("cursor_01"),
            "cursor_02": cursor("cursor_02"),
            "cursor_03": cursor("cursor_03"),
            "cursor_04": cursor("cursor_04"),
        }
        for name, im in pieces.items():
            im.save(os.path.join(OUT, name + ".png"))
        shutil.copy(os.path.join(PACK, "art", "tiles", "grass.png"), os.path.join(OUT, "field_grass.png"))
        print("wrote", len(pieces) + 1, "pieces to", OUT)


    main()


# ==========================================================================
# fieldcalm.py -- calm a field painting and cut its cells
# ==========================================================================

def _fieldcalm(argv):
    """Calm a supplied field painting: mask its clutter, fill the mask from the
painting's own texture, and cut the 6x5 field cells with siegeslice.

    python3 tools/romeart.py fieldcalm <painting.png> <out-dir> <prefix> [--level light|medium|strong] [--fill patchmatch|lama] [--mask mask.png]

Levels (what is repainted):
  light   grey boulders and small rocks, dark clumps (ferns, dense clover)
  medium  light + brown earth patches
  strong  medium + mid-dark leaf clusters and pale yellow-green moss patches
The mask is colour/size thresholds (OpenCV), written to <out-dir>/mask.png so a
hand-edited copy can be passed back with --mask. Fill: G'MIC inpaint_matchpatch
(native resolution) or LaMa on CPU (run at 1024 px). Writes <out-dir>/mask.png,
<out-dir>/inpainted.png and the cells <out-dir>/cells/<prefix>_<x>_<y>.png.
Needs: gmic, python3-opencv, python3-numpy (apt); for --fill lama: torch (cpu),
torchvision, simple-lama-inpainting (pip --user).
Neither fill is deterministic (patch matching is randomly initialised, LaMa is
run at 1024 px), so the inpainted painting that shipped is kept beside the
source; re-running gives an equivalent but not identical field.
"""
    import subprocess, argparse
    import cv2, numpy as np

    ap = argparse.ArgumentParser(prog="romeart.py fieldcalm")
    ap.add_argument("painting"); ap.add_argument("out"); ap.add_argument("prefix")
    ap.add_argument("--level", default="medium", choices=["light", "medium", "strong"])
    ap.add_argument("--fill", default="patchmatch", choices=["patchmatch", "lama"])
    ap.add_argument("--mask", help="use this mask instead of building one (white = repaint)")
    ap.add_argument("--dilate", type=int, default=15)
    a = ap.parse_args(argv[1:])
    os.makedirs(a.out + "/cells", exist_ok=True)
    src = cv2.imread(a.painting, cv2.IMREAD_UNCHANGED)
    if src.shape[2] == 3: src = np.dstack([src, np.full(src.shape[:2], 255, np.uint8)])
    bgr, alpha = src[:, :, :3], src[:, :, 3]
    K = lambda n: cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (n, n))

    def blobs(mask, min_area):
        n, lab, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8); keep = np.zeros_like(mask)
        for i in range(1, n):
            if stats[i, cv2.CC_STAT_AREA] >= min_area: keep |= (lab == i)
        return keep

    if a.mask:
        mask = cv2.imread(a.mask, cv2.IMREAD_GRAYSCALE)
    else:
        hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV); H, S, V = [hsv[:, :, i].astype(int) for i in range(3)]
        content = (alpha > 16) & ~((bgr[:, :, 0] > 235) & (bgr[:, :, 1] > 235) & (bgr[:, :, 2] > 235))
        rocks = blobs(content & (S < 0.20 * 255) & (V > 0.42 * 255), 120)
        dark = blobs(content & (V < 0.40 * 255), 120)
        m = rocks | dark
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
        r = subprocess.run(["gmic", a.painting, "-channels[0]", "0,2", a.out + "/mask.png", "-inpaint_matchpatch[0]", "[1],0,15,10,7,1", "-keep[0]", "-o", rgb_p], capture_output=True, text=True)
        if r.returncode: sys.exit("gmic failed: " + r.stderr[-400:])
        rgb = cv2.imread(rgb_p); out = np.dstack([rgb, alpha])
    else:
        from PIL import Image
        from simple_lama_inpainting import SimpleLama
        S = 1024
        rgb_s = cv2.resize(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), (S, S), interpolation=cv2.INTER_AREA)
        m_s = cv2.resize(mask, (S, S), interpolation=cv2.INTER_NEAREST)
        res = np.array(SimpleLama()(Image.fromarray(rgb_s), Image.fromarray(m_s)).convert("RGB"))[:S, :S]
        out = np.dstack([cv2.cvtColor(res, cv2.COLOR_RGB2BGR), cv2.resize(alpha, (S, S), interpolation=cv2.INTER_AREA)])
    cv2.imwrite(a.out + "/inpainted.png", out)
    _siegeslice(["romeart", a.out + "/inpainted.png", a.out + "/cells", "--field", a.prefix])


# ==========================================================================
# splashlogo.py -- compose the publisher splash
# ==========================================================================

def _splashlogo(argv):
    """Compose the publisher splash (art/ui/splash_logo.png, 320x84, transparent)
from locally rendered C059 Bold lettering and one generated 44x44 emblem.

    python3 tools/romeart.py splashlogo build/art/splash_logo_emblem/run02/01_raw.png build/art/splash_logo_new.png
"""
    import sys
    from PIL import Image, ImageDraw, ImageFont

    FONT = "/usr/share/fonts/opentype/urw-base35/C059-Bold.otf"


    def glyph_mask(text, size):
        f = ImageFont.truetype(FONT, size)
        m = Image.new("L", (400, 80), 0)
        ImageDraw.Draw(m).text((4, 4), text, font=f, fill=255)
        m = m.point(lambda v: 255 if v >= 128 else 0)
        return m.crop(m.getbbox())


    def bitmap_text(text, size):
        m = glyph_mask(text, size)
        w, h = m.size
        out = Image.new("RGBA", (w + 3, h + 3), (0, 0, 0, 0))
        for off in ((1, 1), (2, 2), (0, 2), (2, 0)):
            out.paste(Image.new("RGBA", m.size, (180, 20, 20, 255)), off, m)
        out.paste(Image.new("RGBA", m.size, (255, 255, 255, 255)), (0, 0), m)
        return out


    def coin(d, x, y, r):
        d.ellipse((x - r, y - r, x + r, y + r), fill=(214, 160, 40, 255), outline=(120, 80, 10, 255))
        d.ellipse((x - r + 2, y - r + 2, x - r + 4, y - r + 4), fill=(255, 236, 150, 255))


    def star(d, x, y, c):
        for i in range(-3, 4):
            d.point((x + i, y), c)
            d.point((x, y + i), c)


    def compose(emblem_path):
        canvas = Image.new("RGBA", (320, 84), (0, 0, 0, 0))
        d = ImageDraw.Draw(canvas)
        left = bitmap_text("Dan", 34)
        right = bitmap_text("Heskett", 34)
        presents = bitmap_text("Presents...", 24)
        emb = Image.open(emblem_path).convert("RGBA")
        gap = 6
        total = left.width + gap + emb.width + gap + right.width
        x0 = (320 - total) // 2
        canvas.paste(left, (x0, 16), left)
        ex = x0 + left.width + gap
        canvas.alpha_composite(emb, (ex, 4))
        canvas.paste(right, (ex + emb.width + gap, 16), right)
        canvas.paste(presents, ((320 - presents.width) // 2, 58), presents)
        coin(d, 60, 58, 7)
        coin(d, 262, 52, 5)
        coin(d, 292, 44, 4)
        for (x, y, c) in ((88, 50, (90, 160, 255, 255)), (120, 60, (90, 160, 255, 255)),
                          (226, 58, (255, 90, 220, 255)), (250, 36, (90, 160, 255, 255))):
            star(d, x, y, c)
        return canvas


    compose(argv[1]).save(argv[2])
    print("wrote", argv[2])


# ==========================================================================
# splashtitle.py -- compose the title screen
# ==========================================================================

def _splashtitle(argv):
    """Compose the title screen (art/ui/splash_title.png, 256x164): the generated
eagle standard (build/art/splash_title/run01/01_raw.png) with the title
words rendered locally from C059 Bold at 1-bit, gold with a dark offset
shading, in the empty band across the top. Same route as the publisher
splash (romeart.py splashlogo): generated lettering garbles, drawn lettering
does not.

    python3 tools/romeart.py splashtitle <eagle.png> <out.png>
    python3 tools/romeart.py splashtitle --words <out.png>

--words writes only the lettering on a transparent 256x164 field: the modern
title screen composites it over the purple, then the battle, in code.
"""
    import sys
    from PIL import Image, ImageDraw, ImageFont

    FONT = "/usr/share/fonts/opentype/urw-base35/C059-Bold.otf"
    GOLD = (236, 200, 90, 255)
    GOLD_DK = (150, 100, 20, 255)
    INK = (60, 30, 70, 255)


    def glyph_mask(text, size):
        f = ImageFont.truetype(FONT, size)
        m = Image.new("L", (400, 80), 0)
        ImageDraw.Draw(m).text((4, 4), text, font=f, fill=255)
        m = m.point(lambda v: 255 if v >= 128 else 0)
        return m.crop(m.getbbox())


    def bitmap_text(text, size, col, shade):
        m = glyph_mask(text, size)
        w, h = m.size
        out = Image.new("RGBA", (w + 3, h + 3), (0, 0, 0, 0))
        for off in ((1, 1), (2, 2)):
            out.paste(Image.new("RGBA", m.size, shade), off, m)
        out.paste(Image.new("RGBA", m.size, col), (0, 0), m)
        return out


    def compose(eagle_path):
        base = (Image.new("RGBA", (256, 164), (0, 0, 0, 0)) if eagle_path is None
                else Image.open(eagle_path).convert("RGBA"))
        W, H = base.size
        line1 = bitmap_text("OPEN BOUNTY", 20, GOLD, GOLD_DK)
        line2 = bitmap_text("THE GLORY OF ROME", 13, GOLD, GOLD_DK)
        # the title in the clear band above the eagle, the subtitle across the
        # pole at the foot of the picture; a dark halo keeps both legible
        for im, y in ((line1, 2), (line2, H - line2.height - 4)):
            x = (W - im.width) // 2
            halo = Image.new("RGBA", im.size, INK)
            for off in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                base.paste(halo, (x + off[0], y + off[1]), im)
            base.alpha_composite(im, (x, y))
        return base


    if argv[1] == "--words":
        compose(None).save(argv[2])
        print("wrote", argv[2])
    else:
        compose(argv[1]).save(argv[2])
        print("wrote", argv[2])


# ==========================================================================
# classpicker.py -- pre-render the class-select carousel
# ==========================================================================

def _classpicker(argv):
    """Pre-render the Rome class-select carousel.

Writes art/ui/class_select_picker_0..3.png beside class_select_picker.png: the
same painting with every figure but one dimmed, and that figure ringed in gold.
The painting itself is read only, never changed.

Which pixels belong to which figure: the landscape is found by scanning each
column down from the top edge to the first dark outline pixel (plus short
sideways steps under overhangs); the figures overlap, so three hand-placed
dividing lines decide whose pixel is whose, and three small hand fixes remain.

    python3 tools/romeart.py classpicker [pack dir]   (default assets/glory-of-rome)
"""
    import os, sys
    from PIL import Image, ImageChops, ImageFilter

    PACK = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'assets', 'glory-of-rome')
    SRC = os.path.join(PACK, 'art', 'ui', 'class_select_picker.png')
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
        dst = os.path.join(PACK, 'art', 'ui', 'class_select_picker_%d.png' % k)
        out.convert('RGB').save(dst)
        print(dst)


# ==========================================================================
# loopreview.py -- review page for an animation run
# ==========================================================================

def _loopreview(argv):
    """Review page for an animation run: a gif that clears between frames,
a 1x strip, a 5x strip, and per-frame checks.

    python3 tools/romeart.py loopreview build/art/<id>/runNN [--scale 5]

Writes preview.gif, strip1x.png, strip5x.png and review.html into the run
dir and prints the checks:

  size       every frame is the same size
  clip       opaque pixels touching a canvas edge (a clipped limb or weapon)
  halo       near-white opaque pixels next to transparency (flatten fringe)
  opaque     opaque pixel count per frame (a redrawn figure jumps by a lot)
  moved      pixels changed against frame 0 (motion) and, of those, how many
             are in the bottom quarter (feet should stay planted)
  gif        every decoded gif frame equals its source frame

The gif is written with disposal 2 so each frame replaces the last; without
it PIL paints frames over one another and a loop looks smeared.
"""
    import os
    import sys
    from PIL import Image, ImageChops, ImageDraw

    run = argv[1].rstrip("/")
    scale = int(argv[argv.index("--scale") + 1]) if "--scale" in argv else 5
    names = sorted(f for f in os.listdir(run) if f.startswith("frame_") and f.endswith(".png"))
    frames = [Image.open(os.path.join(run, f)).convert("RGBA") for f in names]
    if not frames:
        sys.exit(f"no frame_*.png in {run}")
    w, h = frames[0].size


    def opaque(im):
        return im.getchannel("A").point(lambda v: 255 if v > 0 else 0)


    def clip(im):
        a = opaque(im)
        bb = a.getbbox()
        return [s for s, hit in (("top", bb[1] == 0), ("bottom", bb[3] == h),
                                 ("left", bb[0] == 0), ("right", bb[2] == w)) if hit]


    def halo(im):
        px = im.load()
        n = 0
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a == 0 or min(r, g, b) < 200:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < w and 0 <= yy < h and px[xx, yy][3] == 0:
                        n += 1
                        break
        return n


    def moved(im, ref):
        d = ImageChops.difference(im, ref).convert("L").point(lambda v: 255 if v > 24 else 0)
        px = d.load()
        tot = feet = 0
        for y in range(h):
            for x in range(w):
                if px[x, y]:
                    tot += 1
                    if y >= h * 3 // 4:
                        feet += 1
        return tot, feet


    rows = []
    for i, f in enumerate(frames):
        o = sum(1 for v in opaque(f).getdata() if v)
        t, ft = moved(f, frames[0])
        rows.append((names[i], f.size, clip(f), halo(f), o, t, ft))

    gif = os.path.join(run, "preview.gif")
    big = [f.resize((w * 3, h * 3), Image.NEAREST) for f in frames]
    big[0].save(gif, save_all=True, append_images=big[1:], duration=150, loop=0,
                disposal=2, transparency=0)

    # verify the gif clears: decode every frame and compare to its source
    g = Image.open(gif)
    gif_ok = True
    for i in range(g.n_frames):
        g.seek(i)
        dec = g.convert("RGBA")
        ref = big[i]
        diff = ImageChops.difference(dec, ref).convert("L").point(lambda v: 255 if v > 40 else 0)
        bad = sum(1 for v in diff.getdata() if v)
        if bad > (w * h * 9) // 100:   # more than 1% of pixels off
            gif_ok = False

    for s, out in ((1, "strip1x.png"), (scale, f"strip{scale}x.png")):
        strip = Image.new("RGB", (len(frames) * (w * s + 8) + 8, h * s + 16), (60, 60, 60))
        d = ImageDraw.Draw(strip)
        for i, f in enumerate(frames):
            b = f.resize((w * s, h * s), Image.NEAREST)
            strip.paste(b, (8 + i * (w * s + 8), 12), b)
            d.text((8 + i * (w * s + 8), 0), f"frame {i}", fill=(230, 230, 230))
        strip.save(os.path.join(run, out))

    print(f"{run}: {len(frames)} frames {w}x{h}")
    print(f"  gif clears between frames: {'yes' if gif_ok else 'NO'}")
    for n, sz, c, hl, o, t, ft in rows:
        print(f"  {n} size {sz} clip {c or 'none'} halo {hl} opaque {o} moved {t} (feet region {ft})")

    html = f"""<!doctype html><meta charset=utf-8><title>{run}</title>
<style>body{{background:#3c3c3c;color:#ddd;font:12px sans-serif;padding:16px}}img{{image-rendering:pixelated;display:block;margin-bottom:12px}}</style>
<h2>{run}</h2>
<img src="/{run}/preview.gif">
<img src="/{run}/strip1x.png">
<img src="/{run}/strip{scale}x.png" style="max-width:100%">
<pre>{chr(10).join(f'{n} clip {c or "none"} halo {hl} opaque {o} moved {t} feet {ft}' for n, sz, c, hl, o, t, ft in rows)}
gif clears between frames: {'yes' if gif_ok else 'NO'}</pre>
"""
    open(os.path.join(run, "review.html"), "w").write(html)
    print(f"  page: {run}/review.html")


# ==========================================================================
# introtheme.py -- the Introduction's theme, synthesised
# ==========================================================================

def _introtheme(argv):
    """The Introduction's theme: an original piece, synthesised here, so it
    carries no licence or credit.

    python3 tools/romeart.py introtheme <out.ogg> [--length S] [--level L]

A lyre (Karplus-Strong plucked string) plays slow broken chords over a soft
E/B drone for the whole piece. One guest a minute fades in over 8 s and out
over 10 s: the aulos at 1:00, frame drum and finger cymbals at 2:00, pan
pipes at 3:00; the lyre is alone again from 4:00 to the fade. Greek Dorian
mode (E F G A B C D), 66 bpm. --length is the intro's running time (252.3 s);
--level scales the whole mix (0.25: background, Dan 2026-10-04).
The random source is seeded, so the same arguments give the same file.
Needs oggenc.
    """
    import argparse, subprocess, tempfile, wave
    import numpy as np
    ap = argparse.ArgumentParser(prog="romeart introtheme")
    ap.add_argument("out")
    ap.add_argument("--length", type=float, default=252.3)
    ap.add_argument("--level", type=float, default=0.25)
    a = ap.parse_args(argv[1:])

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
        subprocess.run(["oggenc", "-Q", "-q", "4", wav, "-o", a.out], check=True)
    print(f"{a.out}  {LENGTH:.0f} s, peak {0.6 * a.level:.2f}")


# ==========================================================================
# PAID (NETWORK) -- every call that leaves this machine
# ==========================================================================
#
# Nothing above this line reaches the network. The commands below do, and
# four of them spend: rdgen run, pltileset, pltilespro and sprites. Each
# describes what it would post and what that costs, and posts only with --run
# (#143). rdgen cost, rdgen balance and rdgen reprocess (its free downscale)
# are network calls that charge nothing.

PIXELLAB_API = "https://api.pixellab.ai/v2"
PIXELLAB_TOKEN = "~/.config/pixellab/token"


def _pixellab(method, path, body=None, timeout=120):
    """One PixelLab request: (HTTP status, the JSON reply -- or its text when
    the reply is not JSON). Token at ~/.config/pixellab/token."""
    import urllib.error
    import urllib.request
    tok = open(os.path.expanduser(PIXELLAB_TOKEN)).read().strip()
    r = urllib.request.Request(
        PIXELLAB_API + path,
        data=json.dumps(body).encode() if body is not None else None,
        method=method,
        headers={"Authorization": "Bearer " + tok,
                 "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=timeout) as f:
            return f.status, json.loads(f.read())
    except urllib.error.HTTPError as e:
        raw = e.read() or b"{}"
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, raw.decode(errors="replace")[:500]


def _download(url):
    """A generated file from PixelLab's storage, which refuses urllib's own
    user agent."""
    import urllib.request
    dl = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    with urllib.request.urlopen(dl, timeout=120) as f:
        return f.read()


def _paid_gate(argv, what, body, cost="PixelLab quotes no price before a call"):
    """True when argv carries --run. Otherwise print what would be posted,
    its cost and the balance, post nothing, and return False."""
    if "--run" in argv:
        return True

    def short(v):
        if isinstance(v, str) and len(v) > 80:
            return v[:40] + f"... ({len(v)} chars)"
        if isinstance(v, dict):
            return {k: short(x) for k, x in v.items()}
        if isinstance(v, list):
            return [short(x) for x in v]
        return v
    print(f"PAID: {what}")
    print(json.dumps(short(body), indent=1))
    print(f"cost: {cost}")
    st, bal = _pixellab("GET", "/balance")
    print("balance:", str(bal)[:200])
    print("dry run; nothing posted (add --run to post and be charged)")
    return False


def cmd_sprites(argv):
    """Run a PixelLab sprite-batch job (rocks, trees) and keep its sprites.

    python3 tools/romeart.py sprites art/jobs/<zone>_o96_rocks.json art/primitives/<zone>/rocks [--run]   (PAID)

The job file is the repo's own record: "description" (the shared prompt),
"batches" (lists of four item descriptions, one POST /create-1-direction-object
per list) and "_note". Each call is size 96, view top-down, and costs 20-40
subscription generations. For every batch this writes body<N>.json (what was
posted), meta<N>.json (the object as GET /v2/objects returned it) and the
candidate frames as tile_<NN>.png numbered across batches, as
art/primitives/africa/rocks was kept. Token at ~/.config/pixellab/token.
Nothing is posted without --run (2026-09-27, Italia rocks for #67; before that
the batches were posted by hand).
    """
    import io, time
    if len(argv) < 2:
        sys.exit("usage: romeart.py sprites <job.json> <out-dir> [--run]")
    job_p, out = argv[0], argv[1]
    job = json.load(open(job_p))
    req = _pixellab

    bodies = [{"description": job["description"], "size": 96, "view": "top-down", "item_descriptions": b} for b in job["batches"]]
    print(f"{len(bodies)} calls, {sum(len(b) for b in job['batches'])} sprites, 20-40 generations per call")
    if not _paid_gate(argv, f"{len(bodies)} x POST /create-1-direction-object", bodies,
                      cost=f"20-40 subscription generations per call, {len(bodies)} calls"):
        return
    os.makedirs(out, exist_ok=True)
    tile = 0
    for n, body in enumerate(bodies):
        json.dump(body, open(os.path.join(out, f"body{n}.json"), "w"), indent=1)
        st, resp = req("POST", "/create-1-direction-object", body)
        print(f"call {n}: HTTP {st} {str(resp)[:160]}")
        if st not in (200, 202):
            sys.exit("post failed")
        oid = resp["object_id"]
        meta = None
        for _ in range(120):
            time.sleep(10)
            st, meta = req("GET", f"/objects/{oid}")
            status = meta.get("status") if isinstance(meta, dict) else None
            if status in ("completed", "review", "failed"):
                break
        json.dump(meta, open(os.path.join(out, f"meta{n}.json"), "w"), indent=1)
        if not isinstance(meta, dict) or meta.get("status") == "failed":
            sys.exit("generation failed")
        urls = meta.get("frame_urls") or [u for k, u in sorted((meta.get("storage_urls") or {}).items())]
        for u in urls:
            im = Image.open(io.BytesIO(_download(u))).convert("RGBA")
            im.save(os.path.join(out, f"tile_{tile:02d}.png")); print(f"  tile_{tile:02d}.png {im.size}"); tile += 1
    st, bal = req("GET", "/balance"); print("balance:", str(bal)[:200])


# ==========================================================================
# pltileset.py -- one PixelLab create-tileset call
# ==========================================================================

def _pltileset(argv):
    """Run one PixelLab create-tileset call and save its 16 tiles.

    python3 tools/romeart.py pltileset <out-dir> <request.json> [--run]   (PAID)

The request file is the JSON body (no images); keys starting with "_" are
our own notes and are not sent. Token at ~/.config/pixellab/token.
Writes submit.json, result.json, tile_NN.png, tiles_meta.json, sheet.png and
a 7x6 mock (map_mock_1x.png / _3x.png) laid out by corner pattern, and prints
the terrain ids (the lower id is what later sets chain to) and seam figures.
"""
    import base64, time
    from PIL import ImageDraw, ImageChops, ImageStat

    if len(argv) < 3:
        sys.exit("usage: romeart.py pltileset <out-dir> <request.json> [--run]")
    out, reqp = argv[1], argv[2]
    body = json.load(open(reqp))
    # A key starting with "_" is our own record, not part of the request -- the
    # same convention rdgen uses. The API rejects an unknown field outright (422),
    # so a job file's _note would make it unrunnable if it were posted.
    body = {k: v for k, v in body.items() if not k.startswith("_")}
    if not _paid_gate(argv, "POST /create-tileset", body):
        return
    os.makedirs(out, exist_ok=True)
    json.dump(body, open(os.path.join(out, "request.json"), "w"), indent=1)
    code, resp = _pixellab("POST", "/create-tileset", body)
    print("HTTP", code, json.dumps(resp)[:300])
    json.dump(resp, open(os.path.join(out, "submit.json"), "w"), indent=1)
    tid = resp.get("tileset_id") if isinstance(resp, dict) else None
    if not tid:
        sys.exit("no tileset id")
    s = None
    for i in range(90):
        time.sleep(10)
        st, s = _pixellab("GET", f"/tilesets/{tid}", timeout=60)
        if st == 200 and isinstance(s, dict) and s.get("tileset"):
            print(f"done at {(i + 1) * 10}s"); break
    json.dump(s, open(os.path.join(out, "result.json"), "w"), indent=1)
    ts = s["tileset"]
    print("terrain ids", s.get("metadata", {}).get("terrain_ids"))
    imgs = []
    for i, t in enumerate(ts["tiles"]):
        data = base64.b64decode(t["image"]["base64"].split(",")[-1])
        p = os.path.join(out, f"tile_{i:02d}.png"); open(p, "wb").write(data)
        imgs.append((i, t, Image.open(p).convert("RGBA")))
    json.dump([{k: v for k, v in t.items() if k != "image"} for _, t, _ in imgs], open(os.path.join(out, "tiles_meta.json"), "w"), indent=1)
    T = imgs[0][2].width; S = 96 // T


    def key(c):
        return tuple({"upper": "u", "lower": "l"}.get(c[k], "t") for k in ("NW", "NE", "SW", "SE"))


    by = {}
    for _, t, im in imgs:
        by.setdefault(key(t["corners"]), im)
    grid = [["l"] * 8 for _ in range(7)]
    for y in range(1, 4):
        for x in range(1, 5):
            grid[y][x] = "u"
    grid[4][2] = "u"; grid[4][3] = "u"
    mock = Image.new("RGB", (7 * T, 6 * T))
    for cy in range(6):
        for cx in range(7):
            k = (grid[cy][cx], grid[cy][cx + 1], grid[cy + 1][cx], grid[cy + 1][cx + 1])
            if k in by:
                mock.paste(by[k].convert("RGB"), (cx * T, cy * T))
    mock.save(os.path.join(out, "map_mock_1x.png"))
    mock.resize((mock.width * S, mock.height * S), Image.NEAREST).save(os.path.join(out, "map_mock_3x.png"))


    def diff(a, b):
        return round(sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3, 1)


    tiles = {"".join(k): im.convert("RGB") for k, im in by.items()}
    rows = [diff(a.crop((T - 1, 0, T, T)), b.crop((0, 0, 1, T))) for ka, a in tiles.items() for kb, b in tiles.items() if ka[1] == kb[0] and ka[3] == kb[2]]
    print("horizontal seam mean", round(sum(rows) / len(rows), 1), "max", max(rows))
    if "uuuu" in tiles:
        f = tiles["uuuu"]; print("upper self-seam v/h", diff(f.crop((0, 0, T, 1)), f.crop((0, T - 1, T, T))), diff(f.crop((0, 0, 1, T)), f.crop((T - 1, 0, T, T))))
    cols = 4; rws = (len(imgs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (T * S + 8) + 8, rws * (T * S + 24) + 8), (40, 40, 40)); d = ImageDraw.Draw(sheet)
    for n, (i, t, im) in enumerate(imgs):
        x = 8 + (n % cols) * (T * S + 8); y = 8 + (n // cols) * (T * S + 24)
        big = im.resize((T * S, T * S), Image.NEAREST); sheet.paste(big, (x, y + 16), big)
        c = t["corners"]; d.text((x, y), f"{i} {c['NW'][0]}{c['NE'][0]}{c['SW'][0]}{c['SE'][0]}", fill=(230, 230, 230))
    sheet.save(os.path.join(out, "sheet.png"))


# ==========================================================================
# pltilespro.py -- one PixelLab Tiles Pro call
# ==========================================================================

def _pltilespro(argv):
    """One PixelLab Tiles Pro call (connectable terrain tileset) from a JSON body.

    python3 tools/romeart.py pltilespro <body.json> <out-dir> [--run]   (PAID)

Posts the body to /create-tiles-pro, polls /tiles-pro/{id}, downloads every
tile as tile_<n>.png, writes meta.json (tile_rules, usage) and sheet.png.
"""
    import io, time

    if len(argv) < 3:
        sys.exit("usage: romeart.py pltilespro <body.json> <out-dir> [--run]")
    req = _pixellab
    body = json.load(open(argv[1]))
    # "_"-prefixed keys are our own record, not part of the request (rdgen's
    # convention). The API rejects unknown fields with a 422.
    body = {k: v for k, v in body.items() if not k.startswith("_")}
    out = argv[2]
    if not _paid_gate(argv, "POST /create-tiles-pro", body):
        return
    os.makedirs(out, exist_ok=True)
    st, resp = req("POST", "/create-tiles-pro", body)
    print("post", st, json.dumps(resp)[:300])
    if st != 202:
        sys.exit(1)
    tid = resp.get("tile_id") or resp.get("id") or resp.get("job_id")
    if not tid:
        print(resp); sys.exit(1)
    while True:
        time.sleep(10)
        st, res = req("GET", f"/tiles-pro/{tid}")
        print("poll", st, str(res)[:120])
        if st == 200 and isinstance(res, dict) and res.get("storage_urls"):
            break
        if st not in (200, 423, 202):
            sys.exit(1)
    urls = res["storage_urls"]
    tiles = {}
    for name, url in urls.items():
        im = Image.open(io.BytesIO(_download(url))).convert("RGBA")
        im.save(os.path.join(out, name + ".png")); tiles[name] = im
    json.dump({"tile_id": tid, "usage": res.get("usage"), "kind": res.get("kind"),
               "tile_rules": res.get("tile_rules"), "body": body},
              open(os.path.join(out, "meta.json"), "w"), indent=1)
    names = sorted(tiles, key=lambda n: int(n.split("_")[-1]))
    w, h = tiles[names[0]].size
    cols = 4
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * w, rows * h))
    for i, n in enumerate(names):
        sheet.paste(tiles[n], ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(out, "sheet.png"))
    print(len(names), "tiles", w, "x", h, "usage", res.get("usage"), "in", out)


# ==========================================================================
# rdgen.py -- the Retro Diffusion driver
# ==========================================================================

def _rdgen(argv):
    """Retro Diffusion art pipeline driver.

Drives one asset from a JSON job spec through generate -> transform -> QA,
keeping every request and response on disk so a result is auditable rather
than remembered. Written for ~116 assets, so the whole run is described by
the job file and nothing lives in shell history.

    python3 tools/romeart.py rdgen cost      art/jobs/hastati.json
    python3 tools/romeart.py rdgen run       art/jobs/hastati.json [--run]   (PAID)
    python3 tools/romeart.py rdgen reprocess art/jobs/hastati.json
    python3 tools/romeart.py rdgen balance

`run` quotes the cost and stops there; only `run --run` submits and is charged.

Two rules this enforces, both learned the hard way:

  * Every paid call goes out with async=true and its task id is written to
    disk BEFORE any polling starts. A synchronous call once timed out after
    the charge landed, and the result was unrecoverable because the id was
    never captured.
  * The token is read from a file, never from the environment and never from
    a command line, so it stays out of process listings and shell history.
    This project does not use environment variables anywhere.

Outputs land under build/art/<id>/ (build/ is gitignored). Approved finals
are copied into the pack by hand, deliberately -- nothing here writes to
assets/.
"""
    import argparse
    import base64
    import io
    import json
    import os
    import shutil
    import sys
    import time
    import urllib.error
    import urllib.request

    API = "https://api.retrodiffusion.ai/v1"
    DEFAULT_TOKEN_FILE = os.path.expanduser("~/.config/retrodiffusion/token")
    OUT_ROOT = "build/art"

    try:
        from PIL import Image, ImageSequence
    except ImportError:
        sys.exit("rdgen: needs Pillow (pip install pillow)")


    # ---- plumbing --------------------------------------------------------------

    def read_token(path):
        if not os.path.exists(path):
            sys.exit(f"rdgen: no token at {path}\n"
                     f"  write your rdpk- key there, chmod 600.")
        tok = open(path).read().strip()
        if not tok.startswith("rdpk-"):
            sys.exit(f"rdgen: {path} does not look like an rdpk- key")
        return tok


    def api(token, method, path, payload=None, timeout=60):
        url = f"{API}{path}"
        data = json.dumps(payload).encode() if payload is not None else None
        req = urllib.request.Request(url, data=data, method=method)
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
            except Exception:
                return {"detail": {"code": str(e.code), "message": body[:400]}}


    def save_json(path, obj):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(obj, f, indent=1)


    def new_run_dir(job_id):
        """A fresh numbered directory per run. Never reuse, never overwrite.

    Generated art is paid for and is not reproducible -- the same prompt and
    seed gave materially different results across runs. An earlier version of
    this wrote every run to build/art/<id>/ and a re-run destroyed the
    previous raw. Recovering it was only possible because the API retains
    outputs for 24 hours and the request id had been saved.
    """
        base = os.path.join(OUT_ROOT, job_id)
        os.makedirs(base, exist_ok=True)
        n = 1
        while os.path.exists(os.path.join(base, f"run{n:02d}")):
            n += 1
        d = os.path.join(base, f"run{n:02d}")
        os.makedirs(d)
        return d


    def latest_run_dir(job_id):
        base = os.path.join(OUT_ROOT, job_id)
        runs = sorted(g for g in os.listdir(base)) if os.path.isdir(base) else []
        runs = [r for r in runs if r.startswith("run")]
        return os.path.join(base, runs[-1]) if runs else None


    # ---- job spec --------------------------------------------------------------

    # Every key a job may carry. Keys beginning with "_" are notes for the reader
    # and are never sent. Anything else is refused rather than ignored: a job once
    # carried return_non_bg_removed, which this file did not forward, and the run
    # went out silently without it -- a paid call that could not answer the question
    # it was made to answer.
    JOB_KEYS = {
        # rdgen's own controls
        "id", "prompt", "style", "width", "height", "target",
        "raw_only", "figure", "headroom_rows",
        "input_image_path", "input_palette_path", "input_image_keep_alpha",
        "reference_image_paths", "pad_to", "num_images",
        # forwarded to the API by request_payload()
        "seed", "remove_bg", "tile_x", "tile_y", "frames_duration",
        "return_spritesheet", "input_palette", "strength",
        "bypass_prompt_expansion", "return_non_bg_removed",
    }


    def load_job(path):
        job = json.load(open(path))
        for k in ("id", "prompt", "style", "width", "height", "target"):
            if k not in job:
                sys.exit(f"rdgen: job {path} missing required key '{k}'")
        unknown = sorted(k for k in job if not k.startswith("_") and k not in JOB_KEYS)
        if unknown:
            sys.exit(f"rdgen: job {path} has unknown key(s): {', '.join(unknown)}\n"
                     f"       rdgen would ignore them and submit a paid request that\n"
                     f"       is not the one described by the job. Add each to\n"
                     f"       JOB_KEYS (and to request_payload if the API takes it),\n"
                     f"       or prefix it with '_' if it is only a note.")
        job["_path"] = path
        return job


    def request_payload(job, *, check_cost=False):
        p = {
            "prompt": job["prompt"],
            "prompt_style": job["style"],
            "width": job["width"],
            "height": job["height"],
            "num_images": job.get("num_images", 1),
        }
        for k in ("seed", "remove_bg", "tile_x", "tile_y", "frames_duration",
                  "return_spritesheet", "input_palette", "strength",
                  "bypass_prompt_expansion", "return_non_bg_removed"):
            if k in job:
                p[k] = job[k]
        # input_palette: DO NOT USE to "anchor" or "match" a colour. It HARD
        # CONSTRAINS the entire output to the supplied palette, and it collapses
        # structure along with it. Measured twice, a day apart, and forgotten in
        # between because the first finding was only ever said out loud:
        #   2026-08-15  on a figure: "forces flat limited colour but collapses the
        #               face and structure"
        #   2026-08-17  on two object tiles: passing grass.png (5 colours, all
        #               green) returned 3-colour all-green images with the ring and
        #               chest rendered in shades of grass. $0.076 wasted.
        # To match a colour, name it in the prompt instead.
        #
        # img2img. The API wants RGB with no alpha, so a transparent source is
        # flattened onto white first. Structure -- pose, proportion, framing,
        # outline weight -- carries over from the input far more reliably than it
        # can be described in words, which is the whole reason for using it.
        if job.get("input_palette_path"):
            pal = Image.open(job["input_palette_path"]).convert("RGB")
            pb = io.BytesIO(); pal.save(pb, format="PNG")
            p["input_palette"] = base64.b64encode(pb.getvalue()).decode()
        # input_image_keep_alpha: send the PNG exactly as it is, alpha intact.
        # The API reference says input_image must be RGB without transparency, but
        # the animation docs say "a transparent start frame yields a transparent
        # GIF" -- both cannot be true. Flattening onto white would guarantee opaque
        # frames, and restoring alpha afterwards is post-processing, which is
        # banned. A check_cost with an RGBA payload is accepted rather than
        # rejected, so this lets the claim be tested for real.
        if job.get("input_image_path"):
            src = Image.open(job["input_image_path"]).convert("RGBA")
            # pad_to: the vendor's motion-room rule -- "a sprite whose opaque
            # pixels touch the canvas edge animates badly -- pad it onto a larger
            # transparent canvas first". Every animation run before this one sent a
            # figure filling its frame, and every one of them under-moved. Nothing
            # is resampled: the still is composited into a bigger empty canvas,
            # centred horizontally and standing on the bottom edge.
            pad = job.get("pad_to")
            if pad:
                pw, ph = (pad, pad) if isinstance(pad, int) else pad
                if pw < src.width or ph < src.height:
                    sys.exit(f"rdgen: pad_to {pw}x{ph} is smaller than the source "
                             f"{src.width}x{src.height}")
                canvas = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
                canvas.alpha_composite(src, ((pw - src.width) // 2,
                                             ph - src.height))
                src = canvas
            if job.get("input_image_keep_alpha"):
                out = src
            else:
                out = Image.new("RGB", src.size, (255, 255, 255))
                out.paste(src, (0, 0), src)
            buf = io.BytesIO()
            out.save(buf, format="PNG")
            p["input_image"] = base64.b64encode(buf.getvalue()).decode()
        # reference_images: RD Pro and the prompt-driven rd_animation__* styles
        # accept up to 9. Unlike input_image these are NOT redrawn -- they steer
        # style and content, which is how one character stays the same character
        # across separate generations.
        if job.get("reference_image_paths"):
            refs = []
            for rp in job["reference_image_paths"]:
                src = Image.open(rp).convert("RGBA")
                buf = io.BytesIO()
                src.save(buf, format="PNG")
                refs.append(base64.b64encode(buf.getvalue()).decode())
            p["reference_images"] = refs
        if check_cost:
            p["check_cost"] = True
        else:
            p["async"] = True
        return p


    # ---- transforms ------------------------------------------------------------

    def content_bbox(im, alpha_floor=12):
        """Tight bbox of non-transparent pixels; None when the image is empty."""
        im = im.convert("RGBA")
        px = im.load()
        w, h = im.size
        pts = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] > alpha_floor]
        if not pts:
            return None
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        return min(xs), min(ys), max(xs) + 1, max(ys) + 1


    def fill_to_target(im, tw, th, headroom=0):
        """Crop to content, then scale so the figure fills the full target height.

    The spec's fill rule: feet on the bottom row, head within a pixel of the
    top. Generations come back with margin, and pasting them unscaled is why
    earlier sprites read as small and floaty against the reference art.
    """
        bb = content_bbox(im)
        if not bb:
            return im.resize((tw, th), Image.NEAREST)
        fig = im.convert("RGBA").crop(bb)
        fw, fh = fig.size
        # Reserve `headroom` rows at the top. The reference leaves row 0 clear and
        # seats the figure on the bottom row; asking the model for that never
        # worked, so it is imposed here instead.
        avail = th - headroom
        scale = avail / fh
        nw = max(1, min(tw, round(fw * scale)))
        fig = fig.resize((nw, avail), Image.LANCZOS)
        out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
        out.paste(fig, ((tw - nw) // 2, headroom))
        return out


    def k_centroid(token, im, tw, th, workdir):
        """Area-weighted downscale via the free RD edit tool, with a local fallback.

    A naive nearest resample of a 2x source turns to mush at 48x34; this is
    what keeps it legible. Free, so it never needs a cost check.
    """
        tmp = os.path.join(workdir, "_kc_in.png")
        im.save(tmp)
        payload = {"input_image": base64.b64encode(open(tmp, "rb").read()).decode(),
                   "width": tw, "height": th}
        r = api(token, "POST", "/edit/tools/k_centroid_downscale", payload, timeout=90)
        save_json(os.path.join(workdir, "kcentroid_response.json"),
                  {k: v for k, v in r.items() if k != "base64_images"})
        imgs = r.get("base64_images") or []
        if not imgs:
            print(f"  ! k_centroid failed ({str(r.get('detail'))[:80]}); "
                  f"falling back to local LANCZOS")
            return im.resize((tw, th), Image.LANCZOS)
        out = os.path.join(workdir, "_kc_out.png")
        open(out, "wb").write(base64.b64decode(imgs[0]))
        return Image.open(out).convert("RGBA")


    def threshold_alpha(im, cut=128):
        """Binary alpha. Soft edges halo against unknown terrain at integer scale."""
        im = im.convert("RGBA")
        px = im.load()
        w, h = im.size
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                px[x, y] = (r, g, b, 255 if a >= cut else 0)
        return im


    # ---- QA --------------------------------------------------------------------

    def qa(im, job):
        """Measure the spec's done-criteria. Returns (rows, ok)."""
        tw, th = job["target"]
        im = im.convert("RGBA")
        px = im.load()
        w, h = im.size
        opaque = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] == 255]
        partial = sum(1 for y in range(h) for x in range(w) if 0 < px[x, y][3] < 255)
        colours = {px[x, y][:3] for (x, y) in opaque}
        xs = [p[0] for p in opaque] or [0]
        ys = [p[1] for p in opaque] or [0]
        fw = max(xs) - min(xs) + 1
        fh = max(ys) - min(ys) + 1
        figure = job.get("figure", True)

        rows = [
            ("dimensions", f"{w}x{h}", (w, h) == (tw, th)),
            ("partial-alpha px", partial, partial == 0),
            ("unique colours", len(colours), 12 <= len(colours) <= 40),
        ]
        if figure:
            top, bot = min(ys), max(ys)
            # Headroom, not maximum fill. Requiring fh == th rewards a figure whose
            # crest runs into the top edge and gets clipped -- which is exactly what
            # the first Hastati did, and this check passed it. The reference art
            # leaves row 0 empty and occupies rows 1..33.
            rows += [
                ("top row clear", f"starts row {top}", top >= 1),
                ("fills height", f"{fh}/{th} rows", fh >= th - 3),
                ("silhouette width", f"{fw}/{tw} cols", fw >= 30),
                ("feet on bottom row", bot == h - 1, bot == h - 1),
            ]
        return rows, all(ok for _, _, ok in rows)


    def contact_sheet(final, job, workdir, pack="assets/glory-of-rome"):
        """The check that cannot be automated: the sprite over real terrain.

    A metric once scored a floating object as a seamless tile because its
    edges were uniform. Numbers get fooled; this gets looked at.
    """
        grounds = ["grass", "forest", "desert"]
        tiles = []
        for g in grounds:
            p = os.path.join(pack, "art/tiles", f"{g}.png")
            if os.path.exists(p):
                tiles.append((g, Image.open(p).convert("RGBA")))
        if not tiles:
            return None
        tw, th = job["target"]
        zooms = (1, 3, 8)
        pad = 10
        W = sum(tw * z + pad for z in zooms) * len(tiles) + pad
        H = th * max(zooms) + 30
        sheet = Image.new("RGBA", (W, H), (24, 24, 28, 255))
        x = pad
        for name, ground in tiles:
            for z in zooms:
                cell = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
                cell.paste(ground.resize((tw, th), Image.NEAREST), (0, 0))
                cell.alpha_composite(final)
                sheet.paste(cell.resize((tw * z, th * z), Image.NEAREST), (x, 20))
                x += tw * z + pad
        out = os.path.join(workdir, "contact_sheet.png")
        sheet.save(out)
        return out


    def transform(token, raw, job, work):
        """No-op when the job is raw_only: the deliverable is the image exactly as
    Retro Diffusion returned it. Anything done here is post-processing."""
        if job.get("raw_only"):
            p = os.path.join(work, "01_raw.png")
            print("  raw_only: delivering the generated image untouched")
            return p

        """Raw -> final, resampling only when the source is genuinely larger.

    A raw already at target size is passed through untouched apart from the
    alpha threshold. Resampling it anyway -- which this did at first, by
    always scaling to 2x target and downscaling back -- turned a clean 31
    colour sprite into 779 colours of mush. Measured, not theorised.
    """
        tw, th = job["target"]
        rw, rh = raw.size
        stage = raw
        if (rw, rh) != (tw, th):
            if job.get("figure", True):
                stage = fill_to_target(stage, min(rw, tw * 2), min(rh, th * 2),
                                       headroom=job.get("headroom_rows", 0))
                stage.save(os.path.join(work, "02_filled.png"))
            stage = k_centroid(token, stage, tw, th, work)
            stage.save(os.path.join(work, "03_downscaled.png"))
        else:
            print("  raw already at target size; no resample")
        final = threshold_alpha(stage)
        p = os.path.join(work, "04_final.png")
        final.save(p)
        return p


    def cmd_reprocess(args):
        """Redo transforms + QA from the saved raw. No generation, no charge."""
        tok = read_token(args.token_file)
        job = load_job(args.job)
        work = latest_run_dir(job["id"])
        if not work:
            sys.exit(f"rdgen: no runs for {job['id']}")
        raw_p = os.path.join(work, "01_raw.png")
        if not os.path.exists(raw_p):
            sys.exit(f"rdgen: no saved raw at {raw_p}; run it first")
        raw = Image.open(raw_p).convert("RGBA")
        print(f"reprocessing {job['id']} from raw {raw.size} (no charge)")
        frames = write_frames(raw_p, job, work)
        if frames:
            widths, growth = motion_report(frames)
            print(f"frames {len(frames)}  ->  {work}/frame_00..{len(frames)-1:02d}.png")
            print(f"  silhouette widths {widths}")
            print(f"  [{'PASS' if growth >= MOTION_GATE_PCT else 'FAIL'}] motion "
                  f"{growth}% growth over frame 0 (gate {MOTION_GATE_PCT}%)")

        final_p = transform(tok, raw, job, work)
        final = Image.open(final_p).convert("RGBA")
        rows, ok = qa(final, job)
        print(f"\nQA  {job['id']}")
        for name, val, good in rows:
            print(f"  [{'PASS' if good else 'FAIL'}] {name:<20} {val}")
        sheet = contact_sheet(final, job, work)
        if sheet:
            print(f"\ncontact sheet: {sheet}")
        print(f"final: {final_p}")
        print(f"\nverdict: {'PASS' if ok else 'NEEDS WORK'} "
              f"(metrics only -- look at the contact sheet before accepting)")


    # ---- commands --------------------------------------------------------------

    def cmd_balance(args):
        tok = read_token(args.token_file)
        r = api(tok, "GET", "/inferences/credits")
        print(json.dumps(r))


    def cmd_cost(args):
        tok = read_token(args.token_file)
        job = load_job(args.job)
        r = api(tok, "POST", "/inferences", request_payload(job, check_cost=True))
        print(f"{job['id']}: ${r.get('balance_cost')}  "
              f"(remaining ${r.get('remaining_balance')})")


    def cmd_run(args):
        tok = read_token(args.token_file)
        job = load_job(args.job)
        payload = request_payload(job)
        cost = api(tok, "POST", "/inferences", request_payload(job, check_cost=True))
        print(f"cost ${cost.get('balance_cost')}  balance ${cost.get('remaining_balance')}")
        # Dry unless --run (#143). The run dir is made only for a real
        # submission: an empty one would become reprocess's "latest run".
        if not args.run:
            print("dry run; nothing submitted (add --run to submit and be charged)")
            return

        work = new_run_dir(job["id"])
        shutil.copy(job["_path"], os.path.join(work, "job.json"))
        print(f"run dir {work}")
        save_json(os.path.join(work, "request.json"),
                  {k: v for k, v in payload.items() if k != "input_image"})

        sub = api(tok, "POST", "/inferences", payload)
        # Written before ANY polling: a lost task id means a paid result that
        # cannot be retrieved.
        save_json(os.path.join(work, "task.json"), sub)
        task = sub.get("task_id")
        if not task:
            sys.exit(f"rdgen: submit failed: {json.dumps(sub)[:300]}")
        print(f"task {task} (saved to {work}/task.json)")

        result = None
        for i in range(120):
            time.sleep(4)
            s = api(tok, "GET", f"/inferences/tasks/{task}")
            st = s.get("status")
            if st in ("succeeded", "failed"):
                save_json(os.path.join(work, "poll.json"),
                          {k: v for k, v in s.items() if k != "result"})
                result = s
                print(f"[{(i+1)*4}s] {st}")
                break
        if not result or result.get("status") != "succeeded":
            sys.exit(f"rdgen: job did not succeed: {json.dumps(result)[:300]}")

        res = result.get("result") or {}
        imgs = res.get("base64_images") or []
        if not imgs:
            sys.exit(f"rdgen: no image returned: {json.dumps(res)[:300]}")
        print(f"charged ${res.get('balance_cost')}  request {res.get('request_id')}")

        # Write EVERY image the response carries, not just the first. Options like
        # return_non_bg_removed and return_pre_palette return a second paid image in
        # the same list, and keeping only imgs[0] silently destroys it.
        raw_p = os.path.join(work, "01_raw.png")
        open(raw_p, "wb").write(base64.b64decode(imgs[0]))
        for i, extra in enumerate(imgs[1:], start=2):
            extra_p = os.path.join(work, f"01_raw_{i}.png")
            open(extra_p, "wb").write(base64.b64decode(extra))
            print(f"also returned: {extra_p}")
        raw = Image.open(raw_p).convert("RGBA")
        print(f"raw {raw.size}  ({len(imgs)} image(s) returned)")

        frames = write_frames(raw_p, job, work)
        if frames:
            widths, growth = motion_report(frames)
            print(f"frames {len(frames)}  ->  {work}/frame_00..{len(frames)-1:02d}.png")
            print(f"  silhouette widths {widths}")
            print(f"  [{'PASS' if growth >= MOTION_GATE_PCT else 'FAIL'}] motion "
                  f"{growth}% growth over frame 0 (gate {MOTION_GATE_PCT}%)")
        final_p = transform(tok, raw, job, work)
        final = Image.open(final_p).convert("RGBA")
        rows, ok = qa(final, job)
        print(f"\nQA  {job['id']}")
        for name, val, good in rows:
            print(f"  [{'PASS' if good else 'FAIL'}] {name:<20} {val}")
        sheet = contact_sheet(final, job, work)
        if sheet:
            print(f"\ncontact sheet: {sheet}")
        print(f"final: {final_p}")
        print(f"\nverdict: {'PASS' if ok else 'NEEDS WORK'} "
              f"(metrics only -- look at the contact sheet before accepting)")


    # ---- animation frames ------------------------------------------------------

    def write_frames(raw_p, job, work):
        """Split an animation result into individual frames.

    The grid is DERIVED from what came back, never assumed. A 4-frame sheet is
    2x2 and a 6-frame sheet is 3x2; this was hardcoded to 2x2 by hand outside
    this file, which sliced every 6-frame run through the middle of each frame.
    Two good animations were discarded and the pipeline was locked at four
    frames on the strength of that mistake.

    A GIF response is authoritative about its own frame count, so nothing is
    cut at all in that case.
    """
        im = Image.open(raw_p)
        if getattr(im, "n_frames", 1) > 1:
            frames = [f.convert("RGBA") for f in ImageSequence.Iterator(im)]
        else:
            w, h = job["width"], job["height"]
            sw, sh = im.size
            if sw % w or sh % h:
                sys.exit(f"rdgen: sheet {sw}x{sh} is not a whole number of {w}x{h} "
                         f"cells -- refusing to cut it into sliced frames")
            cols, rows = sw // w, sh // h
            if cols * rows < 2:
                return []
            im = im.convert("RGBA")
            frames = [im.crop((c * w, r * h, c * w + w, r * h + h))
                      for r in range(rows) for c in range(cols)]
        out = []
        for i, f in enumerate(frames):
            fp = os.path.join(work, f"frame_{i:02d}.png")
            f.save(fp)
            out.append(fp)
        return out


    # The line between an animation that reads as an attack and one that reads as
    # walking, measured across all 25 runs generated before this gate existed: real
    # attacks grew the silhouette by 28-34% over the first frame, everything that
    # looked like a walk grew it by under 7%.
    MOTION_GATE_PCT = 25


    def motion_report(frame_paths):
        widths = []
        for fp in frame_paths:
            bb = Image.open(fp).convert("RGBA").getbbox()
            widths.append(bb[2] - bb[0] if bb else 0)
        base = widths[0] or 1
        return widths, round(100 * (max(widths) - base) / base)


    def main():
        ap = argparse.ArgumentParser(prog="romeart.py rdgen",
                                     description="Retro Diffusion art pipeline")
        ap.add_argument("--token-file", default=DEFAULT_TOKEN_FILE,
                        help=f"file holding the rdpk- key (default {DEFAULT_TOKEN_FILE})")
        sub = ap.add_subparsers(dest="cmd", required=True)
        b = sub.add_parser("balance"); b.set_defaults(fn=cmd_balance)
        c = sub.add_parser("cost");    c.add_argument("job"); c.set_defaults(fn=cmd_cost)
        r = sub.add_parser("run");     r.add_argument("job")
        r.add_argument("--run", action="store_true",
                       help="submit the request and be charged (PAID); without "
                            "it, run only quotes the cost")
        r.set_defaults(fn=cmd_run)
        rp = sub.add_parser("reprocess"); rp.add_argument("job")
        rp.set_defaults(fn=cmd_reprocess)
        args = ap.parse_args(argv[1:])
        args.fn(args)


    main()



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


def map_lint(pack, zid, update_allow=False, update_reach=False, show_all=False):
    g = load_game(pack)
    z = zone_of(g, zid)
    maps = os.path.join(ROOT, "art", "maps")
    dat = os.path.join(pack, z["map"])
    allow_p, reach_p = (os.path.join(maps, f"{zid}_{k}.json") for k in ("lint_allow", "reach"))
    int_p = os.path.join(maps, f"{zid}_lint_intended.json")
    intended = load_json(int_p) if os.path.exists(int_p) else {}
    found = lint_findings(read_rows(os.path.join(maps, f"{zid}.txt")), intended)
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


def cmd_map(argv):
    ap = argparse.ArgumentParser(prog="romeart.py map", description="The zone maps (see the Maps section).")
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
    p = sub.add_parser("zones", help="each zone's id, map file and size, one per line")
    p.add_argument("pack")
    p = sub.add_parser("place", help="scatter chests and armies inside region boxes")
    p.add_argument("pack"); p.add_argument("zone"); p.add_argument("map"); p.add_argument("regions")
    p.add_argument("--add", action="store_true", help="keep what stands; place only what is lacking")
    p.add_argument("--write", action="store_true", help="write them into game.json")
    a = ap.parse_args(argv)
    if a.cmd == "zones":
        for z in load_game(a.pack)["zones"]:
            print(z["id"], z["map"], f'{z["width"]}x{z["height"]}')
    elif a.cmd == "build":
        map_build(a.pack, a.zone, a.src, a.out, a.strict)
    elif a.cmd == "check":
        map_check(a.pack, a.zone, a.map)
    elif a.cmd == "lint":
        sys.exit(map_lint(a.pack, a.zone, a.update_allow, a.update_reach, a.all))
    elif a.cmd == "sanity":
        sys.exit(map_sanity(a.pack, a.map, a.size, a.zone))
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



COMMANDS = {
    "map": cmd_map,
    "zone": cmd_zone, "install": cmd_install, "sheet": cmd_sheet,
    "icon": cmd_icon,
    "sprites": cmd_sprites,   # paid (network)
    "slots": cmd_slots,
    "rebank": cmd_rebank,
    "fieldgrade": cmd_fieldgrade,
    "bridge": cmd_bridge,
    "prompts": lambda a: _artprompts(["romeart"] + a),
    "grass": lambda a: _grassvar(["romeart"] + a),
    "stitch": lambda a: _stitch96(["romeart"] + a),
    "edges": lambda a: _tileedges(["romeart"] + a),
    "fills": lambda a: _fills(["romeart"] + a),
    "aprons": lambda a: _aprons(["romeart"] + a),
    "details": lambda a: _details(["romeart"] + a),
    "edgevars": lambda a: _edgevars(["romeart"] + a),
    "interiors": lambda a: _interiors(["romeart"] + a),
    "lattice": lambda a: _forestlattice(["romeart"] + a),
    "seamcheck": lambda a: _seamcheck(["romeart"] + a),
    "compose": lambda a: _treetile(["romeart"] + a),
    "sweep": lambda a: _roadtile(["romeart"] + a),
    "mouth": lambda a: _rivermouth(["romeart"] + a),
    "tile2x2": lambda a: _tile2x2(["romeart"] + a),
    "mirror": lambda a: _mirrorhalf(["romeart"] + a),
    "crop": lambda a: _cropcentre(["romeart"] + a),
    "siegeslice": lambda a: _siegeslice(["romeart"] + a),
    "siegewalls": lambda a: _siegewalls(["romeart"] + a),
    "fieldcalm": lambda a: _fieldcalm(["romeart"] + a),
    "splashlogo": lambda a: _splashlogo(["romeart"] + a),
    "splashtitle": lambda a: _splashtitle(["romeart"] + a),
    "classpicker": lambda a: _classpicker(["romeart"] + a),
    "loopreview": lambda a: _loopreview(["romeart"] + a),
    "introtheme": lambda a: _introtheme(["romeart"] + a),
    # paid (network): see the last section
    "rdgen": lambda a: _rdgen(["romeart"] + a),
    "pltileset": lambda a: _pltileset(["romeart"] + a),
    "pltilespro": lambda a: _pltilespro(["romeart"] + a),
}


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd = sys.argv[1]
    if cmd not in COMMANDS:
        sys.exit(f"romeart: no command '{cmd}' (try --help)")
    COMMANDS[cmd](sys.argv[2:])
    return 0


if __name__ == "__main__":
    sys.exit(main())
