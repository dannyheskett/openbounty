#!/usr/bin/env python3
"""Render a zone .dat to a flat PNG for inspection.

Usage:
  tools/maprender.py <pack-dir> <map.dat> <out.png> [--scale N] [--zone ID]
                     [--grid] [--tiles] [--seed N] [--crop X0,Y0,X1,Y1]
                     [--shrink N] [--no-objects]

Two modes:

  default   one pixel block per tile, coloured by terrain. Fast, and the
            whole zone reads at a glance -- this is the mode for judging
            coastlines and landmass shape.
  --tiles   composite the pack's real tile art at render.tile_w x tile_h,
            with each code's cosmetic `variants` picked per cell the way the
            shell picks them (src/tilevar.c) for game seed --seed (default
            0). Slower and produces a large image, but it is what the zone
            will actually look like.

--crop keeps only the inclusive tile box X0,Y0..X1,Y1, and --shrink N
scales the picture down N times; both are for review pages. --no-objects
keeps --zone's tile set but draws no markers.

With --zone, the pack's declared objects for that zone (towns, castles,
chests, signs, dwellings, armies) are overlaid as labelled markers, so
placement can be checked against the terrain under it.
"""
import argparse
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
    "river":    (58, 118, 196),
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


# A tile_codes key names one raw byte of a map file: the key's own character,
# or a two-digit "\\xNN" hex escape for a byte with no printable spelling (see
# resources_tile_code_from_key in engine/resources.c). Map files are read as
# latin-1, not UTF-8: a byte over 127 is one character, whatever it is.
def code_char(key):
    if len(key) == 4 and key[0] == "\\" and key[1] in "xX":
        try:
            return chr(int(key[2:], 16))
        except ValueError:
            return None
    return key if len(key) == 1 else None


def read_map(path):
    rows = []
    with open(path, encoding="latin-1") as f:
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
        for key, kind in (("chests", "chest"), ("signs", "sign"),
                          ("dwellings", "dwelling"), ("wandering_armies", "army")):
            for o in z.get(key, []):
                if "x" in o and "y" in o:
                    out.append((o["x"], o["y"], kind))
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


def tilevar_pick(seed, x, y, n):
    """src/tilevar.c tilevar_pick, in 32-bit unsigned arithmetic."""
    if n <= 1:
        return 0
    M = 0xFFFFFFFF
    h = (seed ^ 0x9E3779B9) & M
    h ^= (x * 0x85EBCA6B) & M
    h ^= h >> 13
    h ^= (y * 0xC2B2AE35) & M
    h ^= h >> 16
    h = (h * 0x27D4EB2F) & M
    h ^= h >> 15
    return h % n


def variant_lists(codes):
    """art stem -> its variant names, the first code with variants winning, as
    src/tilevar.c tilevar_init keeps one entry per distinct stem."""
    out = {}
    for v in codes.values():
        if v.get("variants") and v.get("art") and v["art"] not in out:
            out[v["art"]] = v["variants"]
    return out


def render_tiles(rows, w, h, codes, pack_dir, tile_set="", cell=(48, 34), set_arts=None,
                 seed=0, box=None):
    TW, TH = cell
    x0, y0, x1, y1 = box or (0, 0, w - 1, h - 1)
    img = Image.new("RGB", ((x1 - x0 + 1) * TW, (y1 - y0 + 1) * TH), (0, 0, 0))
    cache = {}
    var = variant_lists(codes)

    def vary(art, x, y):
        names = var.get(art)
        if not names:
            return art
        k = tilevar_pick(seed, x, y, len(names) + 1)     # 0 = the base art
        return art if k == 0 else names[k - 1]

    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            code = rows[y][x]
            art = codes.get(code, {}).get("art")
            if not art:
                continue
            art = vary(art, x, y)
            px, py = (x - x0) * TW, (y - y0) * TH
            ground = codes.get(code, {}).get("ground")
            if ground:                      # a landmark over its own ground
                ground = vary(ground, x, y)
                gp = os.path.join(pack_dir, "art", "tiles",
                                  tile_set if (tile_set and (not set_arts or ground in set_arts)) else "",
                                  ground + ".png")
                if os.path.exists(gp):
                    gt = Image.open(gp).convert("RGBA")
                    img.paste(gt, (px, py), gt)
            if art not in cache:
                # Same fixed layout the engine uses: src/tile_cache.c resolves
                # a tile_codes `art` stem as art/tiles/<stem>.png, or under
                # art/tiles/<tile_set>/ when the zone declares a tile_set
                # (only the names in its tile_set_arts, when it lists any).
                own = tile_set and (not set_arts or art in set_arts)
                p = os.path.join(pack_dir, "art", "tiles", tile_set if own else "", art + ".png")
                cache[art] = (Image.open(p).convert("RGBA")
                              if os.path.exists(p) else None)
                if cache[art] is None:
                    print(f"  warn: no art for tile '{art}' "
                          f"(looked for {p})")
            t = cache[art]
            if t is not None:
                img.paste(t, (px, py), t)
    # Inner-corner fills (#63), as src/map_render.c draws them: a grass or
    # sand cell with a wood or range on two adjacent sides gets that
    # terrain's fill in the corner between them.
    def ter(x, y):
        if 0 <= x < w and 0 <= y < h:
            return codes.get(rows[y][x], {}).get("terrain", "grass")
        return None
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            here = ter(x, y)
            if here not in ("grass", "desert"):
                continue
            for corner, (dx, dy) in (("ne", (1, -1)), ("nw", (-1, -1)), ("se", (1, 1)), ("sw", (-1, 1))):
                a, b = ter(x + dx, y), ter(x, y + dy)
                if a != b or a not in ("forest", "mountain"):
                    continue
                fam = "_sand" if here == "desert" else ""
                name = f"{a}{fam}_fill_{corner}"
                if name not in cache:
                    own = tile_set and (not set_arts or name in set_arts)
                    p = os.path.join(pack_dir, "art", "tiles", tile_set if own else "", name + ".png")
                    cache[name] = Image.open(p).convert("RGBA") if os.path.exists(p) else None
                if cache[name] is not None:
                    f_ = cache[name]
                    if f_.width != TW * 3:
                        f_ = cache[name] = f_.resize((TW * 3, TH * 3), Image.NEAREST)
                    img.paste(f_, ((x - x0 - 1) * TW, (y - y0 - 1) * TH), f_)
    return img


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("pack_dir")
    ap.add_argument("map_path")
    ap.add_argument("out_path")
    ap.add_argument("--scale", type=int, default=8)
    ap.add_argument("--zone", default=None)
    ap.add_argument("--grid", action="store_true")
    ap.add_argument("--tiles", action="store_true")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--crop", default=None)
    ap.add_argument("--shrink", type=int, default=1)
    ap.add_argument("--no-objects", action="store_true")
    a = ap.parse_args()
    pack_dir, map_path, out_path = a.pack_dir, a.map_path, a.out_path
    scale, zone_id = a.scale, a.zone

    pack = load_pack(pack_dir)
    codes = {c: v for c, v in ((code_char(k), v)
                               for k, v in pack["tile_codes"].items()) if c}
    rows, w, h = read_map(map_path)
    box = tuple(int(v) for v in a.crop.split(",")) if a.crop else (0, 0, w - 1, h - 1)
    if len(box) != 4 or not (0 <= box[0] <= box[2] < w and 0 <= box[1] <= box[3] < h):
        sys.exit(f"maprender: --crop wants X0,Y0,X1,Y1 inside {w}x{h}")

    if a.tiles:
        tile_set, set_arts = "", None
        for z in pack.get("zones", []):
            if zone_id and z.get("id") == zone_id:
                tile_set = z.get("tile_set", "")
                set_arts = set(z.get("tile_set_arts", [])) or None
        r = pack.get("render", {})
        cell = (int(r.get("tile_w", 48)), int(r.get("tile_h", 34)))
        img = render_tiles(rows, w, h, codes, pack_dir, tile_set, cell, set_arts,
                           a.seed, box)
    else:
        img = render_flat(rows, w, h, codes, scale)
        cell = (scale, scale)
        img = img.crop((box[0] * scale, box[1] * scale,
                        (box[2] + 1) * scale, (box[3] + 1) * scale))
    ox, oy = box[0], box[1]

    if a.grid and cell[0] >= 6:
        d = ImageDraw.Draw(img)
        for x in range(0, w + 1, 10):
            d.line([((x - ox) * cell[0], 0), ((x - ox) * cell[0], img.height)],
                   fill=(255, 255, 255), width=1)
        for y in range(0, h + 1, 10):
            d.line([(0, (y - oy) * cell[1]), (img.width, (y - oy) * cell[1])],
                   fill=(255, 255, 255), width=1)

    if zone_id and not a.no_objects:
        d = ImageDraw.Draw(img)
        objs = zone_objects(pack, zone_id)
        r = max(2, cell[0] // 3)
        for (x, y, kind) in objs:
            if not (box[0] <= x <= box[2] and box[1] <= y <= box[3]):
                continue
            cx = (x - ox) * cell[0] + cell[0] // 2
            cy = (y - oy) * cell[1] + cell[1] // 2
            d.ellipse([cx - r, cy - r, cx + r, cy + r],
                      fill=OBJECT_RGB.get(kind, (255, 255, 255)),
                      outline=(0, 0, 0))
        print(f"overlaid {len(objs)} objects for zone {zone_id}")

    if a.shrink > 1:
        img = img.resize((img.width // a.shrink, img.height // a.shrink), Image.LANCZOS)
    img.save(out_path)
    print(f"wrote {out_path}  {img.width}x{img.height}  "
          f"({w}x{h} tiles, {'art' if a.tiles else 'flat'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
