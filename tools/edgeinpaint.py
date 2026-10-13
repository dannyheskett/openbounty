#!/usr/bin/env python3
"""Edge tiles for a ground by Retro Diffusion's inpainting: the inside tile (e.g. sand) and the outside tile
(the meadow) are laid per shape as the game's own 96 px tiles, four shapes to a 192 px block, and only the
band along their boundary is repainted by the model; the tiles' own sides stay the shared tiles exactly, so
every join matches by construction (the method of Retro Diffusion's tilesetbuilder).
  edgeinpaint.py INSIDE.png OUTSIDE.png OUT_DIR --name desert [--estimate] [--seed N] [--band 10] [--margin 6]"""
import argparse, base64, io, json, os, sys, time, uuid, random
import numpy as np, requests
from PIL import Image, ImageFilter
API = "https://api.retrodiffusion.ai/v1"
T = 96
# (tl, tr, bl, br) 1 = inside; the game's names
SHAPES = {"edge_n": (0, 0, 1, 1), "edge_s": (1, 1, 0, 0), "edge_w": (0, 1, 0, 1), "edge_e": (1, 0, 1, 0),
          "edge_nw": (0, 0, 0, 1), "edge_ne": (0, 0, 1, 0), "edge_sw": (0, 1, 0, 0), "edge_es": (1, 0, 0, 0),
          "inner_es": (1, 1, 1, 0), "inner_sw": (1, 1, 0, 1), "inner_ne": (1, 0, 1, 1), "inner_nw": (0, 1, 1, 1)}
BLOCKS = [("corners", [["edge_nw", "edge_ne"], ["edge_sw", "edge_es"]]),
          ("inners", [["inner_es", "inner_sw"], ["inner_ne", "inner_nw"]]),
          ("ns", [["edge_n", "edge_n_v1"], ["edge_s", "edge_s_v1"]]),
          ("we", [["edge_w", "edge_e"], ["edge_w_v1", "edge_e_v1"]])]

def signed_dist(shape):
    """px: >0 inside, <0 outside; the boundary crosses every open side at its middle, square to it"""
    tl, tr, bl, br = SHAPES[shape]; yy, xx = np.mgrid[0:T, 0:T] + 0.5
    s = sum(SHAPES[shape])
    if s == 2:                                   # a straight edge
        if (tl, tr) == (0, 0): return yy - 48
        if (bl, br) == (0, 0): return 48 - yy
        if (tl, bl) == (0, 0): return xx - 48
        return 48 - xx
    corner = {(0, 0, 0, 1): (T, T), (0, 0, 1, 0): (T, 0), (0, 1, 0, 0): (0, T), (1, 0, 0, 0): (0, 0),
              (1, 1, 1, 0): (T, T), (1, 1, 0, 1): (T, 0), (1, 0, 1, 1): (0, T), (0, 1, 1, 1): (0, 0)}[SHAPES[shape]]
    d = 48 - np.hypot(yy - corner[0], xx - corner[1])          # >0 within the quarter disc at that corner
    return d if s == 1 else -d                                  # outer corner: the disc is inside; inner corner: the disc is the open grass

def compose(inside, outside, shape, rng, feather=4, noise=8):
    d = signed_dist(shape)
    w = 1 / (1 + np.exp(-d / feather * 2))                      # soft step across the boundary
    w = np.clip(w + rng.normal(0, noise / 100, w.shape) * (np.abs(d) < 3 * feather), 0, 1)
    return (inside * w[..., None] + outside * (1 - w[..., None])).round().astype(np.uint8), d

def b64(im):
    buf = io.BytesIO(); im.save(buf, format="PNG"); return base64.b64encode(buf.getvalue()).decode()

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inside"); ap.add_argument("outside"); ap.add_argument("out"); ap.add_argument("--name", required=True)
    ap.add_argument("--prompt", default="pale golden desert sand meeting short bright mossy green grass along a soft uneven edge, seen from above, light from the upper left; only sand and grass")
    ap.add_argument("--seed", type=int, default=11130); ap.add_argument("--band", type=float, default=10); ap.add_argument("--margin", type=int, default=6)
    ap.add_argument("--estimate", action="store_true", help="free: compose, write the inputs, ask the price, run nothing")
    ap.add_argument("--only", nargs="*", help="block names to run")
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    inside = np.array(Image.open(a.inside).convert("RGB")).astype(float); outside = np.array(Image.open(a.outside).convert("RGB")).astype(float)
    tok = open(os.path.expanduser("~/.config/retrodiffusion/token")).read().strip(); H = {"X-RD-Token": tok}
    rng = np.random.default_rng(a.seed)
    for bname, grid in BLOCKS:
        if a.only and bname not in a.only: continue
        img = np.zeros((2 * T, 2 * T, 3), np.uint8); alpha = np.zeros((2 * T, 2 * T), np.uint8)
        for r, row in enumerate(grid):
            for c, nm in enumerate(row):
                shape = nm.replace("_v1", ""); tile, d = compose(inside, outside, shape, rng)
                img[r * T:(r + 1) * T, c * T:(c + 1) * T] = tile
                alpha[r * T:(r + 1) * T, c * T:(c + 1) * T] = (np.abs(d) < a.band) * 255
        m = a.margin; alpha[:m, :] = alpha[-m:, :] = alpha[:, :m] = alpha[:, -m:] = 0     # the block's own sides stay the shared tiles exactly
        src = Image.fromarray(img); mask = Image.fromarray(np.dstack([np.zeros_like(alpha)] * 3 + [alpha]).astype(np.uint8), "RGBA")
        src.save(os.path.join(a.out, f"block_{bname}_in.png")); mask.save(os.path.join(a.out, f"block_{bname}_mask.png"))
        body = {"input_image": b64(src), "mask_image": b64(mask), "prompt": a.prompt, "seed": a.seed}
        est = requests.post(f"{API}/edit/tools/inpainting/estimate", headers=H, json=body, timeout=60); print(bname, "estimate:", est.status_code, est.text[:200])
        if a.estimate or est.status_code != 200: continue
        body["custom_id"] = str(uuid.uuid4()); json.dump({k: (v if k not in ("input_image", "mask_image") else f"<{len(v)} b64>") for k, v in body.items()}, open(os.path.join(a.out, f"block_{bname}_request.json"), "w"), indent=1)
        st = requests.post(f"{API}/edit/tools/inpainting/tasks", headers=H, json=body, timeout=60); print(bname, "task:", st.status_code, st.text[:120]); st.raise_for_status(); tid = st.json()["task_id"]
        while True:
            t = requests.get(f"{API}/edit/tasks/{tid}", headers=H, timeout=60).json()
            if t["status"] not in ("pending", "running"): break
            time.sleep(3)
        json.dump(t, open(os.path.join(a.out, f"block_{bname}_task.json"), "w"), indent=1)
        if t["status"] != "succeeded": print(bname, "FAILED", t.get("error")); continue
        res = t["result"]; data = base64.b64decode(res["base64_images"][0]) if res.get("base64_images") else requests.get(res["output_urls"][0], timeout=120).content
        out = Image.open(io.BytesIO(data)).convert("RGB"); out.save(os.path.join(a.out, f"block_{bname}_out.png")); o = np.array(out)
        if o.shape[:2] != (2 * T, 2 * T): print(bname, "unexpected size", o.shape); continue
        for r, row in enumerate(grid):
            for c, nm in enumerate(row):
                Image.fromarray(o[r * T:(r + 1) * T, c * T:(c + 1) * T]).save(os.path.join(a.out, f"{a.name}_{nm}.png"))
        print(bname, "done")

if __name__ == "__main__": main()
