#!/usr/bin/env python3
"""Bake a zone map from its hand-drawn source, check it, place its objects.

    python3 tools/mapbuild.py build <pack-dir> <zone-id> <source.txt> <out.dat> [--strict]
    python3 tools/mapbuild.py check <pack-dir> <zone-id> <map.dat>
    python3 tools/mapbuild.py place <pack-dir> <zone-id> <map.dat> <regions.json> [--add] [--write]

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
    A river ending against the sea is its mouth (river_mouth_e / _w, and the
    _s pieces drawn the other way up where the sea lies to the south). Rivers
    link only orthogonally: movement is 8-way with no corner rule, so a
    diagonal river would let the hero step across it.
  - a bridge is the river bridge crossing the river at right angles.
  - a road cell under a town or castle may have any number of exits; its
    sprite covers the ground.
  - a river whose straight run leaves the map flows off it: the map's edge
    continues the run, so the piece is a straight, not a tapered end.

`build` also warns, naming each cell, about shapes the art draws badly:
a river mouth without sea above and land below it (the mouth art's shape),
a river that ends in open ground rather than at the sea, the map's edge or a
source in the mountains or woods, a forest- or mountain-banked river beside
another terrain (its drawn bank meets that terrain in a seam), and a terrain
sea tile whose different diagonal no water variant can show. --strict makes
every warning an error. A land tile's corner no variant shows (a small
notch) is only counted.

`check` asserts every object stands on walkable ground, every dock is on the
open sea, and reports what the hero can reach from the spawn on foot and by
boat: with the static guardians holding their tiles, with them beaten, and
then with every river bridged too.

`place` scatters a zone's chests and wandering armies inside hand-drawn
region boxes from a fixed seed, on open grass or sand. It prints them; --write
writes them into game.json. --add keeps every chest and army the zone has
where it stands and places only what each region still lacks, so hand-moved
objects and every id survive.
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
        '^': 'mountain', 'd': 'desert', 'P': 'grass', 'T': 'grass'}
# 'P': a landmark standing on grass -- the neighbours see grass, so no edge
# art changes, and the tile draws the landmark (Africa's Pharos).
PLAIN_ART = {'~': 'water', '.': 'grass', ',': 'grass_variant', 'f': 'forest',
             '^': 'mountain', 'd': 'desert', 'P': 'pharos', 'T': 'temple_ocean'}
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

def build(pack, zid, src, out, strict=False):
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
    warnings = []
    notes = []          # land-against-land corners: a notch, never an error

    def at(x, y):
        return rows[y][x] if 0 <= x < W and 0 <= y < H else None

    def inside(x, y):
        return 0 <= x < W and 0 <= y < H

    # A tile an event turns into a bridge (the Rubicon's) is road to its
    # neighbours, so the roads either side point at the crossing to come.
    codes = g["tile_codes"]
    future_bridge = {(fx["x"], fx["y"]) for ev in z.get("events", [])
                     for fx in ev.get("effects", [])
                     if "tile" in fx and codes.get(fx["tile"], {}).get("is_bridge")}

    def links(x, y, kind):
        """The directions this cell's run continues in."""
        if kind == 'river':
            member = lambda x, y: (at(x, y) or '') in RIVER
        else:
            member = lambda x, y: (at(x, y) or '') in ROAD or (x, y) in future_bridge
        out = set()
        for d, (dx, dy) in DIRS4.items():
            if member(x + dx, y + dy):
                out.add(d)
        if kind == 'road':
            for d, (dx, dy) in DIRS8.items():
                if len(d) != 2 or not member(x + dx, y + dy):
                    continue
                # A diagonal only where the run does not turn the corner itself.
                if member(x + dx, y) or member(x, y + dy):
                    continue
                out.add(d)
        elif kind == 'river':
            for d, (dx, dy) in DIRS8.items():
                if len(d) == 2 and member(x + dx, y + dy) \
                        and not member(x + dx, y) \
                        and not member(x, y + dy):
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
                    # A mouth's art has the open sea on its outflow side and
                    # along its top, land along its foot; the _s piece is the
                    # same drawn the other way up, the sea along its foot.
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
                    # The mouth piece is drawn on a coast tile whose sea side
                    # is open water: its neighbours see it as sea.
                    cls[y][x] = 'water'
                    continue
                if len(ex) == 1:
                    o = OPP[next(iter(ex))]
                    if not inside(x + DIRS4[o][0], y + DIRS4[o][1]):
                        ex = ex | {o}          # the run flows off the map
                    elif c == 'r' and not any(
                            at(x + dx, y + dy) in ('^', 'f')
                            for d, (dx, dy) in DIRS4.items() if d != next(iter(ex))):
                        warnings.append(f"({x},{y}): river ends in open ground, "
                                        f"not at the sea, the map's edge or a "
                                        f"source in the mountains or woods")
                if c in 'RM':
                    for d, (dx, dy) in DIRS4.items():
                        n = at(x + dx, y + dy)
                        if n is None or n in RIVER or n == 'H':
                            continue
                        if BASE.get(n) != RIVER[c]:
                            warnings.append(f"({x},{y}): {RIVER[c]}-banked river "
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
    def edge_idx(t, diff):
        """(index, None) for the edge variant showing these different
        neighbours, or (None, why) when the families have none."""
        card = frozenset(d for d in diff if len(d) == 1)
        m = WATER_IDX if t == 'water' else OTHER_IDX
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
        if card in SPIT_IDX:
            return SPIT_IDX[card] - (1 if t == 'water' else 0), None
        return None, f"{t} open on {sorted(card)}; no variant"

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
            near = {d: cls[y + dy][x + dx] for d, (dx, dy) in DIRS8.items()
                    if 0 <= x + dx < W and 0 <= y + dy < H}
            full = {d for d, k in near.items() if k != t}
            # Every land edge has faded to grass, and the sea's own edge has
            # drawn the shore: forest and sand meeting the sea have kept their
            # own ground to the coast, so the shore is drawn once, not twice
            # -- unless no variant shows what is left, when the old shape has
            # stood. Rock has kept its fringe: cut square at the water, a crag
            # has read as a wall.
            diff = full if t in ('water', 'mountain') else \
                {d for d in full if near[d] != 'water'}
            idx, why = (None, None) if not diff else edge_idx(t, diff)
            if diff and idx is None and diff != full:
                diff = full
                idx, why = edge_idx(t, diff)
            if not diff:
                out_art[y][x] = PLAIN_ART[c]
                continue
            if idx is None:
                errors.append(f"({x},{y}): {why}")
                continue
            card = frozenset(d for d in diff if len(d) == 1)
            if card:
                lost = sorted(d for d in diff if len(d) == 2
                              and d[0] not in card and d[1] not in card)
                if lost:
                    (warnings if t == 'water' else notes).append(
                        f"({x},{y}): {t} edge on {sorted(card)} cannot "
                        f"show its different diagonal {lost}")
            family = f"{t}_edge"
            # A sea whose shore is all sand has drawn a sand shore.
            if t == 'water' and all(near[d] == 'desert' for d in diff) \
                    and f"water_sand_edge_{idx:02d}" in a2c:
                family = "water_sand_edge"
            name = f"{family}_{idx:02d}"
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

    if notes:
        print(f"{len(notes)} note(s): land corners no edge variant shows "
              f"(a small notch), e.g. {notes[0]}")
    if warnings:
        print(f"{len(warnings)} warning(s):")
        for w in warnings[:60]:
            print("  " + w)
        if len(warnings) > 60:
            print(f"  ... and {len(warnings) - 60} more")
        if strict:
            errors += warnings
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
            if "tile" not in fx:      # a reveal, which changes no tile
                continue
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

# Arts a scattered object may stand on: open ground, whatever its look.
OPEN_ARTS = ("grass", "grass_variant", "desert")


def place(pack, zid, path, regions_path, add=False):
    """regions.json: {"seed": N, "spacing": r, "fixed": {"<id>": [x, y]},
    "regions": [{"name", "box": [x0,y0,x1,y1], "chests": n, "armies": n}, ...]}

    A static army (a guardian holding a pass) and a "fixed" chest (a prize the
    salt never turns into something else) keep what they are and go where
    "fixed" puts them; every other chest and army is re-scattered. With add,
    every chest and army stays where it is and a region's counts are totals:
    only what the region still lacks is placed, the new ones numbered on from
    the zone's highest id. Towns, castles, signs, dwellings, events and the
    tiles an event changes are kept clear."""
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
    for d in z.get("dwellings", []):
        mark(d["x"], d["y"], 1)
    for ev in z.get("events", []):
        mark(ev["x"], ev["y"], 1)
        for fx in ev.get("effects", []):
            if "x" in fx:
                mark(fx["x"], fx["y"], 1)
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

    def inbox(box, x, y):
        return box[0] <= x <= box[2] and box[1] <= y <= box[3]

    # With add, what each region already holds counts against its total.
    # Boxes may overlap: an object in one box belongs to it, and one in
    # several goes to whichever of them is furthest short of its count; a
    # region that then has no room for its count spills the rest.
    have = [{"chests": 0, "armies": 0} for _ in spec["regions"]]
    if add:
        for kind, objs in (("chests", [c for c in z.get("chests", []) if not c.get("fixed")]),
                           ("armies", [a for a in z.get("wandering_armies", [])
                                       if not a.get("static")])):
            shared = []
            for o in objs:
                mark(o["x"], o["y"], spec.get("spacing", 2))
                hits = [i for i, reg in enumerate(spec["regions"])
                        if inbox(reg["box"], o["x"], o["y"])]
                if len(hits) == 1:
                    have[hits[0]][kind] += 1
                elif hits:
                    shared.append(hits)
            for hits in shared:
                i = max(hits, key=lambda i: (spec["regions"][i].get(kind, 0)
                                             - have[i][kind], -i))
                have[i][kind] += 1

    def free(x, y):
        return (0 <= x < W and 0 <= y < H and (x, y) not in taken
                and art[y][x] in OPEN_ARTS)

    chests, armies = [], []
    spill = {"chests": 0, "armies": 0}
    # The zone as a whole wants what its regions' counts add up to: what a
    # full region spilled before is counted where it landed, so a second run
    # adds nothing.
    left = {k: sum(r.get(k, 0) for r in spec["regions"]) - sum(h[k] for h in have)
            for k in ("chests", "armies")}
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
                if not free(x, y):
                    continue
                (chests if kind == "chests" else armies).append((x, y))
                mark(x, y, spec.get("spacing", 2))
                got += 1
            if got < n:
                if not add:
                    die(f"region {reg['name']}: room for {got} of {n} {kind}")
                print(f"  region {reg['name']}: room for {got} of {n} {kind}; "
                      f"the rest go to the zone's other regions")
                spill[kind] += n - got
    # With add, what a full region could not take goes anywhere in the
    # zone's regions there is still room.
    if any(spill.values()):
        cells = sorted({(x, y) for reg in spec["regions"]
                        for y in range(reg["box"][1], reg["box"][3] + 1)
                        for x in range(reg["box"][0], reg["box"][2] + 1)})
        rng.shuffle(cells)
        for kind in ("chests", "armies"):
            for x, y in cells:
                if spill[kind] == 0:
                    break
                if free(x, y):
                    (chests if kind == "chests" else armies).append((x, y))
                    mark(x, y, spec.get("spacing", 2))
                    spill[kind] -= 1
            if spill[kind]:
                die(f"no room in any region for {spill[kind]} more {kind}")

    def next_num(objs, prefix):
        nums = [int(o["id"][len(prefix):]) for o in objs
                if o.get("id", "").startswith(prefix) and o["id"][len(prefix):].isdigit()]
        return max(nums) + 1 if nums else 0

    if add:
        c0 = max(1, next_num(z.get("chests", []), "chest_"))
        a0 = next_num(z.get("wandering_armies", []), "wandering_army_")
        new_chests = [{"id": f"chest_{c0 + i}", "x": x, "y": y}
                      for i, (x, y) in enumerate(chests)]
        new_armies = [{"x": x, "y": y, "id": f"wandering_army_{a0 + i:03d}"}
                      for i, (x, y) in enumerate(armies)]
        return g, z, new_chests, new_armies, True
    new_chests = [{"id": f"chest_{i + 1}", "x": x, "y": y}
                  for i, (x, y) in enumerate(chests)] + fixed_chests
    # The guardians first, then the scattered armies.
    new_armies = fixed + [{"x": x, "y": y, "id": f"wandering_army_{i:03d}"}
                          for i, (x, y) in enumerate(armies)]
    return g, z, new_chests, new_armies, False


# ---- writing game.json -----------------------------------------------------
#
# game.json is cJSON's formatted print, hand-edited in places, so it is never
# re-printed whole: only the zone arrays `place` owns are touched, in the text.

def cj_str(v):
    return json.dumps(v, ensure_ascii=False)


def cj_obj(o, depth):
    """One flat object as cJSON prints it inside an array at this depth."""
    pad = "\t" * (depth + 1)
    body = ",\n".join(f"{pad}{cj_str(k)}:\t{cj_str(v) if isinstance(v, str) else json.dumps(v)}"
                      for k, v in o.items())
    return "{\n" + body + "\n" + "\t" * depth + "}"


def zone_array_span(text, zid, key):
    """(start, end) of the zone's `key` array value, '[' to ']' inclusive."""
    zi = text.find(f'\n\t\t\t"id":\t"{zid}",')
    if zi < 0:
        die(f"game.json: no zone '{zid}' in the expected layout")
    nxt = text.find('\n\t\t}, {\n', zi)
    nxt = nxt if nxt >= 0 else len(text)
    ki = text.find(f'\n\t\t\t"{key}":\t', zi, nxt)
    if ki < 0:
        die(f"game.json: zone '{zid}' has no {key} array")
    start = text.index("[", ki)
    if text.startswith("[]", start):
        return start, start + 1
    end = text.index("\n\t\t\t\t}]", start) + len("\n\t\t\t\t}]") - 1
    return start, end


def write_zone(pack, zid, chests, armies, add):
    path = os.path.join(pack, "game.json")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    for key, objs in (("wandering_armies", armies), ("chests", chests)):
        s, e = zone_array_span(text, zid, key)
        items = ", ".join(cj_obj(o, 4) for o in objs)
        if add:
            if not objs:
                continue
            if text[s:e + 1] == "[]":
                text = text[:s] + "[" + items + "]" + text[e + 1:]
            else:
                text = text[:e] + ", " + items + text[e:]
        else:
            text = text[:s] + "[" + items + "]" + text[e + 1:]
    json.loads(text)                       # still valid JSON
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    flags = {x for x in sys.argv[1:] if x.startswith("--")}
    if not a or "--help" in flags or a[0] in ("-h",) or a[0] not in ("build", "check", "place"):
        print(__doc__)
        return
    if a[0] == "build" and len(a) == 5:
        build(a[1], a[2], a[3], a[4], strict="--strict" in flags)
    elif a[0] == "check" and len(a) == 4:
        check(a[1], a[2], a[3])
    elif a[0] == "place" and len(a) == 5:
        g, z, c, r, add = place(a[1], a[2], a[3], a[4], add="--add" in flags)
        verb = "added" if add else "placed"
        if "--write" in flags:
            write_zone(a[1], a[2], c, r, add)
            print(f"{verb} {len(c)} chests and {len(r)} armies; wrote game.json")
        else:
            print(f"{verb} {len(c)} chests and {len(r)} armies (not written)")
            print("  " + json.dumps(c))
            print("  " + json.dumps(r))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
