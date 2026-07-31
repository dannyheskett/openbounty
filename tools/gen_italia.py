#!/usr/bin/env python3
"""Author assets/glory-of-rome/maps/italia.dat -- 40 x 64, the home zone.

Hand-tuned: every coastline row below is an explicit choice, not a formula.
The tables ARE the map. Edit a span, re-run, re-check with tools/mapcheck.py.

Shape: the Alpine arc walls the north, the Po valley sits beneath it, the
peninsula runs SE with the Apennine spine down its western side, and it forks
into the Calabrian toe and the Apulian heel. Sicilia lies off the toe; Corsica
and Sardinia stand offshore west with clear water on every side.
"""
import json
import os

# The design tables below were drawn against a 40-wide grid. SCALE stretches
# them to the real width; the shape is unchanged, the landmass is fatter.
DESIGN_W, DESIGN_H = 40, 64
W, H = 64, 128        # H needs MAP_MAX_H >= 128 (engine/include/map.h)
SCALE = W / DESIGN_W
SCALE_Y = H / DESIGN_H
FATTEN = 2            # extra tiles on each side of every land span


def expand(table):
    """Design rows -> real rows, interpolating spans between them.

    The tables are drawn at DESIGN_H; stretching vertically by repeating rows
    would give a coastline of 2-tall steps. Interpolating the span endpoints
    instead keeps the outline smooth at any height.
    """
    if not table:
        return {}
    lo, hi = min(table), max(table)
    out = {}
    for ry in range(int(round(lo * SCALE_Y)), int(round(hi * SCALE_Y)) + 1):
        dy = ry / SCALE_Y
        a, b = int(dy), min(int(dy) + 1, hi)
        while a > lo and a not in table:
            a -= 1
        while b < hi and b not in table:
            b += 1
        if a not in table or b not in table:
            continue
        f = 0.0 if a == b else (dy - a) / (b - a)
        x0 = table[a][0] + (table[b][0] - table[a][0]) * f
        x1 = table[a][1] + (table[b][1] - table[a][1]) * f
        out[ry] = (x0, x1)
    return out
SEA, GRASS, VAR, FOREST, MTN = '~', '.', ',', 'F', '^'

# --- mainland: row -> (x0, x1) inclusive -------------------------------------
# Rows 0-4 are the Alps and span the FULL WIDTH: the range is the map's
# northern boundary, not an island's coastline. Italia is closed off to the
# north by mountain rather than by sea, so the zone reads as part of a
# continent whose interior lies beyond the edge.
MAINLAND_R = {}
MAINLAND = {
    0:  (0, W - 1), 1: (0, W - 1), 2: (0, W - 1),     # Alpine wall, edge to edge
    3:  (0, W - 1), 4: (0, W - 1),
    5:  (4, 35),  6:  (4, 35),  7:  (4, 34),          # Po valley, widest
    8:  (5, 33),  9:  (6, 32), 10:  (7, 30),
    11: (8, 28), 12: (8, 27), 13: (9, 27),            # Liguria / Etruria neck
    14: (9, 26), 15: (10, 26), 16: (10, 27),
    17: (10, 24), 18: (10, 24), 19: (11, 25),         # the peninsula proper
    20: (12, 25), 21: (13, 25), 22: (13, 25),
    23: (14, 26), 24: (14, 26), 25: (15, 27),
    26: (16, 27), 27: (17, 27), 28: (17, 27),
    29: (18, 28), 30: (18, 28), 31: (19, 29),
    32: (19, 29), 33: (20, 30), 34: (20, 30),
    35: (21, 31), 36: (22, 31), 37: (23, 31),
    38: (23, 31), 39: (24, 32), 40: (24, 32),
    41: (25, 33), 42: (25, 33),
    43: (24, 34), 44: (23, 34), 45: (22, 34), 46: (21, 34),   # the south widens
}

# --- toe and heel: the peninsula forks ---------------------------------------
TOE = {47: (21, 25), 48: (20, 24), 49: (20, 23),
       50: (19, 23), 51: (19, 22), 52: (19, 22)}
# Apulia runs further SE than the toe does SW, with the Gulf of Taranto
# opening between them.
HEEL = {47: (29, 35), 48: (30, 36), 49: (31, 37),
        50: (32, 37), 51: (33, 37), 52: (34, 37)}

# --- islands: each with clear water on every side ----------------------------
SICILIA = {55: (13, 21), 56: (12, 22), 57: (12, 22), 58: (13, 21), 59: (15, 19)}
CORSICA = {19: (2, 6), 20: (2, 6), 21: (2, 6), 22: (2, 6), 23: (3, 6), 24: (3, 5)}
SARDINIA = {27: (3, 7), 28: (2, 7), 29: (2, 7), 30: (2, 7), 31: (2, 7),
            32: (2, 7), 33: (3, 7), 34: (3, 6), 35: (4, 6)}

# --- the Alps ----------------------------------------------------------------
# A solid wall across the top of the map. No passes are cut: with the map edge
# immediately north there is nothing on the far side to reach, so a pass would
# be a dead-end valley rather than a route.
ALPS_ROWS = (0, 1, 2, 3, 4)
ALPINE_PASSES = ()

# --- the Apennine spine ------------------------------------------------------
# Two tiles wide, offset west of centre, broken every few rows so the coast
# roads connect east to west. SPINE_GAPS lists the rows left open.
SPINE_ROWS = range(14, 45)
SPINE_GAPS = {24, 35, 45}   # fewer passes -> the spine gates N-S travel
SPINE_OFFSET = 3          # tiles west of the row's centre

# --- gating belts: forest/mountain walls across the whole peninsula, each with
# a single narrow pass, so Italia reads as chained sections rather than one open
# field. (design_row, terrain, pass_fraction along the land span).
CROSS_BELTS = [(16, FOREST, 0.30),   # Po valley -> the neck
               (30, MTN,    0.62),   # central Italy -> the south
               (44, FOREST, 0.35)]   # the south -> toe & heel

# --- woodland: (row, x0, x1) -------------------------------------------------
PO_FOREST = [(6, 9, 13), (7, 9, 13), (6, 20, 24), (7, 20, 24),
             (8, 14, 19), (9, 26, 31), (10, 12, 16)]
ETRURIA_FOREST = [(21, 20, 22), (22, 20, 22), (26, 22, 24), (27, 22, 24),
                  (32, 24, 26), (33, 24, 26), (38, 27, 29), (39, 27, 29)]
# more woodland down the peninsula so grass is broken up, not one field.
PENINSULA_FOREST = [(19, 12, 15), (20, 12, 14), (24, 15, 17), (25, 15, 18),
                    (28, 19, 21), (29, 19, 22), (36, 22, 25), (37, 23, 25),
                    (40, 26, 29), (41, 27, 30), (43, 25, 28),
                    (48, 21, 23), (49, 21, 24), (50, 32, 35), (51, 33, 36)]
# 2x2 blocks, not single tiles: a lone peak is a 1-tile feature with no edge
# variant. (row, x0) -- covers rows row..row+1 and columns x0..x0+1.
ISLAND_PEAKS = [(21, 4), (30, 3), (56, 17)]   # Corsica, Sardinia, Etna

# --- south gate: wall the Calabrian neck (real rows/cols) so the toe -- the
# southernmost castle -- is reachable only through ONE passage, where a static
# guardian stands. Real coordinates (the toe geometry is easier to read there).
SOUTH_GATE_ROWS = (94, 95)
SOUTH_GATE_X0, SOUTH_GATE_X1 = 26, 41
SOUTH_GATE_PASS = 32          # the one open column (guardian's tile)
SOUTH_GATE_CARVE = range(93, 101)   # force this column to grass top-to-toe


def assert_south_gate(g):
    """Wall the Calabrian neck to a single passage. Applied LAST (after despeckle
    and connect_mainland) so nothing re-closes it: mountain across the two neck
    rows except the PASS column, and a grass corridor punched down the PASS
    column from the band into the toe so the passage actually connects both."""
    for ry in SOUTH_GATE_ROWS:
        for x in range(SOUTH_GATE_X0, SOUTH_GATE_X1 + 1):
            if x != SOUTH_GATE_PASS and g[ry][x] != SEA:
                g[ry][x] = MTN
    for ry in SOUTH_GATE_CARVE:
        if g[ry][SOUTH_GATE_PASS] != SEA:
            g[ry][SOUTH_GATE_PASS] = GRASS


def sx_(x):
    return int(round(x * SCALE))


def build():
    g = [[SEA] * W for _ in range(H)]

    def span(y, x0, x1, ch=GRASS, fat=0):
        a = max(0, sx_(x0) - fat)
        b = min(W - 1, sx_(x1) + fat)
        for x in range(a, b + 1):
            g[y][x] = ch

    global MAINLAND_R
    MAINLAND_R = expand(MAINLAND)
    for table in (MAINLAND, TOE, HEEL, SICILIA, CORSICA, SARDINIA):
        for y, (x0, x1) in expand(table).items():
            span(y, x0, x1, fat=FATTEN)

    for y in range(int(round(max(ALPS_ROWS) * SCALE_Y)) + 1):
        for x in range(W):                    # Alps: the full width, exactly
            g[y][x] = MTN
    for y, x in ALPINE_PASSES:
        g[y][x] = GRASS

    # The spine holds one column pair for a run of rows, then steps east.
    # Stepping every row would leave a 1-tile nub at each step, and a 1-tile
    # feature has no edge variant (REQ-229a) -- it renders as a hard block.
    spine_rows = [int(round(y * SCALE_Y)) + k
                  for y in SPINE_ROWS for k in range(int(SCALE_Y))]
    spine_gaps = {int(round(y * SCALE_Y)) + k
                  for y in SPINE_GAPS for k in range(int(SCALE_Y))}
    seg = []
    for y in spine_rows + [None]:
        if y is not None and y not in spine_gaps and y in MAINLAND_R:
            seg.append(y)
            continue
        if seg:                                # close the segment
            my = seg[len(seg) // 2]
            x0m, x1m = MAINLAND_R[my]
            sx = sx_((x0m + x1m) / 2 - SPINE_OFFSET)
            for yy in seg:
                x0 = sx_(MAINLAND_R[yy][0]) - FATTEN
                x1 = sx_(MAINLAND_R[yy][1]) + FATTEN
                if x1 - x0 < 6:
                    continue
                sxx = min(max(sx, x0 + 2), x1 - 4)
                for dx in range(4):            # the range widens with the map
                    g[yy][sxx + dx] = MTN
            seg = []

    for y, x0, x1 in PO_FOREST + ETRURIA_FOREST + PENINSULA_FOREST:
        for dy in range(int(SCALE_Y)):
            ry = int(round(y * SCALE_Y)) + dy
            for x in range(sx_(x0), sx_(x1) + 1):
                if 0 <= x < W and 0 <= ry < H and g[ry][x] == GRASS:
                    g[ry][x] = FOREST

    # Gating belts: fill each belt row's land span with its terrain, leaving a
    # single ~3-wide pass. MAINLAND_R gives the land span per real row.
    for drow, terr_ch, passfrac in CROSS_BELTS:
        for dy in range(int(SCALE_Y)):
            ry = int(round(drow * SCALE_Y)) + dy
            if ry not in MAINLAND_R:
                continue
            x0 = sx_(MAINLAND_R[ry][0]) - FATTEN
            x1 = sx_(MAINLAND_R[ry][1]) + FATTEN
            if x1 - x0 < 6:
                continue
            passx = int(x0 + (x1 - x0) * passfrac)
            for x in range(max(0, x0), min(W, x1 + 1)):
                if abs(x - passx) <= 1:      # keep a 3-wide pass open
                    continue
                if g[ry][x] == GRASS:
                    g[ry][x] = terr_ch
    for y, x in ISLAND_PEAKS:
        for dy in range(int(SCALE_Y) + 1):
            for dx in range(3):
                ry, rx = int(round(y * SCALE_Y)) + dy, sx_(x) + dx
                if 0 <= rx < W and 0 <= ry < H:
                    g[ry][rx] = MTN

    # Cluster fill: scatter woodland/hill blocks through the open land so every
    # section has cover instead of one bare field. Blocks are >=2 tiles wide
    # (a 1-tile feature has no edge variant), placed only on all-grass footprints
    # so coasts and existing features are untouched. The coarse grid leaves grass
    # lanes between clusters, and the belt passes are never on this grid, so
    # connectivity survives. Deterministic (hash of the tile), no RNG.
    for cy in range(10, H - 6, 4):
        for cx in range(4, W - 5, 5):
            h = (cx * 131 + cy * 197) % 100
            if h >= 55:                      # ~55% of cells get a cluster
                continue
            ch = MTN if h % 5 == 0 else FOREST
            bw, bh = (3, 2) if ch == MTN else (2, 2)
            ox = (cx * 29 + cy * 7) % 3      # jitter so rows don't line up
            if all(0 <= cx + ox + dx < W and 0 <= cy + dy < H and
                   g[cy + dy][cx + ox + dx] == GRASS
                   for dx in range(bw) for dy in range(bh)):
                for dx in range(bw):
                    for dy in range(bh):
                        g[cy + dy][cx + ox + dx] = ch

    # No grass_variant tiles: the variant art is a shadow blob, unwanted on the
    # map. Grass stays plain grass everywhere.
    return g


def connect_mainland(g):
    """Guarantee the mainland is one walkable component. Gating belts and
    cluster fill can wall a land pocket off entirely; here we carve the minimum
    forest/mountain back to grass to reconnect it. Water is never carved, so the
    offshore islands (Corsica/Sardinia/Sicilia) stay islands."""
    from collections import deque
    WALK = (GRASS, VAR)
    def walk(ch): return ch in WALK
    def water(ch): return ch == SEA
    def comps():
        seen = [[False] * W for _ in range(H)]
        out = []
        for y in range(H):
            for x in range(W):
                if seen[y][x] or not walk(g[y][x]):
                    continue
                q = deque([(x, y)]); cell = []
                seen[y][x] = True
                while q:
                    cx, cy = q.popleft(); cell.append((cx, cy))
                    for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                        nx, ny = cx+dx, cy+dy
                        if 0<=nx<W and 0<=ny<H and not seen[ny][nx] and walk(g[ny][nx]):
                            seen[ny][nx] = True; q.append((nx, ny))
                out.append(cell)
        return out
    carved = 0
    for _ in range(40):
        cs = comps()
        if len(cs) <= 1:
            break
        cs.sort(key=len, reverse=True)
        main = set(cs[0])
        # non-water landmass reachable from main (same continent test)
        land_seen = set()
        q = deque(main)
        for p in main: land_seen.add(p)
        while q:
            cx, cy = q.popleft()
            for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nx, ny = cx+dx, cy+dy
                if 0<=nx<W and 0<=ny<H and (nx,ny) not in land_seen and not water(g[ny][nx]):
                    land_seen.add((nx,ny)); q.append((nx,ny))
        # reconnect the largest off-main pocket that is on the same landmass
        target = None
        for c in cs[1:]:
            if any(p in land_seen for p in c):
                target = c; break
        if target is None:
            break   # remaining pockets are islands across water -- leave them
        # BFS from main over non-water, cost 0 through walkable, 1 through block,
        # to the nearest target tile; carve the blocking tiles on that path.
        tgt = set(target)
        dist = {}; prev = {}
        pq = deque()
        for p in main:
            dist[p] = 0; pq.append(p)
        # 0-1 BFS
        found = None
        while pq:
            cur = pq.popleft()
            if cur in tgt:
                found = cur; break
            cx, cy = cur
            for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)):
                nx, ny = cx+dx, cy+dy
                if not (0<=nx<W and 0<=ny<H) or water(g[ny][nx]):
                    continue
                w = 0 if walk(g[ny][nx]) else 1
                nd = dist[cur] + w
                if (nx,ny) not in dist or nd < dist[(nx,ny)]:
                    dist[(nx,ny)] = nd; prev[(nx,ny)] = cur
                    (pq.appendleft if w == 0 else pq.append)((nx,ny))
        if found is None:
            break
        node = found
        while node in prev:
            x, y = node
            if not walk(g[y][x]):
                g[y][x] = GRASS; carved += 1
            node = prev[node]
    return carved


def despeckle(g, codes):
    """Remove terrain shapes REQ-229a defines no edge variant for.

    Two patterns have no variant and therefore render as a hard step:
      * three or more differing cardinal neighbours (a 1-tile-wide feature)
      * exactly two differing cardinals that are OPPOSITE (a 1-tile-wide
        channel or isthmus: N and S, or E and W)

    Rather than chase these by hand in the span tables, absorb each offending
    tile into whichever neighbouring terrain surrounds it most. Iterates to a
    fixed point, since absorbing one tile can expose another.
    """
    terr = {k: v.get('terrain') for k, v in codes.items()}
    CARD = {'N': (0, -1), 'S': (0, 1), 'E': (1, 0), 'W': (-1, 0)}
    DIAG = {'NE': (1, -1), 'SE': (1, 1), 'SW': (-1, 1), 'NW': (-1, -1)}
    OPP = ({'N', 'S'}, {'E', 'W'})
    plain = {'grass': GRASS, 'water': SEA, 'forest': FOREST,
             'mountain': MTN, 'desert': 'D'}
    total = 0
    for _ in range(24):
        fixed = 0
        for y in range(H):
            for x in range(W):
                t = terr[g[y][x]]
                nb = {}
                for d, (dx, dy) in CARD.items():
                    if 0 <= x + dx < W and 0 <= y + dy < H:
                        nb[d] = terr[g[y + dy][x + dx]]
                diff = {d for d, nt in nb.items() if nt != t}
                if not diff:
                    dg = {}
                    for d, (dx, dy) in DIAG.items():
                        if 0 <= x + dx < W and 0 <= y + dy < H:
                            nt = terr[g[y + dy][x + dx]]
                            if nt != t:
                                dg[d] = nt
                    if len(dg) < 2:
                        continue
                    nb, diff = dg, set(dg)
                elif len(diff) < 2 or (len(diff) == 2 and diff not in OPP):
                    continue
                counts = {}
                for d in diff:
                    counts[nb[d]] = counts.get(nb[d], 0) + 1
                win = max(counts, key=lambda k: (counts[k], k == 'water'))
                g[y][x] = plain[win]
                fixed += 1
        total += fixed
        if not fixed:
            break
    return total


# --- the furnish pass (OPENBOUNTY-SPEC REQ-229a) ------------------------------
# Every non-grass terrain ships twelve edge variants; they are baked into the
# .dat by the author, because furnish_map in the engine is a deliberate no-op.
# Without this pass every coastline renders as a hard stair-step.
#
# Two families: water is 0-based 00-11, the others 1-based 01-12, with
# different permutations. Cardinals take precedence over diagonals.
DIRS8 = {'N': (0, -1), 'S': (0, 1), 'E': (1, 0), 'W': (-1, 0),
         'NE': (1, -1), 'SE': (1, 1), 'SW': (-1, 1), 'NW': (-1, -1)}
WATER_IDX = {'N': 10, 'S': 11, 'E': 8, 'W': 9,
             'NE_c': 0, 'NW_c': 1, 'SW_c': 2, 'SE_c': 3,
             'NE': 5, 'SE': 4, 'SW': 6, 'NW': 7}
OTHER_IDX = {'N': 11, 'S': 12, 'E': 9, 'W': 10,
             'NE_c': 3, 'NW_c': 1, 'SW_c': 2, 'SE_c': 4,
             'NE': 6, 'SE': 5, 'SW': 7, 'NW': 8}


def furnish(g, codes):
    """Rewrite plain terrain tiles into their edge variants in place."""
    art2code = {v['art']: k for k, v in codes.items()}
    terr = {k: v.get('terrain') for k, v in codes.items()}
    out = [row[:] for row in g]
    changed = 0
    unresolved = []

    for y in range(H):
        for x in range(W):
            t = terr[g[y][x]]
            if t == 'grass':          # grass ships no edge variants
                continue
            # Out-of-bounds counts as the SAME terrain, so a range running off
            # the map edge stays solid instead of transitioning to nothing.
            diff = {d for d, (dx, dy) in DIRS8.items()
                    if 0 <= x + dx < W and 0 <= y + dy < H
                    and terr[g[y + dy][x + dx]] != t}
            if not diff:
                continue
            card = diff & {'N', 'S', 'E', 'W'}
            m = WATER_IDX if t == 'water' else OTHER_IDX
            if card == {'N', 'E'}:
                idx = m['NE_c']
            elif card == {'N', 'W'}:
                idx = m['NW_c']
            elif card == {'S', 'W'}:
                idx = m['SW_c']
            elif card == {'S', 'E'}:
                idx = m['SE_c']
            elif len(card) == 1:
                idx = m[next(iter(card))]
            elif not card:
                d = diff & {'NE', 'SE', 'SW', 'NW'}
                if len(d) != 1:
                    unresolved.append((x, y, t, 'multi-diagonal'))
                    continue
                idx = m[next(iter(d))]
            else:
                unresolved.append((x, y, t, f'{len(card)} cardinals'))
                continue
            code = art2code.get(f"{t}_edge_{idx:02d}")
            if code:
                out[y][x] = code
                changed += 1
            else:
                unresolved.append((x, y, t, idx))
    return out, changed, unresolved


HEADER = """\
# Italia -- 40x64. Home zone of the Glory of Rome pack.
#
# Alpine arc across the north with three passes; the Po valley beneath it; the
# peninsula running SE with the Apennine spine down its western side, broken
# for the coast roads; the Calabrian toe and Apulian heel at the south.
# Sicilia off the toe. Corsica and Sardinia offshore west, clear water around.
#
# Authored by tools/gen_italia.py -- edit the span tables there, not this file.
# Verify: tools/mapcheck.py assets/kings-bounty <this file> 40x64
# Render: tools/maprender.py assets/kings-bounty <this file> out.png
"""


def main():
    root = os.path.normpath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), os.pardir))
    with open(os.path.join(root, "assets", "kings-bounty", "game.json")) as f:
        codes = json.load(f)["tile_codes"]

    g = build()
    carved = connect_mainland(g)
    smoothed = despeckle(g, codes)
    connect_mainland(g)                  # reconnect anything despeckle re-isolated
    assert_south_gate(g)                 # LAST: cut the neck to one passage. The
                                         # toe was connected before this; now its
                                         # only link is the carved PASS column.
    g, changed, unresolved = furnish(g, codes)

    out = os.path.join(root, "assets", "glory-of-rome", "maps", "italia.dat")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write(HEADER + "\n".join("".join(r) for r in g) + "\n")
    distinct = len({c for r in g for c in r})
    print(f"wrote {out} ({W}x{H})")
    print(f"  carved {carved} tile(s) to keep the mainland connected")
    print(f"  despeckled {smoothed} tile(s) with no legal edge variant")
    print(f"  furnished {changed} tiles into edge variants; "
          f"{distinct} distinct tile codes in use")
    if unresolved:
        print(f"  WARNING: {len(unresolved)} tile(s) left plain while bordering "
              f"another terrain -- REQ-229a defines no variant for these, so "
              f"they render as a hard step. Reshape the coastline:")
        for (x, y, t, why) in unresolved[:10]:
            print(f"    ({x},{y}) {t}: {why}")


if __name__ == "__main__":
    main()
