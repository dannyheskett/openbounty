#!/usr/bin/env python3
"""Cut a Retro Diffusion sheet (several objects on transparency) into named sprites.
  split RAW OUT --names n1,n2,... --canvases WxH,WxH,... [--rows 2] [--gap 1]
      rows by the largest gaps between the objects' vertical centres, objects left to
      right within a row, names in that order; writes <name>_trim.png (the object, as
      drawn), <name>.png (bottom-centred on its canvas, only if it fits) and measure.json
  page OUT_HTML TITLE RUN_DIR... each run dir holds request.json, 01_raw.png and cut/
Nothing is ever scaled. Numbers are printed; no verdicts."""
import sys, os, json, html, argparse
import numpy as np
from PIL import Image

def load(p): return np.array(Image.open(p).convert("RGBA")).astype(int)

def dilate(m, r):
    H, W = m.shape; d = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            s = np.zeros_like(m); ys, ye, xs, xe = max(0, dy), H + min(0, dy), max(0, dx), W + min(0, dx)
            s[ys:ye, xs:xe] = m[ys - dy:ye - dy, xs - dx:xe - dx]; d |= s
    return d

def components(op, gap=3, debris=40):
    H, W = op.shape; dm = dilate(op, gap); lab = np.zeros((H, W), int); n = 0
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
        if m.sum() < debris: continue
        ys, xs = np.nonzero(m)
        out.append(dict(area=int(m.sum()), x0=int(xs.min()), x1=int(xs.max()), y0=int(ys.min()), y1=int(ys.max()), mask=m))
    return out

def measure(a):
    op = a[..., 3] > 0; R, G, B = a[..., 0], a[..., 1], a[..., 2]
    ys = np.nonzero(op.any(1))[0]; xs = np.nonzero(op.any(0))[0]; bot = a[-12:]; o = bot[..., 3] > 0
    return dict(w=int(xs.max() - xs.min() + 1), h=int(ys.max() - ys.min() + 1), purple=int((op & (R > G + 30) & (B > G + 30)).sum()),
                ground=int((o & (bot[..., 0] > 180) & (bot[..., 1] > 90) & (bot[..., 2] < 90)).sum()))

def split(a):
    os.makedirs(a.out, exist_ok=True)
    names, canvases = a.names.split(","), [tuple(int(v) for v in c.split("x")) for c in a.canvases.split(",")]
    assert len(names) == len(canvases), "one canvas per name"
    im = load(a.raw); comps = components(im[..., 3] > 0, gap=a.gap)
    print(f"objects found: {len(comps)}, expected {len(names)}")
    comps.sort(key=lambda c: (c["y0"] + c["y1"]) / 2)
    cy = [(c["y0"] + c["y1"]) / 2 for c in comps]
    gaps = sorted(range(1, len(comps)), key=lambda i: cy[i] - cy[i - 1], reverse=True)[:a.rows - 1]
    rows, start = [], 0
    for i in sorted(gaps) + [len(comps)]:
        rows.append(sorted(comps[start:i], key=lambda c: c["x0"])); start = i
    ordered = [c for r in rows for c in r]
    for r in rows: print("  row:", " ".join(f"[{c['x0']}-{c['x1']},{c['y0']}-{c['y1']}]" for c in r))
    rep = {"found": len(comps), "expected": len(names), "objects": {}}
    for name, (cw, ch), c in zip(names, canvases, ordered):
        crop = im[c["y0"]:c["y1"] + 1, c["x0"]:c["x1"] + 1].copy(); crop[~c["mask"][c["y0"]:c["y1"] + 1, c["x0"]:c["x1"] + 1]] = 0
        Image.fromarray(crop.astype(np.uint8)).save(f"{a.out}/{name}_trim.png")
        m = measure(crop); fits = m["w"] <= cw and m["h"] <= ch
        if fits:
            pad = np.zeros((ch, cw, 4), int); x, y = (cw - m["w"]) // 2, ch - m["h"]
            pad[y:y + m["h"], x:x + m["w"]] = crop; Image.fromarray(pad.astype(np.uint8)).save(f"{a.out}/{name}.png")
        m.update(canvas=f"{cw}x{ch}", fits=fits, height_pct_of_canvas=round(100 * m["h"] / ch)); rep["objects"][name] = m
        print(f"  {name:16s} drawn {m['w']:3d} x {m['h']:3d}  canvas {cw}x{ch}  height {m['height_pct_of_canvas']}%  fits {fits}  purple {m['purple']}  ground {m['ground']}")
    for name in names[len(ordered):]: print(f"  {name:16s} MISSING"); rep["objects"][name] = None
    json.dump(rep, open(f"{a.out}/measure.json", "w"), indent=1)

def page(a):
    os.makedirs(os.path.dirname(a.out_html) or ".", exist_ok=True); web = os.path.dirname(a.out_html); parts = []
    for run in a.runs:
        rid = os.path.basename(os.path.dirname(run.rstrip("/"))); d = f"{web}/{rid}"; os.makedirs(d, exist_ok=True)
        Image.open(f"{run}/01_raw.png").save(f"{d}/raw.png"); rq = json.load(open(f"{run}/request.json")); w, h = Image.open(f"{run}/01_raw.png").size
        rq.pop("input_palette", None); rq.pop("reference_images", None)
        call = ("POST https://api.retrodiffusion.ai/v1/inferences\nX-RD-Token: <token hidden>\nContent-Type: application/json\n\n" + json.dumps(rq)
                + "\n+ input_palette: <base64 art/palettes/italia.png>\n+ reference_images: [<base64 art/reference/italia96/italia_key.png>]")
        cuts = ""
        if os.path.isdir(f"{run}/cut"):
            meas = json.load(open(f"{run}/cut/measure.json"))
            for n in meas["objects"]:
                if os.path.exists(f"{run}/cut/{n}_trim.png"):
                    Image.open(f"{run}/cut/{n}_trim.png").save(f"{d}/{n}.png"); cw, ch = Image.open(f"{d}/{n}.png").size
                    cuts += f'<figure style="display:inline-block"><img style="image-rendering:pixelated" src="{rid}/{n}.png" width="{cw*2}" height="{ch*2}"><figcaption>{n}</figcaption></figure>'
            cuts += f"<pre>{html.escape(json.dumps(meas, indent=1))}</pre>"
        parts.append(f'<h2>{rid}</h2><table><tr><th>1×</th><th>2×</th><th>Exact request sent</th><th>Prompt</th></tr><tr>'
                     f'<td><img src="{rid}/raw.png" width="{w}" height="{h}"></td><td><img style="image-rendering:pixelated" src="{rid}/raw.png" width="{w*2}" height="{h*2}"></td>'
                     f'<td><code>{html.escape(call)}</code></td><td>{html.escape(rq["prompt"])}</td></tr></table><h3>Cut objects (2×)</h3>{cuts}')
    open(a.out_html, "w").write('<!doctype html><meta charset="utf-8"><style>body{background:#333;color:#ddd;font:14px sans-serif}table{border-collapse:collapse}'
        'td,th{border:1px solid #555;padding:8px;vertical-align:top}code{white-space:pre-wrap;word-break:break-all;display:block;max-width:520px;font-size:12px}</style>'
        f'<h1>{html.escape(a.title)}</h1>' + "".join(parts)); print("wrote", a.out_html)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter); sp = ap.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("split"); s.add_argument("raw"); s.add_argument("out"); s.add_argument("--names", required=True); s.add_argument("--canvases", required=True); s.add_argument("--rows", type=int, default=2); s.add_argument("--gap", type=int, default=1, help="px of transparency that still joins two parts of one object (default 1)"); s.set_defaults(f=split)
    g = sp.add_parser("page"); g.add_argument("out_html"); g.add_argument("title"); g.add_argument("runs", nargs="+"); g.set_defaults(f=page)
    a = ap.parse_args(); a.f(a)
