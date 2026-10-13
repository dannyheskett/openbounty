#!/usr/bin/env python3
"""Lay the one shared grass band on every grass-facing side of a set of 96 px tiles, so any two tiles
join and the join carries real grass: the outer w px of the side are the band exactly; between w and K
px in, a least-difference seam (dynamic programming, through base-colour pixels wherever it can) hands
over to the tile's own pixels, so no clump is sliced where a path exists.
  grassband.py BAND_DIR TILES_DIR [--only NAME ...]
BAND_DIR holds band_c.png (2K x 2K), band_v.png ((96-2K) x 2K) and band_h.png (2K x (96-2K)) from landcut.
Sides facing grass: every side of grass and grass_pN; the named sides of <t>_edge_<sides>; the corner of
<t>_inner_<corner>. Other tiles are left alone."""
import sys, os, re, argparse
import numpy as np
from PIL import Image
T, w, K = 96, 4, 10

def seam(diff, along):
    """diff: cost per pixel in the overlap zone (rows or cols between w and K); the seam is a row index per
    column (along=1) or column index per row (along=0) that minimises cost + 1 per step of change"""
    D = diff if along == 1 else diff.T                     # D[depth, position]
    n, m = D.shape; cost = D.copy(); back = np.zeros((n, m), int)
    for j in range(1, m):
        for i in range(n):
            best = i; c = cost[i, j - 1]
            for k in (i - 1, i + 1):
                if 0 <= k < n and cost[k, j - 1] + 1 < c: best, c = k, cost[k, j - 1] + 1
            cost[i, j] += c; back[i, j] = best
    i = int(np.argmin(cost[:, m - 1])); path = [i]
    for j in range(m - 1, 0, -1): i = back[i, j]; path.append(i)
    return np.array(path[::-1])

def lay_side(t, band, side):
    """t: 96x96x3 tile; band: 2K x 96 (top/bottom) or 96 x 2K (left/right) centred on the tile line"""
    if side in ("top", "bottom"):
        half = band[K:] if side == "top" else band[:K]        # the tile's own K px next to the line
        if side == "bottom": half = half[::-1]
        reg = t[:K] if side == "top" else t[::-1][:K]
        zone = slice(w, K)
        diff = np.abs(half[zone].astype(int) - reg[zone].astype(int)).sum(2)   # (K-w) x 96
        path = seam(diff, 1)                                   # depth per column, within the zone
        out = reg.copy(); out[:w] = half[:w]
        for x in range(T):
            d = w + path[x]; out[w:d + 1, x] = half[w:d + 1, x]   # band pixels down to the seam, tile pixels below
        if side == "top": t[:K] = out
        else: t[T - K:] = out[::-1]
    else:
        tt = t.transpose(1, 0, 2).copy(); bb = band.transpose(1, 0, 2)
        lay_side(tt, bb, "top" if side == "left" else "bottom"); t[:] = tt.transpose(1, 0, 2)
    return t

def lay_corner(t, bc, corner):
    """the tile's corner square takes the shared corner's quadrant, outer w px exact, the rest by the
    least-difference choice per pixel of band or tile (clump pixels of the band win over base)"""
    q = {"nw": bc[K:, K:], "ne": bc[K:, :K], "sw": bc[:K, K:], "se": bc[:K, :K]}[corner]
    ys = slice(0, K) if corner[0] == "n" else slice(T - K, T); xs = slice(0, K) if corner[1] == "w" else slice(T - K, T)
    reg = t[ys, xs].copy(); qq = q.copy()
    yy = np.arange(K); xx = np.arange(K)
    dy = yy if corner[0] == "n" else K - 1 - yy; dx = xx if corner[1] == "w" else K - 1 - xx
    inner = (dy[:, None] >= w) & (dx[None, :] >= w)
    reg[~inner] = qq[~inner]
    t[ys, xs] = reg
    return t

def faces(name):
    if name == "grass" or re.match(r"grass_p\d+$", name): return "nesw", ""
    if re.match(r"fields_[a-z]+(_v\d+)?$", name): return "nesw", ""      # a crop plot on the meadow: grass all round
    m = re.match(r"[a-z_]+?_edge_([nesw]+)$", name)
    if m: return m.group(1), ""
    m = re.match(r"[a-z_]+?_inner_([nesw]{2})$", name)
    if m: return "", m.group(1)
    return None, None

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("band_dir"); ap.add_argument("tiles_dir"); ap.add_argument("--only", nargs="*")
    ap.add_argument("--terrain", help="lay this terrain's own band (<terrain>_band_*.png) on the sides of its tiles that face the same terrain, instead of the grass band on grass-facing sides")
    a = ap.parse_args()
    pre = f"{a.terrain}_band" if a.terrain else "band"
    bc = np.array(Image.open(os.path.join(a.band_dir, f"{pre}_c.png")).convert("RGB"))
    bv = np.array(Image.open(os.path.join(a.band_dir, f"{pre}_v.png")).convert("RGB"))
    bh = np.array(Image.open(os.path.join(a.band_dir, f"{pre}_h.png")).convert("RGB"))
    # full-length bands along a tile line: corner quadrant, band, next corner's quadrant
    BH = np.concatenate([bc[:, K:], bh, bc[:, :K]], axis=1)      # 2K x 96, x runs along the line
    BV = np.concatenate([bc[K:, :], bv, bc[:K, :]], axis=0)      # 96 x 2K
    n = 0
    for f in sorted(os.listdir(a.tiles_dir)):
        if not f.endswith(".png"): continue
        name = f[:-4]
        if a.only and name not in a.only: continue
        sides, corner = faces(name)
        if a.terrain:
            plain = name == a.terrain or re.fullmatch(re.escape(a.terrain) + r"_v\d+", name)
            if not (plain or name.startswith(a.terrain + "_edge_") or name.startswith(a.terrain + "_inner_")): continue
            # only a side that is wholly this terrain takes its band: a shore crossing a side leaves it half grass
            opp = {"n": "s", "s": "n", "e": "w", "w": "e"}
            if plain: sides, corner = "nesw", ""
            elif sides is None: continue
            elif corner: sides, corner = "".join(c for c in "nesw" if c not in corner), ""   # an inner corner: the two sides away from the open corner
            elif len(sides) == 1: sides, corner = opp[sides], ""                           # a straight edge: the side opposite the open one
            else: continue                                                                 # an outer corner: no side is wholly this terrain
        elif sides is None: continue
        im = Image.open(os.path.join(a.tiles_dir, f)).convert("RGBA"); t = np.array(im)[..., :3].copy(); al = np.array(im)[..., 3]
        for sd, nm in (("n", "top"), ("s", "bottom"), ("w", "left"), ("e", "right")):
            if sd in sides: lay_side(t, BH if sd in "ns" else BV, nm)
        for c in ("nw", "ne", "sw", "se"):
            if (c[0] in sides and c[1] in sides) or corner == c: lay_corner(t, bc, c)
        out = np.dstack([t, al]); Image.fromarray(out.astype(np.uint8)).save(os.path.join(a.tiles_dir, f)); n += 1
    print("grassband:", n, "tiles finished")

if __name__ == "__main__": main()
