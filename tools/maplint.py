#!/usr/bin/env python3
"""Lint a zone's terrain shapes against what the tile art can draw well.

    python3 tools/maplint.py <pack-dir> <zone-id> [--update-allow] [--update-reach] [--all]

Reads the zone's hand-drawn source (art/maps/<zone>.txt) and its built .dat,
and fails on shapes that render badly with the pack's edge pieces, for the
masses: forest (f), mountain (^) and farmland (p, w).

  step      a one-cell stair step: two corner cells of a mass, turned the same
            way, diagonally adjacent -- a staircase of square corners
  strand    a cell of a mass that belongs to no 2x2 block of it: a one-cell
            strand, spur or lone cell
  notch     a cell whose edge piece cannot show a different diagonal
            neighbour (the corner is cut square)
  pair      two different masses side by side: the edge pieces only fade to
            grass, so the seam between them is wrong
  coast     a mass beside the sea, outside the zone's intended coasts
            (farmland never: fields have no shore)
  edge      a mass within two cells of the world's edge, outside the zone's
            intended edges (farmland never: a field ends against the black)
  dat       a finding in the built .dat's terrain that the source does not
            have

The intended coasts and edges are hand-kept in
art/maps/<zone>_lint_intended.json, {"coast": [...], "edge": [...]}, each
entry {"box": [x0, y0, x1, y1], "reason": "..."}: a wooded or rocky shore, a
range running off the map, drawn that way on purpose.

and on any change in what the hero can reach, against art/maps/<zone>_reach.json
(the four columns `mapbuild.py check` prints).

The findings still to clear are recorded in art/maps/<zone>_lint_allow.json,
the baseline: only findings not listed fail, so the count can only fall, and
the finished maps carry none. --update-allow writes every current finding
into that list (to record the baseline, and again as findings are cleared). --update-reach records the
current reach as the baseline. --all prints every finding, allowed or not.

Exit 0 clean, 1 on any new finding or a reach change.
"""
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASS = {'f': 'forest', '^': 'mountain', 'p': 'field', 'w': 'field'}
D4 = {'n': (0, -1), 'e': (1, 0), 's': (0, 1), 'w': (-1, 0)}
D8 = dict(D4, ne=(1, -1), se=(1, 1), sw=(-1, 1), nw=(-1, -1))


def rows_of(path):
    with open(path, encoding="latin-1") as f:
        return [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]


def inside(boxes, x, y):
    return any(b[0] <= x <= b[2] and b[1] <= y <= b[3] for b in boxes)


def findings(rows, intended=None):
    H, W = len(rows), len(rows[0])
    coast_ok = [e["box"] for e in (intended or {}).get("coast", [])]
    edge_ok = [e["box"] for e in (intended or {}).get("edge", [])]

    def at(x, y):
        return rows[y][x] if 0 <= x < W and 0 <= y < H else None

    def kind(c):
        if c is None:
            return None
        return 'field-' + c if c in 'pw' else MASS.get(c)

    out = []
    corners = {}
    for y in range(H):
        for x in range(W):
            c = rows[y][x]
            k = kind(c)
            if not k:
                continue
            same = lambda dx, dy: kind(at(x + dx, y + dy)) == k or at(x + dx, y + dy) is None
            open4 = {d for d, (dx, dy) in D4.items() if not same(dx, dy)}
            # strand: in no 2x2 block of the same mass
            if not any(all(kind(at(x + ox + i, y + oy + j)) == k for i in (0, 1) for j in (0, 1))
                       for ox in (-1, 0) for oy in (-1, 0)):
                out.append(("strand", x, y, f"{k} cell in no 2x2 block of {k}"))
            # notch: a different diagonal the cardinal edge cannot show
            if open4:
                for d, (dx, dy) in D8.items():
                    if len(d) == 2 and not same(dx, dy) and d[0] not in open4 and d[1] not in open4:
                        out.append(("notch", x, y, f"{k} edge on {sorted(open4)} hides the {d} corner"))
            # corners, for steps
            for pair in (('n', 'e'), ('n', 'w'), ('s', 'e'), ('s', 'w')):
                if open4 == set(pair):
                    corners[(x, y)] = (k, pair)
            # pair: two different masses side by side (east and south, so once each)
            for d in ('e', 's'):
                dx, dy = D4[d]
                k2 = kind(at(x + dx, y + dy))
                if k2 and k2 != k:
                    out.append(("pair", x, y, f"{k} beside {k2} to the {d}"))
            # coast: a mass beside the sea, outside the intended coasts
            if any(at(x + dx, y + dy) == '~' for dx, dy in D8.values()) and \
                    (k.startswith('field') or not inside(coast_ok, x, y)):
                out.append(("coast", x, y, f"{k} beside the sea"))
            # edge: a mass within two cells of the world's edge, outside the
            # intended edges
            if min(x, y, W - 1 - x, H - 1 - y) < 2 and \
                    (k.startswith('field') or not inside(edge_ok, x, y)):
                out.append(("edge", x, y, f"{k} within two cells of the world's edge"))
    for (x, y), (k, pair) in corners.items():
        # the next step of a staircase: the corner cell diagonally beyond,
        # along the outline, turned the same way
        ddx = 1 if 'e' in pair else -1
        ddy = 1 if 'n' in pair else -1
        for nx, ny in ((x + ddx, y + ddy), (x - ddx, y - ddy)):
            if corners.get((nx, ny)) == (k, pair) and (nx, ny) > (x, y):
                out.append(("step", x, y, f"{k} stair step with ({nx},{ny})"))
    return out


def dat_rows(pack, g, dat):
    """The built .dat as source characters for the masses: forest f,
    mountain ^, sea ~, farmland p / w by its art, everything else grass."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import mapbuild
    _, c2e = mapbuild.art_codes(g)
    raw = open(dat, "rb").read().decode("latin-1")
    out = []
    for line in raw.split("\n"):
        line = line.rstrip("\r")
        if not line or line.startswith("#"):
            continue
        r = []
        for c in line:
            e = c2e.get(c, {})
            art, ter = e.get("art", ""), e.get("terrain", "grass")
            r.append('p' if art.startswith("fields_plough") else 'w' if art.startswith("fields_wheat")
                     else {'forest': 'f', 'mountain': '^', 'water': '~'}.get(ter, '.'))
        out.append(''.join(r))
    w = max(len(r) for r in out)
    return [r.ljust(w, '.') for r in out]


def reach_now(pack, zid, dat):
    res = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mapbuild.py"), "check",
                          pack, zid, dat], capture_output=True, text=True)
    out = {}
    for line in res.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 5 and all(p in ("yes", "NO") for p in parts[-4:]):
            out[" ".join(parts[:-4])] = parts[-4:]
    return out


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    flags = {x for x in sys.argv[1:] if x.startswith("--")}
    if len(a) != 2 or "--help" in flags:
        print(__doc__)
        return 2
    pack, zid = a
    g = json.load(open(os.path.join(pack, "game.json")))
    z = next(z for z in g["zones"] if z["id"] == zid)
    src = os.path.join(ROOT, "art", "maps", f"{zid}.txt")
    dat = os.path.join(pack, z["map"])
    allow_p = os.path.join(ROOT, "art", "maps", f"{zid}_lint_allow.json")
    reach_p = os.path.join(ROOT, "art", "maps", f"{zid}_reach.json")

    int_p = os.path.join(ROOT, "art", "maps", f"{zid}_lint_intended.json")
    intended = json.load(open(int_p)) if os.path.exists(int_p) else {}
    found = findings(rows_of(src), intended)
    src_keys = {(r, x, y) for r, x, y, _ in found}
    found += [("dat", x, y, f"the built .dat has a {r} the source does not ({w})")
              for r, x, y, w in findings(dat_rows(pack, g, dat), intended)
              if (r, x, y) not in src_keys]
    allow = json.load(open(allow_p)) if os.path.exists(allow_p) else []
    allowed = {(e["rule"], e["x"], e["y"]) for e in allow}
    new = [f for f in found if (f[0], f[1], f[2]) not in allowed]
    gone = [e for e in allow if (e["rule"], e["x"], e["y"]) not in {(f[0], f[1], f[2]) for f in found}]

    if "--update-allow" in flags:
        old = {(e["rule"], e["x"], e["y"]): e.get("reason", "") for e in allow}
        allow = [{"rule": r, "x": x, "y": y, "what": w,
                  "reason": old.get((r, x, y), "baseline: still to clear (#63 plan, step 1)")}
                 for r, x, y, w in found]
        with open(allow_p, "w") as f:
            json.dump(allow, f, indent=1)
            f.write("\n")
        print(f"{zid}: recorded {len(allow)} findings still to clear in {os.path.relpath(allow_p, ROOT)}")
        new, gone = [], []

    reach = reach_now(pack, zid, dat)
    if "--update-reach" in flags or not os.path.exists(reach_p):
        with open(reach_p, "w") as f:
            json.dump(reach, f, indent=1, sort_keys=True)
            f.write("\n")
        print(f"{zid}: recorded the reach baseline ({len(reach)} places)")
    base = json.load(open(reach_p))
    reach_bad = [f"{k}: {base.get(k)} -> {reach.get(k)}" for k in sorted(set(base) | set(reach))
                 if base.get(k) != reach.get(k)]

    by = {}
    for r, *_ in found:
        by[r] = by.get(r, 0) + 1
    print(f"{zid}: {len(found)} findings ({', '.join(f'{k} {v}' for k, v in sorted(by.items())) or 'none'}), "
          f"{len(found) - len(new)} still to clear, {len(new)} new; reach "
          f"{'unchanged' if not reach_bad else 'CHANGED'}")
    shown = found if "--all" in flags else new
    for r, x, y, w in shown[:80]:
        print(f"  {r:6s} ({x},{y}) {w}")
    if len(shown) > 80:
        print(f"  ... and {len(shown) - 80} more")
    for e in gone[:20]:
        print(f"  fixed  ({e['x']},{e['y']}) {e['rule']}: drop it from the allow list")
    for b in reach_bad:
        print(f"  REACH  {b}")
    return 1 if new or reach_bad else 0


if __name__ == "__main__":
    sys.exit(main())
