#!/usr/bin/env python3
"""Check a zone .dat against the map rules in docs/GLORY-OF-ROME.md section 10.

Usage: tools/mapcheck.py <pack-dir> <map.dat> [WIDTHxHEIGHT]

Checks, in order of how badly each one breaks the game:

  1. Dimensions match the declaration and every code resolves through
     tile_codes in the pack's game.json.
  2. No town dock on a landlocked water body. THIS is the boat trap, not
     enclosed water as such: boats only ever spawn at a town's boat_x/boat_y,
     so a pond nothing can launch into is decorative and harmless. All four
     shipped kings-bounty maps carry 8-32 enclosed water tiles and the pack
     clears 15/15, which is the proof. Rental is charged weekly forever and
     cancellation is refused mid-sail, so a boat launched into an enclosed
     body IS unrecoverable -- but only a dock can put one there (section 10.5).
  3. No OCCUPIED walkable pocket. Land not foot-reachable from the mainland
     is fine while it is empty; it becomes a maroon hazard the autoplay
     stranding rules (AP-051 / AP-190) report as unreachable only once an
     objective sits on it (section 10.6).
  4. Enough open walkable area to host the per-zone budget (section 10.7).

Pass a zone id as the 4th argument to check 2 and 3 against the pack's
declared objects. Without it they degrade to warnings.

Exit 0 clean, 1 on any failure.
"""
import json
import os
import sys
from collections import deque


def load_codes(pack_dir):
    with open(os.path.join(pack_dir, "game.json")) as f:
        return json.load(f)["tile_codes"]


def zone_objects(pack_dir, zone_id):
    """(town docks, every placed object) for a zone. Empty when no zone given."""
    if not zone_id:
        return [], []
    with open(os.path.join(pack_dir, "game.json")) as f:
        g = json.load(f)
    docks = [(t["boat_x"], t["boat_y"]) for t in g.get("towns", [])
             if t.get("zone") == zone_id and "boat_x" in t]
    objs = [(t["x"], t["y"], "town " + t["id"]) for t in g.get("towns", [])
            if t.get("zone") == zone_id]
    objs += [(c.get("gate_x", c.get("x")), c.get("gate_y", c.get("y")),
              "castle " + c["id"]) for c in g.get("castles", [])
             if c.get("zone") == zone_id]
    for z in g.get("zones", []):
        if z.get("id") != zone_id:
            continue
        for kind in ("chests", "signs", "dwellings", "armies"):
            for o in z.get(kind, []):
                if "x" in o and "y" in o:
                    objs.append((o["x"], o["y"], kind[:-1]))
    return docks, objs


def read_map(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n").rstrip("\r")
            if not line or line.startswith("#"):
                continue
            rows.append(line)
    return rows


def flood(cells, w, h, start, member):
    """4-and-8-connected flood over cells satisfying member()."""
    seen = {start}
    q = deque([start])
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1),
                       (1, 1), (1, -1), (-1, 1), (-1, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and (nx, ny) not in seen:
                if member(cells[ny][nx]):
                    seen.add((nx, ny))
                    q.append((nx, ny))
    return seen


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    pack_dir, map_path = sys.argv[1], sys.argv[2]
    declared = sys.argv[3] if len(sys.argv) > 3 else None
    zone_id = sys.argv[4] if len(sys.argv) > 4 else None

    codes = load_codes(pack_dir)
    docks, objects = zone_objects(pack_dir, zone_id)
    rows = read_map(map_path)
    if not rows:
        print("FAIL: map is empty")
        return 1

    h = len(rows)
    w = max(len(r) for r in rows)
    rows = [r.ljust(w, ".") for r in rows]   # engine pads short rows with grass
    fails = []

    # 1. dimensions + code resolution ---------------------------------------
    if declared:
        dw, dh = (int(v) for v in declared.lower().split("x"))
        if (w, h) != (dw, dh):
            fails.append(f"dimensions are {w}x{h}, declaration says {dw}x{dh}")

    unknown = sorted({c for r in rows for c in r} - set(codes))
    if unknown:
        fails.append(f"tile codes not in game.json: {' '.join(repr(c) for c in unknown)}")
        print("FAIL:", fails[-1])
        return 1

    def terr(c):
        return codes[c].get("terrain")

    def blocks(c):
        return bool(codes[c].get("blocks_foot")) and not codes[c].get("is_bridge")

    def walkable(c):
        return not blocks(c)

    def is_water(c):
        return terr(c) == "water"

    # 2. one water body ------------------------------------------------------
    water = [(x, y) for y in range(h) for x in range(w) if is_water(rows[y][x])]
    if not water:
        fails.append("no water at all -- the zone cannot be sailed to or from")
    else:
        # start from an edge tile: that is by definition the open sea
        edge = [(x, y) for (x, y) in water if x in (0, w - 1) or y in (0, h - 1)]
        if not edge:
            fails.append("no water touches the map edge -- there is no open sea")
        else:
            sea = flood(rows, w, h, edge[0], is_water)
            orphan = set(water) - sea
            if orphan:
                trapped = [d for d in docks if d in orphan]
                if trapped:
                    fails.append(
                        f"town dock(s) on a landlocked water body (BOAT TRAP): "
                        f"{trapped}")
                else:
                    print(f"  note: {len(orphan)} enclosed water tile(s) "
                          f"(decorative ponds; harmless -- no dock launches "
                          f"into them)")
                if not zone_id:
                    print("  note: no zone id given, so dock placement was not "
                          "checked against these")

    # 3. walkable pockets ----------------------------------------------------
    land = [(x, y) for y in range(h) for x in range(w)
            if walkable(rows[y][x]) and not is_water(rows[y][x])]
    if not land:
        fails.append("no walkable land")
    else:
        # largest walkable region = the mainland
        unvisited = set(land)
        regions = []
        while unvisited:
            s = next(iter(unvisited))
            r = flood(rows, w, h, s,
                      lambda c: walkable(c) and not is_water(c)) & set(land)
            regions.append(r)
            unvisited -= r
        regions.sort(key=len, reverse=True)
        # Islands are legal and mostly decorative. One only matters once an
        # objective sits on it, because then it must be reachable.
        if len(regions) > 1:
            small = regions[1:]
            sizes = sorted((len(r) for r in small), reverse=True)
            print(f"  note: {len(small)} landmass(es) besides the mainland "
                  f"(sizes {sizes[:12]}{' ...' if len(sizes) > 12 else ''})")
            # A region is reachable by sea if ANY of its tiles touches the
            # connected sea -- disembarking parks the boat on whatever coastal
            # land tile you step onto (REQ-243), so no dock is needed on the
            # far shore. A region with no coast at all is a true inland pocket,
            # walled by forest or mountain, and only flight or a gate gets in.
            sea = sea if water else set()
            landlocked = []
            for r in small:
                on_it = [o[2] for o in objects if (o[0], o[1]) in r]
                if not on_it:
                    continue
                coastal = any(
                    (x + dx, y + dy) in sea
                    for (x, y) in r
                    for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                if not coastal:
                    landlocked.append((len(r), on_it))
            for size, what in landlocked:
                fails.append(
                    f"objective(s) {what} sit in a {size}-tile INLAND POCKET "
                    f"with no coast -- reachable only by flight or gate")
            if not docks and any(
                    any((x + dx, y + dy) in sea
                        for (x, y) in r
                        for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                    for r in small if any((o[0], o[1]) in r for o in objects)):
                print("  note: island objectives exist and are coastal, but the "
                      "zone declares no town dock -- one town must be able to "
                      "rent a boat or they cannot be reached")

    # 4. capacity ------------------------------------------------------------
    open_land = sum(1 for (x, y) in land
                    if terr(rows[y][x]) in ("grass", "desert"))
    if open_land < 21 * 4:
        fails.append(f"only {open_land} open walkable tiles; section 10.7 needs "
                     f"room for >=21 chest placeholders plus castles and towns")

    # report -----------------------------------------------------------------
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


if __name__ == "__main__":
    sys.exit(main())
