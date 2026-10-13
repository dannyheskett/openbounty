#!/usr/bin/env python3
"""One whole crop plot per 96 px tile: each plot of a render (a connected patch of the crop, kept whole with
its own pixels, ear tops and cast shadow) set on a window of the meadow, as <name>.png, <name>_v1.png ...
  plots.py RENDER MEADOW OUT_DIR --name fields_wheat [--max 84] [--seed S]"""
import argparse, os, random
import numpy as np
from PIL import Image
from numpy.lib.stride_tricks import sliding_window_view as sw

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("render"); ap.add_argument("meadow"); ap.add_argument("out"); ap.add_argument("--name", required=True)
    ap.add_argument("--max", type=int, default=84, help="largest plot side that fits with a margin")
    ap.add_argument("--min", type=int, default=40); ap.add_argument("--seed", type=int, default=3)
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    R = np.array(Image.open(a.render).convert("RGB")).astype(int); M = np.array(Image.open(a.meadow).convert("RGB")).astype(int)
    r, g, b = R[..., 0], R[..., 1], R[..., 2]
    crop = (r > g) & (r - b > 20)                                   # ears and the brown row gaps
    green = (g > r) & (g > b)
    ink = ~green                                                    # crop, its shadow, anything not grass
    # plots: connected crop patches, closed over 3 px so the row gaps join
    p = np.pad(crop, 1, mode="constant"); d = sw(p, (3, 3)).max((2, 3)); closed = sw(np.pad(d, 1, mode="constant"), (3, 3)).min((2, 3))
    H, W = crop.shape; lab = np.zeros((H, W), int); n = 0; plots = []
    for y0 in range(H):
        for x0 in range(W):
            if closed[y0, x0] and lab[y0, x0] == 0:
                n += 1; st = [(y0, x0)]; lab[y0, x0] = n; pts = []
                while st:
                    y, x = st.pop(); pts.append((y, x))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = y + dy, x + dx
                            if 0 <= ny < H and 0 <= nx < W and closed[ny, nx] and lab[ny, nx] == 0: lab[ny, nx] = n; st.append((ny, nx))
                ys = [q[0] for q in pts]; xs = [q[1] for q in pts]
                y1, y2, x1, x2 = min(ys), max(ys) + 1, min(xs), max(xs) + 1
                if y1 < 3 or x1 < 3 or y2 > H - 4 or x2 > W - 4: continue              # cut by the render's edge
                y1, x1, y2, x2 = y1 - 3, x1 - 3, y2 + 4, x2 + 4                          # room for ear tops and the shadow
                if not (a.min <= y2 - y1 <= a.max and a.min <= x2 - x1 <= a.max): continue
                reg = lab[y1:y2, x1:x2] == n
                near = sw(np.pad(reg, 3, mode="constant"), (7, 7)).max((2, 3))           # within 3 px of the plot
                msk = near & ink[y1:y2, x1:x2]
                plots.append((R[y1:y2, x1:x2].copy(), msk))
    print("plots kept:", len(plots), [m.shape for _, m in plots])
    rng = random.Random(a.seed)
    for k, (pic, msk) in enumerate(plots):
        my, mx = rng.randrange(0, M.shape[0] - 96 + 1), rng.randrange(0, M.shape[1] - 96 + 1)
        t = M[my:my + 96, mx:mx + 96].copy(); h, w = msk.shape
        y, x = (96 - h) // 2, (96 - w) // 2
        reg = t[y:y + h, x:x + w]; reg[msk] = pic[msk]
        Image.fromarray(t.astype(np.uint8)).save(os.path.join(a.out, a.name + (f"_v{k}" if k else "") + ".png"))

if __name__ == "__main__": main()
