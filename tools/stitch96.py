#!/usr/bin/env python3
"""Stitch a PixelLab 16px corner tileset into the pack's 96px terrain tiles.

    python3 tools/stitch96.py <set-dir> <terrain> <out-dir> [--seed N] [--invert]
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
import glob, json, os, random, sys
from PIL import Image

STD = {1: "lllu", 2: "lull", 3: "llul", 4: "ulll", 5: "uuul", 6: "uluu",
       7: "uulu", 8: "luuu", 9: "ulul", 10: "lulu", 11: "lluu", 12: "uull",
       # spits and strips (REQ-229e): given by their OPEN SIDES, not corners
       13: "S:NS", 14: "S:EW", 15: "S:NES", 16: "S:ESW", 17: "S:SWN", 18: "S:WNE", 19: "S:NESW"}
WATER = {0: "llul", 1: "lllu", 2: "lull", 3: "ulll", 4: "uuul", 5: "uluu",
         6: "uulu", 7: "luuu", 8: "ulul", 9: "lulu", 10: "lluu", 11: "uull",
         12: "S:NS", 13: "S:EW", 14: "S:NES", 15: "S:ESW", 16: "S:SWN", 17: "S:WNE", 18: "S:NESW"}
VAL = {"l": 0, "u": 1, "t": 2}


def arg(name, default, conv=str):
    return conv(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


src, terrain, out = sys.argv[1], sys.argv[2], sys.argv[3]
seed = arg("--seed", 1, int)
rough = arg("--rough", 0.0, float)   # bays: a vertex beside a straight fringe creeps to grass at this rate
invert = "--invert" in sys.argv
pool_dir, pool_rate = arg("--pool", None), arg("--pool-rate", 0.5, float)
decor_dir, decor_rate = arg("--decor", None), arg("--decor-rate", 0.2, float)
decor_gap = arg("--decor-gap", 1, int)
decor_cover = "--decor-cover" in sys.argv   # every spot, back to front, edge to edge
sprite_only = "--sprite-only" in sys.argv   # no terrain layer: sprites on grass, seamless across cells
sprite_pitch = arg("--sprite-pitch", 24, int)
skip_clipped = "--skip-clipped" in sys.argv   # leave out sprites whose art touches their canvas edge   # sub-tiles kept clear round a sprite; -1 lets them overlap by one
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


import math

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
