#!/usr/bin/env python3
"""Draw a ground's 12 edge and corner tiles from a render's own materials when no render holds straight runs:
the inside (sand) from the plain tile, the outside from the meadow tile, and between them the render's own rim,
colour by depth (measured in the render) with its own speckle. The boundary is straight (edges) or a quarter
circle (corners), wobbled a little inside the tile and exact at the tile sides, so every join matches.
  sandedge.py RENDER PLAIN.png MEADOW.png OUT_DIR --name desert [--seed N] [--wobble 3]"""
import argparse, os, random
import numpy as np
from PIL import Image
from numpy.lib.stride_tricks import sliding_window_view as sw
T = 96
SHAPES = {"edge_n": (0, 0, 1, 1), "edge_s": (1, 1, 0, 0), "edge_w": (0, 1, 0, 1), "edge_e": (1, 0, 1, 0),
          "edge_nw": (0, 0, 0, 1), "edge_ne": (0, 0, 1, 0), "edge_sw": (0, 1, 0, 0), "edge_es": (1, 0, 0, 0),
          "inner_es": (1, 1, 1, 0), "inner_sw": (1, 1, 0, 1), "inner_ne": (1, 0, 1, 1), "inner_nw": (0, 1, 1, 1)}

def lum(a): return 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
def green(a): return (a[..., 1] > a[..., 0]) & (a[..., 1] > a[..., 2])

def profile(R, pale):
    """colours seen at each signed depth from the sand/grass boundary in the render: >0 towards the sand"""
    g = green(R); s = ~g & (lum(R) >= 110) & (lum(R) < 226)      # sand, never the pale frame
    # distance to the nearest grass pixel (inside) and to the nearest sand pixel (outside), by brute iteration up to 14 px
    H, W = g.shape; ys, xs = np.mgrid[0:H, 0:W]
    def dist_to(mask):
        d = np.full((H, W), 99.0)
        for r in range(1, 15):
            ring = sw(np.pad(mask, r, mode="constant"), (2 * r + 1, 2 * r + 1)).max((2, 3))
            d[(ring > 0) & (d == 99)] = r
        return d
    dg, ds = dist_to(g), dist_to(s)
    depth = np.where(g, -ds, dg)           # grass pixels: negative, by distance to sand; others positive by distance to grass
    out = {}
    for k in range(-12, 13):
        m = (depth == k) if k != 0 else np.zeros_like(g)
        if k > 0: m &= s                                   # rim pixels are sand-class, never frame-white
        if k < 0: m &= g
        if m.sum() < 30: continue
        cols, cnt = np.unique(R[m], axis=0, return_counts=True)
        out[k] = (cols, cnt / cnt.sum())
    return out

def rim_strip(R, fringe, rim):
    """the render's own rim: for every column of its longest straight-ish top run, the pixels from `fringe` px
    above the sand's first row to `rim` px below it; (fringe+rim) x L, real structure along the boundary"""
    g = green(R); sd = ~g & (lum(R) >= 110) & (lum(R) < 226); H, W = g.shape
    b = np.full(W, -1)
    for x in range(W):
        col = np.nonzero(sd[:, x])[0]
        if len(col) and 20 <= col[0] <= H // 2 and g[max(col[0] - 4, 0):col[0], x].any(): b[x] = col[0]
    best = (0, 0, 0); st = 0
    for x in range(1, W + 1):
        if x == W or b[x] < 0 or abs(b[x] - b[x - 1]) > 2:
            if x - st > best[0]: best = (x - st, st, x)
            st = x
    L, x0, x1 = best
    strip = np.zeros((fringe + rim, L, 3), int)
    for i, x in enumerate(range(x0, x1)):
        y0 = b[x] - fringe; seg = R[y0:y0 + fringe + rim, x]; strip[:, i] = seg
    return strip

def signed(shape, wob):
    tl, tr, bl, br = SHAPES[shape]; yy, xx = np.mgrid[0:T, 0:T] + 0.5; s = sum(SHAPES[shape])
    if s == 2:
        if (tl, tr) == (0, 0): d = yy - 48 - wob[0][xx.astype(int)]
        elif (bl, br) == (0, 0): d = 48 - yy + wob[0][xx.astype(int)]
        elif (tl, bl) == (0, 0): d = xx - 48 - wob[1][yy.astype(int)]
        else: d = 48 - xx + wob[1][yy.astype(int)]
        return d
    corner = {(0, 0, 0, 1): (T, T), (0, 0, 1, 0): (T, 0), (0, 1, 0, 0): (0, T), (1, 0, 0, 0): (0, 0),
              (1, 1, 1, 0): (T, T), (1, 1, 0, 1): (T, 0), (1, 0, 1, 1): (0, T), (0, 1, 1, 1): (0, 0)}[SHAPES[shape]]
    ang = np.arctan2(yy - corner[0], xx - corner[1]); k = ((ang + np.pi) / (2 * np.pi) * (T - 1)).astype(int)
    d = 48 - np.hypot(yy - corner[0], xx - corner[1]) + wob[2][k]
    return d if s == 1 else -d

def along(shape):
    """position along the boundary, px, for every pixel: x for top/bottom edges, y for left/right, arc length for corners"""
    tl, tr, bl, br = SHAPES[shape]; yy, xx = np.mgrid[0:T, 0:T]; s = sum(SHAPES[shape])
    if s == 2: return xx if tl == tr else yy
    corner = {(0, 0, 0, 1): (T, T), (0, 0, 1, 0): (T, 0), (0, 1, 0, 0): (0, T), (1, 0, 0, 0): (0, 0),
              (1, 1, 1, 0): (T, T), (1, 1, 0, 1): (T, 0), (1, 0, 1, 1): (0, T), (0, 1, 1, 1): (0, 0)}[SHAPES[shape]]
    ang = np.arctan2(yy + 0.5 - corner[0], xx + 0.5 - corner[1]); return ((ang + np.pi) * 48).astype(int)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("render"); ap.add_argument("plain"); ap.add_argument("meadow"); ap.add_argument("out"); ap.add_argument("--name", required=True)
    ap.add_argument("--plain-from", action="store_true", help="also cut the three plain tiles from the render: the calmest distinct all-sand windows"); ap.add_argument("--base", default=None)
    ap.add_argument("--seed", type=int, default=7); ap.add_argument("--wobble", type=float, default=5.0); ap.add_argument("--rim", type=int, default=8, help="rim depth into the sand, px"); ap.add_argument("--fringe", type=int, default=3, help="grass-side fringe depth, px")
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    R = np.array(Image.open(a.render).convert("RGB")).astype(int); P = np.array(Image.open(a.plain).convert("RGB")).astype(int); M = np.array(Image.open(a.meadow).convert("RGB")).astype(int)
    strip = rim_strip(R, a.fringe, a.rim); print("rim strip:", strip.shape[1], "px long")
    rng = np.random.default_rng(a.seed)
    if a.plain_from:
        gR = green(R); sR = ~gR & (lum(R) >= 110) & (lum(R) < 226); base = np.array([int(v) for v in a.base.split(",")]) if a.base else None
        cands = []
        for y in range(0, R.shape[0] - T + 1, 2):
            for x in range(0, R.shape[1] - T + 1, 2):
                if not sR[y:y + T, x:x + T].all(): continue
                reg = R[y:y + T, x:x + T]; tex = float((np.abs(reg - reg.reshape(-1, 3).mean(0)).sum(2) > 24).mean())
                cands.append((tex, y, x))
        cands.sort(); picked = []
        for tex, y, x in cands:
            if all(abs(y - py) >= 40 or abs(x - px) >= 40 for py, px in picked): picked.append((y, x))
            if len(picked) == 3: break
        for k, (y, x) in enumerate(picked):
            Image.fromarray(R[y:y + T, x:x + T].astype(np.uint8)).save(os.path.join(a.out, a.name + ("" if k == 0 else f"_v{k}") + ".png"))
        print("plain windows:", picked, "of", len(cands))
        P = R[picked[0][0]:picked[0][0] + T, picked[0][1]:picked[0][1] + T].astype(int)
    def wobble():
        # a smooth low-frequency wobble along the boundary, zero within 10 px of either end so the sides stay exact
        n = T; f = np.zeros(n)
        for k, amp in ((13, 1.0), (4, 0.4)):
            r = rng.normal(0, 1, n); r = np.convolve(np.pad(r, k, mode="reflect"), np.ones(2 * k + 1) / (2 * k + 1), "valid")[:n]; f += r / (np.abs(r).max() + 1e-9) * amp
        f = f / (np.abs(f).max() + 1e-9) * a.wobble; t = np.arange(n); fade = np.clip(np.minimum(t, n - 1 - t) / 12, 0, 1)
        return f * fade
    for shape in SHAPES:
        wob = (wobble(), wobble(), wobble()); d = signed(shape, wob)
        tile = M.copy()
        sand = d > a.rim; tile[sand] = P[sand]
        band = (d <= a.rim) & (d > -a.fringe); pos = along(shape); off = int(rng.integers(0, strip.shape[1]))
        ys, xs = np.nonzero(band)
        for y, x in zip(ys, xs):
            row = int(round(a.rim - d[y, x])); row = max(0, min(strip.shape[0] - 1, row))      # 0 = the grass side of the strip
            tile[y, x] = strip[row, (pos[y, x] + off) % strip.shape[1]]
        Image.fromarray(tile.astype(np.uint8)).save(os.path.join(a.out, f"{a.name}_{shape}.png"))
    print("drawn:", len(SHAPES), "tiles")

if __name__ == "__main__": main()
