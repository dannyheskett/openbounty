#!/usr/bin/env python3
"""Recode the cells REQ-229a cannot express: one-wide strips, spits and
islands (REQ-229e), in every zone map of a pack.

    python3 tools/spitfix.py <pack-dir> [--dry]

For each non-grass cell whose OPEN cardinals (neighbours of another
terrain; outside the map counts as the same terrain, as the generator does)
are an opposite pair, three sides or all four, the matching
`<terrain>_edge_NN` code from the pack's tile_codes is written in place.
Cells the pack has no code for are reported and left alone. Only these
cells change; every other byte of the map is untouched.
"""
import glob, json, os, sys

pack = sys.argv[1]
dry = "--dry" in sys.argv
tc = json.load(open(os.path.join(pack, "game.json")))["tile_codes"]
art2code = {v["art"]: k for k, v in tc.items()}
SPIT = {frozenset("NS"): 13, frozenset("EW"): 14, frozenset("NES"): 15,
        frozenset("ESW"): 16, frozenset("SWN"): 17, frozenset("WNE"): 18, frozenset("NESW"): 19}

for path in sorted(glob.glob(os.path.join(pack, "maps", "*.dat"))):
    lines = open(path).read().split("\n")
    head = [i for i, l in enumerate(lines) if l.startswith("#")]
    off = head[-1] + 1 if head else 0
    rows = [list(l) for l in lines[off:] if l]
    H = len(rows); W = max(len(r) for r in rows)
    def terr(x, y, t):
        if not (0 <= x < W and 0 <= y < H): return t
        r = rows[y]
        return tc[r[x]]["terrain"] if x < len(r) else "grass"
    changed, missing = [], []
    for y in range(H):
        for x in range(len(rows[y])):
            t = tc[rows[y][x]]["terrain"]
            if t == "grass": continue
            card = frozenset(s for s, (dx, dy) in (("N", (0, -1)), ("S", (0, 1)), ("E", (1, 0)), ("W", (-1, 0)))
                             if terr(x + dx, y + dy, t) != t)
            if card not in SPIT: continue
            idx = SPIT[card] - (1 if t == "water" else 0)
            code = art2code.get(f"{t}_edge_{idx:02d}")
            if not code:
                missing.append((x, y, t, "".join(sorted(card)))); continue
            if rows[y][x] != code:
                changed.append((x, y, t, "".join(sorted(card)), rows[y][x], code))
                rows[y][x] = code
    print(os.path.basename(path), "recoded", len(changed), "cells;", "no code for", missing if missing else "none")
    for c in changed: print("   ", c)
    if changed and not dry:
        lines[off:off + H] = ["".join(r) for r in rows]
        open(path, "w").write("\n".join(lines))
