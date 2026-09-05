#!/usr/bin/env python3
"""Render a zone .dat to a flat PNG for inspection.

Usage:
  tools/maprender.py <pack-dir> <map.dat> <out.png> [--scale N] [--zone ID]
                     [--grid] [--tiles]

Two modes:

  default   one pixel block per tile, coloured by terrain. Fast, and the
            whole zone reads at a glance -- this is the mode for judging
            coastlines and landmass shape.
  --tiles   composite the pack's real 48x34 tile art. Slower and produces a
            large image, but it is what the zone will actually look like.

With --zone, the pack's declared objects for that zone (towns, castles,
chests, signs, dwellings, armies) are overlaid as labelled markers, so
placement can be checked against the terrain under it.
"""
import json
import os
import sys

from PIL import Image, ImageDraw

# Terrain colours. Deliberately close to the engine's own minimap palette
# (game.json colors.minimap_*) so this preview and the in-game M view agree.
TERRAIN_RGB = {
    "grass":    (72, 132, 48),
    "forest":   (28, 78, 32),
    "mountain": (120, 108, 96),
    "water":    (36, 68, 140),
    "desert":   (198, 176, 104),
}
OBJECT_RGB = {
    "town":     (240, 220, 80),
    "castle":   (230, 90, 70),
    "chest":    (250, 250, 250),
    "sign":     (170, 140, 90),
    "dwelling": (210, 120, 210),
    "army":     (255, 40, 40),
}


def load_pack(pack_dir):
    with open(os.path.join(pack_dir, "game.json")) as f:
        return json.load(f)


def read_map(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            rows.append(line)
    w = max(len(r) for r in rows)
    return [r.ljust(w, ".") for r in rows], w, len(rows)


def zone_objects(pack, zone_id):
    """[(x, y, kind)] for everything the pack places in this zone."""
    out = []
    for t in pack.get("towns", []):
        if t.get("zone") == zone_id:
            out.append((t["x"], t["y"], "town"))
    for c in pack.get("castles", []):
        if c.get("zone") == zone_id:
            out.append((c.get("gate_x", c.get("x")),
                        c.get("gate_y", c.get("y")), "castle"))
    for z in pack.get("zones", []):
        if z.get("id") != zone_id:
            continue
        for kind in ("chests", "signs", "dwellings", "armies"):
            for o in z.get(kind, []):
                if "x" in o and "y" in o:
                    out.append((o["x"], o["y"], kind[:-1]))
    return out


def render_flat(rows, w, h, codes, scale):
    img = Image.new("RGB", (w * scale, h * scale), (0, 0, 0))
    px = img.load()
    for y in range(h):
        for x in range(w):
            code = rows[y][x]
            terr = codes.get(code, {}).get("terrain", "grass")
            rgb = TERRAIN_RGB.get(terr, (255, 0, 255))
            # A blocking tile is drawn a shade darker so the walls read.
            if codes.get(code, {}).get("blocks_foot") and terr != "water":
                rgb = tuple(int(v * 0.82) for v in rgb)
            for dy in range(scale):
                for dx in range(scale):
                    px[x * scale + dx, y * scale + dy] = rgb
    return img


def render_tiles(rows, w, h, codes, pack_dir, tile_set="", cell=(48, 34)):
    TW, TH = cell
    img = Image.new("RGB", (w * TW, h * TH), (0, 0, 0))
    cache = {}
    for y in range(h):
        for x in range(w):
            code = rows[y][x]
            art = codes.get(code, {}).get("art")
            if not art:
                continue
            if art not in cache:
                # Same fixed layout the engine uses: src/tile_cache.c resolves
                # a tile_codes `art` stem as art/tiles/<stem>.png, or under
                # art/tiles/<tile_set>/ when the zone declares a tile_set.
                p = os.path.join(pack_dir, "art", "tiles", tile_set, art + ".png")
                cache[art] = (Image.open(p).convert("RGBA")
                              if os.path.exists(p) else None)
                if cache[art] is None:
                    print(f"  warn: no art for tile '{art}' "
                          f"(looked for {p})")
            t = cache[art]
            if t is not None:
                img.paste(t, (x * TW, y * TH), t)
    return img


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) < 3:
        print(__doc__)
        return 2
    pack_dir, map_path, out_path = args[0], args[1], args[2]

    scale = 8
    for f in flags:
        if f.startswith("--scale"):
            scale = int(f.split("=", 1)[1]) if "=" in f else 8
    zone_id = None
    for i, a in enumerate(sys.argv):
        if a == "--zone" and i + 1 < len(sys.argv):
            zone_id = sys.argv[i + 1]
        elif a.startswith("--zone="):
            zone_id = a.split("=", 1)[1]
    if zone_id in args:
        args.remove(zone_id)

    pack = load_pack(pack_dir)
    codes = pack["tile_codes"]
    rows, w, h = read_map(map_path)

    if "--tiles" in flags:
        tile_set = ""
        for z in pack.get("zones", []):
            if zone_id and z.get("id") == zone_id:
                tile_set = z.get("tile_set", "")
        r = pack.get("render", {})
        cell = (int(r.get("tile_w", 48)), int(r.get("tile_h", 34)))
        img = render_tiles(rows, w, h, codes, pack_dir, tile_set, cell)
    else:
        img = render_flat(rows, w, h, codes, scale)
        cell = (scale, scale)

    if "--grid" in flags and cell[0] >= 6:
        d = ImageDraw.Draw(img)
        for x in range(0, w + 1, 10):
            d.line([(x * cell[0], 0), (x * cell[0], h * cell[1])],
                   fill=(255, 255, 255), width=1)
        for y in range(0, h + 1, 10):
            d.line([(0, y * cell[1]), (w * cell[0], y * cell[1])],
                   fill=(255, 255, 255), width=1)

    if zone_id:
        d = ImageDraw.Draw(img)
        objs = zone_objects(pack, zone_id)
        r = max(2, cell[0] // 3)
        for (x, y, kind) in objs:
            cx = x * cell[0] + cell[0] // 2
            cy = y * cell[1] + cell[1] // 2
            d.ellipse([cx - r, cy - r, cx + r, cy + r],
                      fill=OBJECT_RGB.get(kind, (255, 255, 255)),
                      outline=(0, 0, 0))
        print(f"overlaid {len(objs)} objects for zone {zone_id}")

    img.save(out_path)
    print(f"wrote {out_path}  {img.width}x{img.height}  "
          f"({w}x{h} tiles, {'art' if '--tiles' in flags else 'flat'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
