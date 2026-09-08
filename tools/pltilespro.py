#!/usr/bin/env python3
"""One PixelLab Tiles Pro call (connectable terrain tileset) from a JSON body.

    python3 tools/pltilespro.py <body.json> <out-dir>

Posts the body to /create-tiles-pro, polls /tiles-pro/{id}, downloads every
tile as tile_<n>.png, writes meta.json (tile_rules, usage) and sheet.png.
"""
import base64, io, json, os, sys, time, urllib.request
from PIL import Image

API = "https://api.pixellab.ai/v2"
TOKEN = open(os.path.expanduser("~/.config/pixellab/token")).read().strip()

def req(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(API + path, data=data, method=method,
                               headers={"Authorization": "Bearer " + TOKEN,
                                        "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(r, timeout=120) as f:
            return f.status, json.loads(f.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")

body = json.load(open(sys.argv[1]))
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
st, resp = req("POST", "/create-tiles-pro", body)
print("post", st, json.dumps(resp)[:300])
if st != 202:
    sys.exit(1)
tid = resp.get("tile_id") or resp.get("id") or resp.get("job_id")
if not tid:
    print(resp); sys.exit(1)
while True:
    time.sleep(10)
    st, res = req("GET", f"/tiles-pro/{tid}")
    print("poll", st, str(res)[:120])
    if st == 200 and res.get("storage_urls"):
        break
    if st not in (200, 423, 202):
        sys.exit(1)
urls = res["storage_urls"]
tiles = {}
for name, url in urls.items():
    dl = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
    with urllib.request.urlopen(dl, timeout=120) as f:
        im = Image.open(io.BytesIO(f.read())).convert("RGBA")
    im.save(os.path.join(out, name + ".png")); tiles[name] = im
json.dump({"tile_id": tid, "usage": res.get("usage"), "kind": res.get("kind"),
           "tile_rules": res.get("tile_rules"), "body": body},
          open(os.path.join(out, "meta.json"), "w"), indent=1)
names = sorted(tiles, key=lambda n: int(n.split("_")[-1]))
w, h = tiles[names[0]].size
cols = 4
rows = (len(names) + cols - 1) // cols
sheet = Image.new("RGBA", (cols * w, rows * h))
for i, n in enumerate(names):
    sheet.paste(tiles[n], ((i % cols) * w, (i // cols) * h))
sheet.save(os.path.join(out, "sheet.png"))
print(len(names), "tiles", w, "x", h, "usage", res.get("usage"), "in", out)
