#!/usr/bin/env python3
"""Author assets/glory-of-rome/maps/italia.dat -- 40 x 64, the home zone.

Hand-tuned: every coastline row below is an explicit choice, not a formula.
The tables ARE the map. Edit a span, re-run, re-check with tools/mapcheck.py.

Shape: the Alpine arc walls the north, the Po valley sits beneath it, the
peninsula runs SE with the Apennine spine down its western side, and it forks
into the Calabrian toe and the Apulian heel. Sicilia lies off the toe; Corsica
and Sardinia stand offshore west with clear water on every side.
"""
import os

W, H = 40, 64
SEA, GRASS, VAR, FOREST, MTN = '~', '.', ',', 'F', '^'

# --- mainland: row -> (x0, x1) inclusive -------------------------------------
# Rows 0-4 are the Alps and span the FULL WIDTH: the range is the map's
# northern boundary, not an island's coastline. Italia is closed off to the
# north by mountain rather than by sea, so the zone reads as part of a
# continent whose interior lies beyond the edge.
MAINLAND = {
    0:  (0, W - 1), 1: (0, W - 1), 2: (0, W - 1),     # Alpine wall, edge to edge
    3:  (0, W - 1), 4: (0, W - 1),
    5:  (5, 35),  6:  (4, 35),  7:  (4, 34),          # Po valley, widest
    8:  (5, 33),  9:  (6, 32), 10:  (7, 30),
    11: (8, 28), 12: (8, 27), 13: (9, 27),            # Liguria / Etruria neck
    14: (9, 26), 15: (10, 26), 16: (10, 27),
    17: (10, 24), 18: (10, 24), 19: (11, 25),         # the peninsula proper
    20: (12, 24), 21: (13, 25), 22: (13, 25),
    23: (14, 26), 24: (14, 26), 25: (15, 27),
    26: (16, 26), 27: (17, 27), 28: (17, 27),
    29: (18, 28), 30: (18, 28), 31: (19, 29),
    32: (19, 29), 33: (20, 30), 34: (20, 30),
    35: (21, 31), 36: (22, 30), 37: (23, 31),
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
SICILIA = {54: (13, 21), 55: (12, 22), 56: (12, 22), 57: (13, 21), 58: (15, 19)}
CORSICA = {19: (3, 7), 20: (3, 7), 21: (3, 7), 22: (3, 7), 23: (4, 7), 24: (4, 6)}
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
SPINE_ROWS = range(14, 47)
SPINE_GAPS = {18, 24, 29, 35, 41, 45}
SPINE_OFFSET = 3          # tiles west of the row's centre

# --- woodland: (row, x0, x1) -------------------------------------------------
PO_FOREST = [(6, 9, 14), (7, 10, 13), (6, 20, 25), (7, 21, 24), (8, 22, 26)]
ETRURIA_FOREST = [(21, 14, 16), (22, 14, 15), (25, 16, 18), (26, 17, 19),
                  (31, 20, 22), (32, 21, 23), (37, 24, 26), (38, 25, 26)]
ISLAND_PEAKS = [(21, 5), (30, 4), (32, 5), (56, 17)]   # Corsica, Sardinia, Etna


def build():
    g = [[SEA] * W for _ in range(H)]

    def span(y, x0, x1, ch=GRASS):
        for x in range(max(0, x0), min(W, x1 + 1)):
            g[y][x] = ch

    for table in (MAINLAND, TOE, HEEL, SICILIA, CORSICA, SARDINIA):
        for y, (x0, x1) in table.items():
            span(y, x0, x1)

    for y in ALPS_ROWS:                       # Alps overwrite their own rows
        x0, x1 = MAINLAND[y]
        span(y, x0, x1, MTN)
    for y, x in ALPINE_PASSES:
        g[y][x] = GRASS

    for y in SPINE_ROWS:                      # Apennines
        if y in SPINE_GAPS or y not in MAINLAND:
            continue
        x0, x1 = MAINLAND[y]
        sx = (x0 + x1) // 2 - SPINE_OFFSET
        for x in (sx, sx + 1):
            if x0 < x < x1:                   # never let the spine touch coast
                g[y][x] = MTN

    for y, x0, x1 in PO_FOREST + ETRURIA_FOREST:
        for x in range(x0, x1 + 1):
            if g[y][x] == GRASS:
                g[y][x] = FOREST
    for y, x in ISLAND_PEAKS:
        g[y][x] = MTN

    for y in range(H):                        # texture speckle
        for x in range(W):
            if g[y][x] == GRASS and (x * 7 + y * 13) % 19 == 0:
                g[y][x] = VAR
    return g


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
    g = build()
    out = os.path.normpath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), os.pardir,
        "assets", "glory-of-rome", "maps", "italia.dat"))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write(HEADER + "\n".join("".join(r) for r in g) + "\n")
    print(f"wrote {out} ({W}x{H})")


if __name__ == "__main__":
    main()
