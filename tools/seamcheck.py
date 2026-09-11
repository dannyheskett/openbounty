#!/usr/bin/env python3
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
import json, sys
from PIL import Image

lay = json.load(open(sys.argv[1]))
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
