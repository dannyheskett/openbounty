#!/usr/bin/env python3
"""Run one PixelLab create-tileset call and save its 16 tiles.

    python3 tools/pltileset.py <out-dir> <request.json>

The request file is the JSON body (no images). Token at ~/.config/pixellab/token.
Writes submit.json, result.json, tile_NN.png, tiles_meta.json, sheet.png and
a 7x6 mock (map_mock_1x.png / _3x.png) laid out by corner pattern, and prints
the terrain ids (the lower id is what later sets chain to) and seam figures.
"""
import base64, json, os, sys, time, urllib.request
from PIL import Image, ImageDraw, ImageChops, ImageStat

out, reqp = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
tok = open(os.path.expanduser("~/.config/pixellab/token")).read().strip()
H = {"Authorization": "Bearer " + tok, "Content-Type": "application/json"}
body = json.load(open(reqp))
json.dump(body, open(os.path.join(out, "request.json"), "w"), indent=1)
req = urllib.request.Request("https://api.pixellab.ai/v2/create-tileset", data=json.dumps(body).encode(), headers=H, method="POST")
try:
    r = urllib.request.urlopen(req, timeout=120); resp = json.loads(r.read()); code = r.status
except urllib.error.HTTPError as e:
    code = e.code; resp = json.loads(e.read() or b"{}")
print("HTTP", code, json.dumps(resp)[:300])
json.dump(resp, open(os.path.join(out, "submit.json"), "w"), indent=1)
tid = resp.get("tileset_id")
if not tid:
    sys.exit("no tileset id")
s = None
for i in range(90):
    time.sleep(10)
    try:
        g = urllib.request.urlopen(urllib.request.Request(f"https://api.pixellab.ai/v2/tilesets/{tid}", headers=H), timeout=60)
        s = json.loads(g.read()); st = 200
    except urllib.error.HTTPError as e:
        st = e.code; s = json.loads(e.read() or b"{}")
    if st == 200 and s.get("tileset"):
        print(f"done at {(i + 1) * 10}s"); break
json.dump(s, open(os.path.join(out, "result.json"), "w"), indent=1)
ts = s["tileset"]
print("terrain ids", s.get("metadata", {}).get("terrain_ids"))
imgs = []
for i, t in enumerate(ts["tiles"]):
    data = base64.b64decode(t["image"]["base64"].split(",")[-1])
    p = os.path.join(out, f"tile_{i:02d}.png"); open(p, "wb").write(data)
    imgs.append((i, t, Image.open(p).convert("RGBA")))
json.dump([{k: v for k, v in t.items() if k != "image"} for _, t, _ in imgs], open(os.path.join(out, "tiles_meta.json"), "w"), indent=1)
T = imgs[0][2].width; S = 96 // T


def key(c):
    return tuple({"upper": "u", "lower": "l"}.get(c[k], "t") for k in ("NW", "NE", "SW", "SE"))


by = {}
for _, t, im in imgs:
    by.setdefault(key(t["corners"]), im)
grid = [["l"] * 8 for _ in range(7)]
for y in range(1, 4):
    for x in range(1, 5):
        grid[y][x] = "u"
grid[4][2] = "u"; grid[4][3] = "u"
mock = Image.new("RGB", (7 * T, 6 * T))
for cy in range(6):
    for cx in range(7):
        k = (grid[cy][cx], grid[cy][cx + 1], grid[cy + 1][cx], grid[cy + 1][cx + 1])
        if k in by:
            mock.paste(by[k].convert("RGB"), (cx * T, cy * T))
mock.save(os.path.join(out, "map_mock_1x.png"))
mock.resize((mock.width * S, mock.height * S), Image.NEAREST).save(os.path.join(out, "map_mock_3x.png"))


def diff(a, b):
    return round(sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3, 1)


tiles = {"".join(k): im.convert("RGB") for k, im in by.items()}
rows = [diff(a.crop((T - 1, 0, T, T)), b.crop((0, 0, 1, T))) for ka, a in tiles.items() for kb, b in tiles.items() if ka[1] == kb[0] and ka[3] == kb[2]]
print("horizontal seam mean", round(sum(rows) / len(rows), 1), "max", max(rows))
if "uuuu" in tiles:
    f = tiles["uuuu"]; print("upper self-seam v/h", diff(f.crop((0, 0, T, 1)), f.crop((0, T - 1, T, T))), diff(f.crop((0, 0, 1, T)), f.crop((T - 1, 0, T, T))))
cols = 4; rws = (len(imgs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (T * S + 8) + 8, rws * (T * S + 24) + 8), (40, 40, 40)); d = ImageDraw.Draw(sheet)
for n, (i, t, im) in enumerate(imgs):
    x = 8 + (n % cols) * (T * S + 8); y = 8 + (n // cols) * (T * S + 24)
    big = im.resize((T * S, T * S), Image.NEAREST); sheet.paste(big, (x, y + 16), big)
    c = t["corners"]; d.text((x, y), f"{i} {c['NW'][0]}{c['NE'][0]}{c['SW'][0]}{c['SE'][0]}", fill=(230, 230, 230))
sheet.save(os.path.join(out, "sheet.png"))
