#!/usr/bin/env python3
"""Grass variants: the pack grass with a patch of a second grass inside.

    python3 tools/grassvar.py <set-dir> <out-dir> [--count N] [--seed S] [--set DIR ...]

The set is a PixelLab 16 px tileset whose lower terrain is the pack grass
and whose upper is a detail (weeds, pebbles, flowers, dry grass). More
sets, chained to the same grass, may be given with --set; each variant
takes its patches from one or two of them at random. Each variant is a 96 px
tile built like a terrain tile (tools/stitch96.py): a 7x7 vertex grid, the
set's corner tile per 2x2. The border vertices are always the lower grass,
so every variant's edges are the plain grass and any two variants, or a
variant and the plain tile, join without a seam. The patch is a random
blob grown from a seed vertex inside the tile, a different shape per
variant, and one variant may carry two small patches.

Writes grass_01.png .. grass_NN.png and sheet.png (a field mixing them).
"""
import json, os, random, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
count = int(sys.argv[sys.argv.index("--count") + 1]) if "--count" in sys.argv else 3
seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 1
extra = [sys.argv[i + 1] for i, a in enumerate(sys.argv) if a == "--set"]
# --decor DIR --decor-ids 0,1,2 [--decor-n 2] [--patch-rate 0.5]: small transparent
# objects (32 px) laid fully inside each variant, never crossing a tile line,
# on top of the grass; a variant carries a patch only at --patch-rate.
decor_dir = sys.argv[sys.argv.index("--decor") + 1] if "--decor" in sys.argv else None
decor_ids = [int(x) for x in sys.argv[sys.argv.index("--decor-ids") + 1].split(",")] if "--decor-ids" in sys.argv else []
decor_n = int(sys.argv[sys.argv.index("--decor-n") + 1]) if "--decor-n" in sys.argv else 2
decor_rate = float(sys.argv[sys.argv.index("--decor-rate") + 1]) if "--decor-rate" in sys.argv else 1.0   # share of variants that get objects
patch_rate = float(sys.argv[sys.argv.index("--patch-rate") + 1]) if "--patch-rate" in sys.argv else 1.0
# --mottle N: N small one- or two-vertex patches of the first set's upper
# terrain scattered through EVERY tile, base included, so the ground reads as
# a soft mottle of two close tones instead of one flat colour
mottle = int(sys.argv[sys.argv.index("--mottle") + 1]) if "--mottle" in sys.argv else 0
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
patch_size = int(sys.argv[sys.argv.index("--patch-size") + 1]) if "--patch-size" in sys.argv else 0   # vertices per patch, 0 = the default mix
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
