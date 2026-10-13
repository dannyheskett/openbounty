#!/usr/bin/env python3
"""Scatter a few of a render's own ripple strokes onto the flat sea of a set of 96 px water tiles.
  ripples.py RENDER TILES_DIR [--per N] [--seed S] [--out DIR]
A stroke is a thin clump of water pixels lighter than their surroundings in RENDER (kept whole, its own
pixels, recoloured onto the tiles' sea tones). Each tile gets --per strokes (less on shore tiles), on
water only, clear of sand and of the 4 px shared border, so the joins are untouched."""
import argparse, os, random, re, sys
import numpy as np
from PIL import Image
from numpy.lib.stride_tricks import sliding_window_view

def lum(a): return 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
def water(a): return (a[..., 2] > a[..., 1] + 8) & (a[..., 2] > a[..., 0] + 25) & (lum(a) >= 110)

def strokes(a, lo=8, hi=90):
    m = water(a); L = lum(a)
    k = 15; loc = sliding_window_view(np.pad(L, k // 2, mode="edge"), (k, k)).mean((2, 3))
    hi_ = m & (L - loc > 10)
    H, W = hi_.shape; lab = np.zeros((H, W), int); out = []; n = 0
    for y0 in range(H):
        for x0 in range(W):
            if hi_[y0, x0] and lab[y0, x0] == 0:
                n += 1; st = [(y0, x0)]; lab[y0, x0] = n; pts = []
                while st:
                    y, x = st.pop(); pts.append((y, x))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = y + dy, x + dx
                            if 0 <= ny < H and 0 <= nx < W and hi_[ny, nx] and lab[ny, nx] == 0: lab[ny, nx] = n; st.append((ny, nx))
                ys = [p[0] for p in pts]; xs = [p[1] for p in pts]
                y1, y2, x1, x2 = min(ys), max(ys) + 1, min(xs), max(xs) + 1
                if not (lo <= len(pts) <= hi) or (y2 - y1) > 6 or (x2 - x1) < 6 or (x2 - x1) > 28: continue   # thin, mostly horizontal
                if y1 == 0 or x1 == 0 or y2 == H or x2 == W: continue
                msk = np.zeros((y2 - y1, x2 - x1), bool)
                for y, x in pts: msk[y - y1, x - x1] = True
                out.append((a[y1:y2, x1:x2].copy(), msk))
    return out

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("render"); ap.add_argument("tiles_dir"); ap.add_argument("--per", type=int, default=3)
    ap.add_argument("--seed", type=int, default=5); ap.add_argument("--out")
    a = ap.parse_args(); out = a.out or a.tiles_dir; os.makedirs(out, exist_ok=True)
    R = np.array(Image.open(a.render).convert("RGB")).astype(int)
    S = strokes(R); print("strokes:", len(S))
    if not S: sys.exit("ripples: no strokes found")
    rng = random.Random(a.seed); n = 0
    for f in sorted(os.listdir(a.tiles_dir)):
        if not (f.endswith(".png") and re.match(r"water(_|\.)", f)): continue
        im = Image.open(os.path.join(a.tiles_dir, f)).convert("RGBA"); t = np.array(im).astype(int); rgb = t[..., :3]
        wm = water(rgb); sea = np.unique(rgb[wm].reshape(-1, 3), axis=0, return_counts=True); base = sea[0][sea[1].argmax()]
        light = base + np.array([18, 18, 14])                                     # one lighter tone of the sea
        k = 9; safe = sliding_window_view(np.pad(wm, k // 2, mode="constant"), (k, k)).all((2, 3))   # water all round
        safe[:4, :] = safe[-4:, :] = safe[:, :4] = safe[:, -4:] = False
        want = a.per if re.match(r"water(_v\d+)?\.png", f) else max(1, a.per // 2)
        placed = []
        for _ in range(200):
            if len(placed) >= want: break
            pic, msk = rng.choice(S); h, w = msk.shape
            y, x = rng.randrange(0, 96 - h), rng.randrange(0, 96 - w)
            if not safe[y:y + h, x:x + w][msk].all(): continue
            if any(abs(y - py) < 14 and abs(x - px) < 24 for py, px in placed): continue
            reg = rgb[y:y + h, x:x + w]; reg[msk] = light; placed.append((y, x))
        t[..., :3] = rgb; Image.fromarray(t.astype(np.uint8)).save(os.path.join(out, f)); n += 1
    print("ripples:", n, "tiles")

if __name__ == "__main__": main()
