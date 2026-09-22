#!/usr/bin/env python3
"""Rome art: one tool for every compositing step the pack's art goes through.

    python3 tools/romeart.py <command> [args]

    zone <zone>            build a continent's whole tile set from its primitives
    install <zone>         copy a built set into the pack and list it in game.json
    sheet <zone>           a review page of every tile in a set, at 1:1
    icon [outdir]          the launcher icon, from the title art (128/512/1024)
    prompts                rebuild docs/ROME-ART.md from art/jobs/*.json

    grass <set> <out> ...  the base grass and its variants
    stitch <set> <terrain> <out>      a PixelLab corner set into 96 px tiles
    edges [pack] [tile-set]           the terrain edges over that set's bases
    lattice <out.json> --sprites ...  a forest/mountain layout (border contract)
    seamcheck <layout.json>           check a layout against that contract
    compose <layout.json> <out>       the layout's tiles, sprites over ground
    sweep <set> <out> --sweep ...     road / river / bridge pieces
    mouth <coast> <river_ew> <grass> <sea> <out>
    tile2x2 <in> <out>                lay a 48 px tile 2x2 into 96
    mirror <in> <out> <half>          mirror half a tile over the other
    crop <in> <out> [w h]             centre-crop a still to the pack size

NOTHING HERE MAKES A PAID CALL. Generation lives in tools/rdgen.py (Retro
Diffusion) and tools/pltileset.py / pltilespro.py (PixelLab); every prompt and
setting they are given is recorded in docs/ROME-ART.md. This tool only
composites what those calls returned: it may be re-run at any time, and the
same inputs give the same tiles.

Each section below is one compositing step. Each encodes a contract set by
measurement (the lattice border rule, the road sweep's joining pattern, the
edge masks, the grass variants' shared border).
"""
import glob
import json
import math
import os
import random
import sys

from PIL import Image

PACK = "assets/glory-of-rome"


# ==========================================================================
# tile2x2.py -- lay a 48 px tile 2x2 into the 96 px pack tile
# ==========================================================================

def _tile2x2(argv):
    """Lay a 48x48 seamless tile 2x2 into the 96x96 pack tile (ART-PIPELINE, base terrain).

    python3 tools/tile2x2.py build/art/grass/run01/01_raw.png build/art/grass/run01/01_96.png
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

    python3 tools/mirrorhalf.py in.png out.png bottom|top|left|right

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

    python3 tools/cropcentre.py in.png out.png [size] [top]

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

    python3 tools/rivermouth.py <coast.png> <river_ew.png> <grass.png> <sea.png> <out.png>

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

    python3 tools/tileedges.py [pack-dir] [tile-set]

Default pack assets/glory-of-rome; with a tile set the bases are read from
and the edges written to art/tiles/<set>/.
    """


    PACK = argv[1] if len(argv) > 1 else "assets/glory-of-rome"
    SET = argv[2] if len(argv) > 2 else ""
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
# stitch96.py -- stitch a PixelLab corner tileset into 96 px tiles
# ==========================================================================

def _stitch96(argv):
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
            for (i, ins, place) in spec:
                if place == "top" and "N" in open_sides: continue
                if place == "bottom" and "S" in open_sides: continue
                l, t, r, b = bbox(i)
                y = {"top": 1 - t, "mid": 48 - (t + b) // 2, "bottom": 95 - b}[place]
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

    python3 tools/treetile.py <layout.json> <out-dir>

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

    python3 tools/seamcheck.py <layout.json>

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

    python3 tools/roadtile.py <set-dir> <out-dir> [--seed N]
    python3 tools/roadtile.py <set-dir> <out-dir> --sweep [--rim N --rim-shade F]
    python3 tools/roadtile.py <set-dir> <out-dir> --sweep --prefix river   (river_*.png)
    python3 tools/roadtile.py <set-dir> <out-dir> --sweep --fill PAVING.png --grass RIVER.png
        (a bridge deck: the band filled with a 96 px tile over another piece)

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

    python3 tools/artprompts.py [out.md]

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
                                     (["--slots", cfg["slots"]] if cfg["slots"] else []))):
        lay = os.path.join(stage, f"{terrain}.json")
        _forestlattice(["romeart", lay, "--sprites", os.path.join(prim, sprites),
                        "--name", terrain] + extra)
        d = json.load(open(lay))
        d["grass"] = os.path.join(out, "grass.png")
        json.dump(d, open(lay, "w"), indent=1)
        _treetile(["romeart", lay, os.path.join(stage, terrain)])
        print(f"  {terrain}: ", end="")
        _seamcheck(["romeart", lay])
        for f in _pack_names(terrain):
            p = os.path.join(stage, terrain, f)
            if os.path.exists(p):
                Image.open(p).save(os.path.join(out, f))

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

    # 6. The two river bridges: the pack's own paving swept across a river piece.
    for deck, over, name in (("bridge_v.png", "river_ew.png", "bridge_river_ns.png"),
                             ("bridge_h.png", "river_ns.png", "bridge_river_ew.png")):
        if not os.path.isdir(os.path.join(prim, "river")):
            break
        dst = os.path.join(stage, "bridge_" + name[14:16])
        _roadtile(["romeart", os.path.join(prim, "river"), dst, "--sweep",
                   "--fill", os.path.join(PACK, "art", "tiles", deck),
                   "--grass", os.path.join(stage, "rivers", over),
                   "--rim", "3", "--rim-shade", "0.7"])
        piece = "road_ns.png" if name.endswith("ns.png") else "road_ew.png"
        Image.open(os.path.join(dst, piece)).save(os.path.join(out, name))

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


COMMANDS = {
    "zone": cmd_zone, "install": cmd_install, "sheet": cmd_sheet,
    "icon": cmd_icon,
    "prompts": lambda a: _artprompts(["romeart"] + a),
    "grass": lambda a: _grassvar(["romeart"] + a),
    "stitch": lambda a: _stitch96(["romeart"] + a),
    "edges": lambda a: _tileedges(["romeart"] + a),
    "lattice": lambda a: _forestlattice(["romeart"] + a),
    "seamcheck": lambda a: _seamcheck(["romeart"] + a),
    "compose": lambda a: _treetile(["romeart"] + a),
    "sweep": lambda a: _roadtile(["romeart"] + a),
    "mouth": lambda a: _rivermouth(["romeart"] + a),
    "tile2x2": lambda a: _tile2x2(["romeart"] + a),
    "mirror": lambda a: _mirrorhalf(["romeart"] + a),
    "crop": lambda a: _cropcentre(["romeart"] + a),
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
