#!/usr/bin/env python3
"""Stage 0 sheet test, free part.
  split RAW_SHEET OUT_DIR            cut the sheet into town/rider/tree/crag, measure, pad to the plan canvases
  scene OUT_DIR TILES_DIR KEY_PNG REQUEST_JSON RAW_SHEET [NONBG_PNG]
                                     build the 5x5 scene (plan item 1.6 layout) and the review page
Never scales anything. Numbers are printed; no verdict words."""
import sys, os, json, html
import numpy as np
from PIL import Image

CANVAS = {"town": (192, 96), "rider": (56, 56), "tree": (72, 72), "crag": (96, 96)}

def load(p): return np.array(Image.open(p).convert("RGBA")).astype(int)

def dilate(m, r):
    H, W = m.shape; d = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            s = np.zeros_like(m)
            ys, ye, xs, xe = max(0, dy), H + min(0, dy), max(0, dx), W + min(0, dx)
            s[ys:ye, xs:xe] = m[ys - dy:ye - dy, xs - dx:xe - dx]
            d |= s
    return d

def components(op):
    H, W = op.shape; dm = dilate(op, 3); lab = np.zeros((H, W), int); n = 0
    for y0 in range(H):
        for x0 in range(W):
            if dm[y0, x0] and lab[y0, x0] == 0:
                n += 1; st = [(y0, x0)]; lab[y0, x0] = n
                while st:
                    y, x = st.pop()
                    for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                        if 0 <= ny < H and 0 <= nx < W and dm[ny, nx] and lab[ny, nx] == 0:
                            lab[ny, nx] = n; st.append((ny, nx))
    out = []
    for k in range(1, n + 1):
        m = (lab == k) & op
        if m.sum() == 0: continue
        ys, xs = np.nonzero(m)
        out.append(dict(area=int(m.sum()), x0=int(xs.min()), x1=int(xs.max()), y0=int(ys.min()), y1=int(ys.max()), mask=m))
    return out

def measure(a):
    op = a[..., 3] > 0; R, G, B = a[..., 0], a[..., 1], a[..., 2]
    ys = np.nonzero(op.any(1))[0]; xs = np.nonzero(op.any(0))[0]
    bot = a[-12:]; o = bot[..., 3] > 0
    return dict(w=int(xs.max() - xs.min() + 1), h=int(ys.max() - ys.min() + 1),
                purple=int((op & (R > G + 30) & (B > G + 30)).sum()),
                ground=int((o & (bot[..., 0] > 180) & (bot[..., 1] > 90) & (bot[..., 2] < 90)).sum()))

def split(raw, out):
    os.makedirs(out, exist_ok=True)
    a = load(raw); op = a[..., 3] > 0
    comps = sorted(components(op), key=lambda c: -c["area"])
    big = [c for c in comps if c["area"] >= 40]; debris = [c for c in comps if c["area"] < 40]
    print(f"components: {len(big)} (debris under 40 px: {len(debris)})")
    for c in big: print(f"  area {c['area']:5d}  box x {c['x0']}-{c['x1']}  y {c['y0']}-{c['y1']}  ({c['x1']-c['x0']+1} x {c['y1']-c['y0']+1})")
    roles = {}
    if len(big) >= 4:
        big = big[:4]
        town = max(big, key=lambda c: c["x1"] - c["x0"]); rest = [c for c in big if c is not town]
        tc = (town["y0"] + town["y1"]) / 2
        top = [c for c in rest if abs((c["y0"] + c["y1"]) / 2 - tc) < 48]; bottom = [c for c in rest if c not in top]
        if len(top) == 1 and len(bottom) == 2:
            bottom.sort(key=lambda c: c["x0"]); roles = dict(town=town, rider=top[0], tree=bottom[0], crag=bottom[1]); how = "by position (town row: rider; lower row left to right: tree, crag)"
        else:
            rest.sort(key=lambda c: -(c["y1"] - c["y0"])); roles = dict(town=town, crag=rest[0], tree=rest[1], rider=rest[2]); how = "FALLBACK by size (layout not as prompted): tallest=crag, next=tree, smallest=rider"
    else:
        how = f"only {len(big)} objects found; no roles assigned"
    print("roles:", how)
    report = {"components": len(big), "debris": len(debris), "assignment": how, "objects": {}}
    for name, c in roles.items():
        crop = a[c["y0"]:c["y1"] + 1, c["x0"]:c["x1"] + 1].copy(); crop[~c["mask"][c["y0"]:c["y1"] + 1, c["x0"]:c["x1"] + 1]] = 0
        Image.fromarray(crop.astype(np.uint8)).save(f"{out}/{name}_trim.png")
        m = measure(crop); cw, ch = CANVAS[name]; fits = m["w"] <= cw and m["h"] <= ch
        if fits:
            pad = np.zeros((ch, cw, 4), int); x = (cw - m["w"]) // 2; y = ch - m["h"]
            pad[y:y + m["h"], x:x + m["w"]] = crop; Image.fromarray(pad.astype(np.uint8)).save(f"{out}/{name}.png")
        m.update(canvas=f"{cw}x{ch}", fits_canvas=fits); report["objects"][name] = m
        print(f"  {name:5s} drawn {m['w']:3d} x {m['h']:3d}  canvas {cw}x{ch}  fits {fits}  purple {m['purple']}  ground {m['ground']}")
    o = report["objects"]
    if len(o) == 4:
        r = {"hero_h_42_56": o["rider"]["h"], "town_h_over_hero_h_min_1.5": round(o["town"]["h"] / o["rider"]["h"], 2),
             "town_w_over_hero_w_min_3": round(o["town"]["w"] / o["rider"]["w"], 2), "tree_h_over_hero_h_min_1.1": round(o["tree"]["h"] / o["rider"]["h"], 2),
             "crag_h_over_tree_h_min_1.25": round(o["crag"]["h"] / o["tree"]["h"], 2)}
        report["ratios_vs_plan_1.6"] = r
        for k, v in r.items(): print(f"  {k}: {v}")
    json.dump(report, open(f"{out}/measure.json", "w"), indent=1)

def scene(out, tiles, key, req, raw, nonbg=None):
    T = lambda n: Image.open(f"{tiles}/{n}.png").convert("RGBA")
    S = Image.new("RGBA", (480, 480)); grid = [["grass"] * 5 for _ in range(5)]
    wood = os.path.exists(f"{tiles}/forest_edge_nw.png")
    if wood: grid[0][0], grid[0][1], grid[1][0], grid[1][1] = "forest_edge_nw", "forest_edge_ne", "forest_edge_sw", "forest_edge_es"
    for r in range(5):
        for c in range(5): S.paste(T(grid[r][c]), (c * 96, r * 96))
    def put(name, cx, cy, cells_w=1):
        p = f"{out}/{name}.png"; p = p if os.path.exists(p) else f"{out}/{name}_trim.png"
        if not os.path.exists(p): return
        im = Image.open(p).convert("RGBA"); x = cx * 96 + (cells_w * 96 - im.width) // 2; y = cy * 96 + 96 - im.height
        S.paste(im, (x, y), im)
    put("rider", 2, 2); put("town", 1, 3, 2); put("crag", 3, 4); put("crag", 4, 4)
    if not wood: put("tree", 0, 0)
    S.save(f"{out}/stage0_scene.png"); S.resize((960, 960), Image.NEAREST).save(f"{out}/stage0_scene_2x.png")
    rq = json.load(open(req)); rq.pop("input_palette", None); rq.pop("reference_images", None)
    shown = json.dumps(rq, indent=1) + "\n+ input_palette: <base64 art/palettes/italia.png, 72 colours>\n+ reference_images: [<base64 art/reference/italia96/italia_key.png>]"
    call = "POST https://api.retrodiffusion.ai/v1/inferences\nX-RD-Token: <token hidden>\nContent-Type: application/json\n\n" + shown
    for n in ("sheet_raw.png",): Image.open(raw).save(f"{out}/{n}")
    if nonbg: Image.open(nonbg).save(f"{out}/sheet_nonbg.png")
    Image.open(key).save(f"{out}/key.png")
    meas = json.load(open(f"{out}/measure.json"))
    def img(n, s=1):
        im = Image.open(f"{out}/{n}"); return f'<img style="image-rendering:pixelated" src="{n}" width="{im.width*s}" height="{im.height*s}">'
    rows = [f"<tr><td>{img('sheet_raw.png')}</td><td>{img('sheet_raw.png',2)}</td><td><code>{html.escape(call)}</code></td><td>{html.escape(rq['prompt'])}</td></tr>"]
    if nonbg: rows.append(f"<tr><td colspan=4>Before background removal (return_non_bg_removed): {img('sheet_nonbg.png')} {img('sheet_nonbg.png',2)}</td></tr>")
    crops = " ".join(f"<figure style='display:inline-block'>{img(n+'_trim.png',2)}<figcaption>{n}</figcaption></figure>" for n in ("rider", "tree", "crag", "town") if os.path.exists(f"{out}/{n}_trim.png"))
    wood_note = "" if wood else "<p>No wood: romeart.py lattice refused the sheet's tree at its true size: <code>forest code 1: a sprite crosses a terminal line</code>. One tree is placed at the top left instead.</p>"
    page = ('<!doctype html><meta charset="utf-8"><style>body{background:#333;color:#ddd;font:14px sans-serif}table{border-collapse:collapse}'
            'td,th{border:1px solid #555;padding:8px;vertical-align:top}code{white-space:pre-wrap;word-break:break-all;display:block;max-width:520px;font-size:12px}</style>'
            '<h1>stage0</h1><h2>The sheet, as returned</h2><table><tr><th>1x</th><th>2x</th><th>Exact request sent</th><th>Prompt</th></tr>' + "".join(rows) + '</table>'
            f'<h2>The four objects, cut from it (2x)</h2>{crops}<pre>{html.escape(json.dumps(meas, indent=1))}</pre>'
            f'{wood_note}<h2>The scene (plan item 1.6 layout) at 1x, with the key frame beside it</h2>{img("stage0_scene.png")} {img("key.png")}'
            f'<h2>The scene at 2x</h2>{img("stage0_scene_2x.png")}')
    open(f"{out}/stage0.html", "w").write(page); print("wrote", f"{out}/stage0.html")

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "split": split(sys.argv[2], sys.argv[3])
    elif cmd == "scene": scene(*sys.argv[2:])
    else: sys.exit(__doc__)
