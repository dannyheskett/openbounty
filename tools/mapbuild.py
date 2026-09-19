#!/usr/bin/env python3
"""Bake a zone map from its hand-drawn source, check it, place its objects.

    python3 tools/mapbuild.py build <pack-dir> <zone-id> <source.txt> <out.dat>
    python3 tools/mapbuild.py check <pack-dir> <zone-id> <map.dat>
    python3 tools/mapbuild.py place <pack-dir> <zone-id> <map.dat> <regions.json>

Nothing is written unless an output path is named; `--help` only prints this.

The SOURCE is the map. One character per tile, `#` lines are comments:

  terrain   ~ sea   . grass   , grass variant   f forest   ^ mountain   d desert
  overlays  r river on grass   R river in forest   M river in mountains
            = road             H bridge (a road crossing a river)

`build` turns it into the fully furnished .dat the engine loads
(GLORY-OF-ROME 10.6.1: nothing about a map's look is computed at game time):

  - terrain edges by OPENBOUNTY-SPEC REQ-229a/e: a cell takes the variant for
    the neighbours of a different terrain, cardinals before diagonals, the
    map's outside counting as the same terrain. A river through a wood counts
    as wood, through mountains as mountain; plain rivers, roads and bridges as
    grass; a river mouth, drawn on a coast tile, as sea. A shape the pack
    ships no variant for is an error naming the cell.
  - rivers and roads by their links: straights, curves, ends, diagonals, the
    straight-to-diagonal joins and the corner companions a diagonal needs.
    A river ending against the sea is its mouth (river_mouth_e / _w). Rivers
    link only orthogonally: movement is 8-way with no corner rule, so a
    diagonal river would let the hero step across it.
  - a bridge is the river bridge crossing the river at right angles.
  - a road cell under a town or castle may have any number of exits; its
    sprite covers the ground.

`check` asserts every object stands on walkable ground, every dock is on the
open sea, and reports what the hero can reach from the spawn on foot and by
boat: with the static guardians holding their tiles, with them beaten, and
then with every river bridged too.

`place` scatters a zone's chests and wandering armies inside hand-drawn
region boxes from a fixed seed, on open grass or sand, and writes them into
game.json.
"""
import json
import os
import random
import sys
from collections import deque

DIRS4 = {'n': (0, -1), 'e': (1, 0), 's': (0, 1), 'w': (-1, 0)}
DIRS8 = dict(DIRS4, ne=(1, -1), se=(1, 1), sw=(-1, 1), nw=(-1, -1))
OPP = {'n': 's', 's': 'n', 'e': 'w', 'w': 'e',
       'ne': 'sw', 'sw': 'ne', 'nw': 'se', 'se': 'nw'}

BASE = {'~': 'water', '.': 'grass', ',': 'grass', 'f': 'forest',
        '^': 'mountain', 'd': 'desert'}
PLAIN_ART = {'~': 'water', '.': 'grass', ',': 'grass_variant', 'f': 'forest',
             '^': 'mountain', 'd': 'desert'}
RIVER = {'r': 'grass', 'R': 'forest', 'M': 'mountain', 'H': 'grass'}
ROAD = {'=', 'H'}
RIVER_PREFIX = {'grass': 'river_', 'forest': 'river_forest_',
                'mountain': 'river_mountain_'}

# REQ-229a: two families, water 0-based, the rest 1-based.
WATER_IDX = {'n': 10, 's': 11, 'e': 8, 'w': 9, 'ne_c': 0, 'nw_c': 1,
             'sw_c': 2, 'se_c': 3, 'ne': 5, 'se': 4, 'sw': 6, 'nw': 7}
OTHER_IDX = {'n': 11, 's': 12, 'e': 9, 'w': 10, 'ne_c': 3, 'nw_c': 1,
             'sw_c': 2, 'se_c': 4, 'ne': 6, 'se': 5, 'sw': 7, 'nw': 8}
# REQ-229e: strips, spits and the island, keyed by the open cardinals.
SPIT_IDX = {frozenset('ns'): 13, frozenset('ew'): 14, frozenset('nes'): 15,
            frozenset('esw'): 16, frozenset('swn'): 17, frozenset('wne'): 18,
            frozenset('nesw'): 19}

CURVES = {frozenset('ns'): 'ns', frozenset('ew'): 'ew', frozenset('ne'): 'ne',
          frozenset('es'): 'es', frozenset('sw'): 'sw', frozenset('wn'): 'wn'}
JOINS = {('n', 'sw'), ('n', 'se'), ('s', 'nw'), ('s', 'ne'),
         ('e', 'nw'), ('e', 'sw'), ('w', 'ne'), ('w', 'se')}


def die(msg):
    sys.exit(f"mapbuild: {msg}")


def code_char(key):
    if len(key) == 4 and key[0] == "\\" and key[1] in "xX":
        return chr(int(key[2:], 16))
    return key if len(key) == 1 else None


def load_pack(pack):
    with open(os.path.join(pack, "game.json")) as f:
        return json.load(f)


def art_codes(g):
    """art name -> map byte, and map byte -> tile_codes entry."""
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
    die(f"no zone '{zid}'")


def object_cells(g, zid):
    """{(x, y): label} for towns and castles -- their sprites cover the ground."""
    z = zone_of(g, zid)
    out = {}
    for t in z.get("towns", []):
        out[(t["x"], t["y"])] = "town " + t["id"]
    for c in z.get("castles", []):
        out[(c["x"], c["y"])] = "castle " + c["id"]
    return out


def read_rows(path):
    with open(path, encoding="latin-1") as f:
        return [l.rstrip("\n").rstrip("\r") for l in f
                if l.strip() and not l.startswith("#")]


# ---- build -----------------------------------------------------------------

def build(pack, zid, src, out):
    g = load_pack(pack)
    a2c, _ = art_codes(g)
    z = zone_of(g, zid)
    W, H = z["width"], z["height"]
    rows = read_rows(src)
    if len(rows) != H or any(len(r) != W for r in rows):
        die(f"{src}: want {W}x{H}, have {len(rows)} rows of "
            f"{sorted({len(r) for r in rows})}")
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c not in BASE and c not in RIVER and c not in ROAD:
                die(f"({x},{y}): unknown source character {c!r}")
    objs = object_cells(g, zid)
    errors = []

    def at(x, y):
        return rows[y][x] if 0 <= x < W and 0 <= y < H else None

    def links(x, y, kind):
        """The directions this cell's run continues in."""
        member = (lambda c: c in RIVER) if kind == 'river' else (lambda c: c in ROAD)
        out = set()
        for d, (dx, dy) in DIRS4.items():
            if member(at(x + dx, y + dy) or ''):
                out.add(d)
        if kind == 'road':
            for d, (dx, dy) in DIRS8.items():
                if len(d) != 2 or not member(at(x + dx, y + dy) or ''):
                    continue
                # A diagonal only where the run does not turn the corner itself.
                if member(at(x + dx, y) or '') or member(at(x, y + dy) or ''):
                    continue
                out.add(d)
        elif kind == 'river':
            for d, (dx, dy) in DIRS8.items():
                if len(d) == 2 and member(at(x + dx, y + dy) or '') \
                        and not member(at(x + dx, y) or '') \
                        and not member(at(x, y + dy) or ''):
                    errors.append(f"({x},{y}): river links diagonally to "
                                  f"({x + dx},{y + dy}) -- make it a corner")
        return out

    def piece(ex):
        orth = {d for d in ex if len(d) == 1}
        diag = {d for d in ex if len(d) == 2}
        if len(ex) == 1 and orth:
            return next(iter(orth))
        if len(ex) == 2 and len(orth) == 2:
            return CURVES.get(frozenset(orth))
        if len(ex) == 2 and len(diag) == 2:
            if diag == {'ne', 'sw'}:
                return 'nesw'
            if diag == {'nw', 'se'}:
                return 'nwse'
        if len(ex) == 2 and len(orth) == 1 and len(diag) == 1:
            o, c = next(iter(orth)), next(iter(diag))
            if (o, c) in JOINS:
                return f"{o}_{c}"
        return None

    out_art = [[None] * W for _ in range(H)]
    cls = [[None] * W for _ in range(H)]          # terrain class for edges
    for y in range(H):
        for x in range(W):
            c = rows[y][x]
            cls[y][x] = BASE.get(c) or RIVER.get(c) or 'grass'

    companions = {}                                # (x, y) -> [(kind, corner)]

    for y in range(H):
        for x in range(W):
            c = rows[y][x]
            if c in RIVER and c != 'H':
                ex = links(x, y, 'river')
                prefix = RIVER_PREFIX[RIVER[c]]
                sea = [d for d, (dx, dy) in DIRS4.items() if at(x + dx, y + dy) == '~']
                if len(ex) == 1 and len(sea) >= 1:
                    inflow = next(iter(ex))
                    if inflow == 'w' and 'e' in sea:
                        out_art[y][x] = 'river_mouth_e'
                    elif inflow == 'e' and 'w' in sea:
                        out_art[y][x] = 'river_mouth_w'
                    else:
                        errors.append(f"({x},{y}): river meets the sea from the "
                                      f"{inflow}; the pack has mouths for east "
                                      f"and west only")
                    # The mouth piece is drawn on a coast tile whose sea side
                    # is open water: its neighbours see it as sea.
                    cls[y][x] = 'water'
                    continue
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
                    if (x, y) in objs:
                        out_art[y][x] = 'grass'
                        continue
                    errors.append(f"({x},{y}): road with exits {sorted(ex)} "
                                  f"has no piece")
                    continue
                out_art[y][x] = 'road_' + p
                for d in ex:
                    if len(d) != 2:
                        continue
                    dx, dy = DIRS8[d]
                    # The corner the diagonal crosses, seen from each flank.
                    for fx, fy, corner in ((x + dx, y, ('s' if dy > 0 else 'n') +
                                            ('w' if dx > 0 else 'e')),
                                           (x, y + dy, ('n' if dy > 0 else 's') +
                                            ('e' if dx > 0 else 'w'))):
                        companions.setdefault((fx, fy), set()).add(corner)

    for (x, y), corners in companions.items():
        c = at(x, y)
        if c is None:
            continue
        if c in ROAD or c in RIVER:
            continue            # the flank is road already; nothing to add
        if len(corners) > 1:
            errors.append(f"({x},{y}): two diagonals cross this cell's corners "
                          f"{sorted(corners)}; there is no piece")
            continue
        if BASE.get(c) != 'grass':
            errors.append(f"({x},{y}): a road diagonal's corner falls on "
                          f"{BASE.get(c)}; its companion needs grass")
            continue
        out_art[y][x] = 'road_c_' + next(iter(corners))

    # Terrain edges.
    for y in range(H):
        for x in range(W):
            if out_art[y][x] is not None:
                continue
            c = rows[y][x]
            if c not in BASE:
                continue            # an overlay whose error is already listed
            t = BASE[c]
            if t == 'grass':
                out_art[y][x] = PLAIN_ART[c]
                continue
            diff = {d for d, (dx, dy) in DIRS8.items()
                    if 0 <= x + dx < W and 0 <= y + dy < H
                    and cls[y + dy][x + dx] != t}
            if not diff:
                out_art[y][x] = PLAIN_ART[c]
                continue
            card = frozenset(d for d in diff if len(d) == 1)
            m = WATER_IDX if t == 'water' else OTHER_IDX
            idx = None
            for pair in ('ne', 'nw', 'sw', 'se'):
                if card == frozenset(pair):
                    idx = m[pair + '_c']
            if idx is None and len(card) == 1:
                idx = m[next(iter(card))]
            if idx is None and not card:
                dg = [d for d in diff if len(d) == 2]
                if len(dg) == 1:
                    idx = m[dg[0]]
                else:
                    errors.append(f"({x},{y}): {t} with open diagonals "
                                  f"{sorted(dg)} only; no variant")
                    continue
            if idx is None and card in SPIT_IDX:
                idx = SPIT_IDX[card] - (1 if t == 'water' else 0)
            if idx is None:
                errors.append(f"({x},{y}): {t} open on {sorted(card)}; no variant")
                continue
            name = f"{t}_edge_{idx:02d}"
            if name not in a2c:
                errors.append(f"({x},{y}): {t} open on {sorted(diff)} wants "
                              f"{name}, which the pack does not ship")
                continue
            out_art[y][x] = name

    for y in range(H):
        for x in range(W):
            a = out_art[y][x]
            if a is not None and a not in a2c:
                errors.append(f"({x},{y}): the pack has no tile code for {a}")

    if errors:
        print(f"{len(errors)} error(s):")
        for e in errors[:60]:
            print("  " + e)
        if len(errors) > 60:
            print(f"  ... and {len(errors) - 60} more")
        sys.exit(1)

    header = (f"# {z.get('name', zid)} -- {W}x{H}.\n"
              f"#\n"
              f"# BUILT by tools/mapbuild.py from {os.path.relpath(src)}.\n"
              f"# Do not edit this file: edit the source and rebuild.\n"
              f"# Check: tools/mapbuild.py check {pack} {zid} <this file>\n")
    with open(out, "w", encoding="latin-1") as f:
        f.write(header)
        for y in range(H):
            f.write("".join(a2c[out_art[y][x]] for x in range(W)) + "\n")
    print(f"wrote {out} ({W}x{H})")


# ---- check -----------------------------------------------------------------

def terrain_grid(pack, zid, path):
    g = load_pack(pack)
    _, c2e = art_codes(g)
    z = zone_of(g, zid)
    W, H = z["width"], z["height"]
    rows = read_rows(path)
    if len(rows) != H or any(len(r) != W for r in rows):
        die(f"{path}: not {W}x{H}")
    ter = [[c2e[c]["terrain"] if c in c2e else die(f"unknown byte {c!r}")
            for c in r] for r in rows]
    art = [[c2e[c]["art"] for c in r] for r in rows]
    return g, z, W, H, ter, art


def walkable(t):
    return t in ("grass", "desert")


def reach(W, H, ter, start, docks, open_rivers, arrivals=()):
    """Tiles the hero can stand on: foot from the spawn, then every landing on
    the water body of a dock the hero has reached (a boat sails only the water
    it was rented on), repeated until nothing new is reached. A hero who sails
    in lands in a boat at an arrival, so its water body is sailed from the
    start. Also returns the set of water tiles connected to the map edge (the
    open sea)."""
    def ok(x, y):
        t = ter[y][x]
        return walkable(t) or (open_rivers and t == "river")

    def body(seed):
        out = {seed}
        q = deque([seed])
        while q:
            x, y = q.popleft()
            for dx, dy in DIRS8.values():
                n = (x + dx, y + dy)
                if 0 <= n[0] < W and 0 <= n[1] < H and n not in out \
                        and ter[n[1]][n[0]] == "water":
                    out.add(n); q.append(n)
        return out

    sea = set()
    for y in range(H):
        for x in range(W):
            if (x in (0, W - 1) or y in (0, H - 1)) and ter[y][x] == "water" \
                    and (x, y) not in sea:
                sea |= body((x, y))

    seen = {start}
    q = deque([start])
    sailed = set()
    first = set()
    for a in arrivals:
        if ter[a[1]][a[0]] == "water" and a not in first:
            first |= body(a)
    while True:
        while q:
            x, y = q.popleft()
            for dx, dy in DIRS8.values():
                n = (x + dx, y + dy)
                if 0 <= n[0] < W and 0 <= n[1] < H and n not in seen and ok(*n):
                    seen.add(n); q.append(n)
        waters, first = first, set()
        sailed |= waters
        for t, d in docks:
            if d in sea and d not in sailed and any(
                    (t[0] + dx, t[1] + dy) in seen or t in seen
                    for dx, dy in DIRS8.values()):
                waters |= body(d)
                sailed |= body(d)
        if not waters:
            break
        for y in range(H):
            for x in range(W):
                if ok(x, y) and (x, y) not in seen and any(
                        (x + dx, y + dy) in waters for dx, dy in DIRS8.values()):
                    seen.add((x, y)); q.append((x, y))
    return seen, sea


def check(pack, zid, path):
    g, z, W, H, ter, art = terrain_grid(pack, zid, path)
    towns = {t["id"]: t for t in g["towns"] if t.get("zone") == zid}
    castles = {c["id"]: c for c in g["castles"] if c.get("zone") == zid}
    bad = []

    def stand(x, y, what):
        if not (0 <= x < W and 0 <= y < H):
            bad.append(f"{what} ({x},{y}) is off the map")
        elif not walkable(ter[y][x]):
            bad.append(f"{what} ({x},{y}) stands on {ter[y][x]} ({art[y][x]})")

    points = {}
    for t in towns.values():
        stand(t["x"], t["y"], f"town {t['id']}")
        gt = t.get("gate") or {}
        if gt.get("x", -1) >= 0:
            stand(gt["x"], gt["y"], f"town {t['id']} gate")
        points["town " + t["id"]] = (t["x"], t["y"])
    for c in castles.values():
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
    for t in towns.values():
        b = t.get("boat") or {}
        if b.get("x", -1) >= 0:
            if ter[b["y"]][b["x"]] != "water":
                bad.append(f"town {t['id']} dock ({b['x']},{b['y']}) is not water")
            docks.append(((t["x"], t["y"]), (b["x"], b["y"])))

    start = (z["hero_spawn"]["x"], z["hero_spawn"]["y"])
    # A town is entered from its own tile; count a town reached when the hero
    # can reach its tile or any tile touching it.
    def reached(seen, p):
        x, y = p
        return p in seen or any((x + dx, y + dy) in seen for dx, dy in DIRS8.values())

    # A static army (a guardian) holds its tile until it is beaten.
    guards = [(a["x"], a["y"]) for a in z.get("wandering_armies", []) if a.get("static")]
    held = [r[:] for r in ter]
    for gx, gy in guards:
        held[gy][gx] = "forest"
    # Sailing in from another zone lands in a boat at that zone's arrival.
    arrivals = [(a["x"], a["y"]) for a in z.get("arrivals", {}).values()]
    # A one-time vista ("events") changes tiles for good once it has played: the
    # Rubicon's bridge is a tile the map never holds until then.
    fired = [r[:] for r in ter]
    codes = {c: v for c, v in g["tile_codes"].items()}
    for ev in z.get("events", []):
        for fx in ev.get("effects", []):
            code = codes.get(fx["tile"])
            if code:
                fired[fx["y"]][fx["x"]] = ("river" if code.get("terrain") == "river"
                                           else code.get("terrain", "grass"))
                if code.get("is_bridge"):
                    fired[fx["y"]][fx["x"]] = "grass"
    guarded, _ = reach(W, H, held, start, docks, False, arrivals)
    shut, sea = reach(W, H, ter, start, docks, False, arrivals)
    open_, _ = reach(W, H, ter, start, docks, True, arrivals)
    played, _ = reach(W, H, fired, start, docks, False, arrivals)
    for t, d in docks:
        if d not in sea:
            bad.append(f"dock {d} is not on the open sea")
    for frm, a in z.get("arrivals", {}).items():
        x, y = a["x"], a["y"]
        if (x, y) not in sea:
            bad.append(f"arrival from {frm} ({x},{y}) is not on the open sea")
        elif not any(0 <= x + dx < W and 0 <= y + dy < H and walkable(ter[y + dy][x + dx])
                     for dx, dy in DIRS8.values()):
            bad.append(f"arrival from {frm} ({x},{y}) touches no land")
    missing = [n for n in z.get("neighbors", []) if n not in z.get("arrivals", {})]
    if z.get("arrivals") and missing:
        bad.append(f"no arrival from {', '.join(missing)}")

    print(f"{zid}: {W}x{H}")
    print(f"  {'':44s} guardians    guardians    rivers        vistas")
    print(f"  {'':44s} standing     beaten       bridged       played")
    for name, p in sorted(points.items(), key=lambda kv: (kv[1][1], kv[1][0])):
        print(f"  {name:44s} {'yes' if reached(guarded, p) else 'NO ':12s} "
              f"{'yes' if reached(shut, p) else 'NO ':12s} "
              f"{'yes' if reached(open_, p) else 'NO ':13s} "
              f"{'yes' if reached(played, p) else 'NO'}")
    counts = {}
    for y in range(H):
        for x in range(W):
            counts[ter[y][x]] = counts.get(ter[y][x], 0) + 1
    print("  terrain: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    print(f"  walkable {sum(v for k, v in counts.items() if walkable(k))}")
    if bad:
        print(f"{len(bad)} problem(s):")
        for b in bad:
            print("  " + b)
        sys.exit(1)
    print("OK")


# ---- place -----------------------------------------------------------------

def place(pack, zid, path, regions_path):
    """regions.json: {"seed": N, "spacing": r, "fixed": {"<army id>": [x, y]},
    "regions": [{"name", "box": [x0,y0,x1,y1], "chests": n, "armies": n}, ...]}

    A static army (a guardian holding a pass) and a "fixed" chest (a prize the
    salt never turns into something else) keep what they are and go where
    "fixed" puts them; every other chest and army is re-scattered."""
    g, z, W, H, ter, art = terrain_grid(pack, zid, path)
    with open(regions_path) as f:
        spec = json.load(f)
    rng = random.Random(spec["seed"])
    taken = set()

    def mark(x, y, r):
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                taken.add((x + dx, y + dy))

    for t in (t for t in g["towns"] if t.get("zone") == zid):
        mark(t["x"], t["y"], 1)
        gt = t.get("gate") or {}
        if gt.get("x", -1) >= 0:
            mark(gt["x"], gt["y"], 1)
    for c in (c for c in g["castles"] if c.get("zone") == zid):
        mark(c["x"], c["y"], 1); mark(c["x"], c["y"] + 1, 1)
    for s in z.get("signs", []):
        mark(s["x"], s["y"], 1)
    fixed_chests = []
    for c in z.get("chests", []):
        if c.get("fixed"):
            if c["id"] not in spec.get("fixed", {}):
                die(f"fixed chest {c['id']} needs a place in \"fixed\"")
            c = dict(c)
            c["x"], c["y"] = spec["fixed"][c["id"]]
            fixed_chests.append(c)
            mark(c["x"], c["y"], 1)
    fixed = []
    for a in z.get("wandering_armies", []):
        if a.get("static"):
            if a["id"] not in spec.get("fixed", {}):
                die(f"static army {a['id']} needs a place in \"fixed\"")
            a = dict(a)
            a["x"], a["y"] = spec["fixed"][a["id"]]
            fixed.append(a)
            mark(a["x"], a["y"], 1)
    for k in ("magic_alcove", "hero_spawn"):
        mark(z[k]["x"], z[k]["y"], 1)

    def free(x, y):
        return (0 <= x < W and 0 <= y < H and (x, y) not in taken
                and art[y][x] in ("grass", "grass_variant", "desert"))

    chests, armies = [], []
    for reg in spec["regions"]:
        x0, y0, x1, y1 = reg["box"]
        cells = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
        rng.shuffle(cells)
        for kind, n in (("chests", reg.get("chests", 0)), ("armies", reg.get("armies", 0))):
            got = 0
            for x, y in cells:
                if got == n:
                    break
                if not free(x, y):
                    continue
                (chests if kind == "chests" else armies).append((x, y))
                mark(x, y, spec.get("spacing", 2))
                got += 1
            if got < n:
                die(f"region {reg['name']}: room for {got} of {n} {kind}")
    z["chests"] = [{"id": f"chest_{i + 1}", "x": x, "y": y}
                   for i, (x, y) in enumerate(chests)] + fixed_chests
    # The guardians first, then the scattered armies.
    z["wandering_armies"] = fixed + [{"x": x, "y": y, "id": f"wandering_army_{i:03d}"}
                                     for i, (x, y) in enumerate(armies)]
    return g, z, chests, armies


def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help") or a[0] not in ("build", "check", "place"):
        print(__doc__)
        return
    if a[0] == "build" and len(a) == 5:
        build(a[1], a[2], a[3], a[4])
    elif a[0] == "check" and len(a) == 4:
        check(a[1], a[2], a[3])
    elif a[0] == "place" and len(a) == 5:
        g, z, c, r = place(a[1], a[2], a[3], a[4])
        print(f"placed {len(c)} chests and {len(r)} armies (not written)")
        print("  " + json.dumps(c))
        print("  " + json.dumps(r))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
