#!/usr/bin/env python3
"""Review page for an animation run: a gif that clears between frames,
a 1x strip, a 5x strip, and per-frame checks.

    python3 tools/loopreview.py build/art/<id>/runNN [--scale 5]

Writes preview.gif, strip1x.png, strip5x.png and review.html into the run
dir and prints the checks:

  size       every frame is the same size
  clip       opaque pixels touching a canvas edge (a clipped limb or weapon)
  halo       near-white opaque pixels next to transparency (flatten fringe)
  opaque     opaque pixel count per frame (a redrawn figure jumps by a lot)
  moved      pixels changed against frame 0 (motion) and, of those, how many
             are in the bottom quarter (feet should stay planted)
  gif        every decoded gif frame equals its source frame

The gif is written with disposal 2 so each frame replaces the last; without
it PIL paints frames over one another and a loop looks smeared.
"""
import os
import sys
from PIL import Image, ImageChops, ImageDraw

run = sys.argv[1].rstrip("/")
scale = int(sys.argv[sys.argv.index("--scale") + 1]) if "--scale" in sys.argv else 5
names = sorted(f for f in os.listdir(run) if f.startswith("frame_") and f.endswith(".png"))
frames = [Image.open(os.path.join(run, f)).convert("RGBA") for f in names]
if not frames:
    sys.exit(f"no frame_*.png in {run}")
w, h = frames[0].size


def opaque(im):
    return im.getchannel("A").point(lambda v: 255 if v > 0 else 0)


def clip(im):
    a = opaque(im)
    bb = a.getbbox()
    return [s for s, hit in (("top", bb[1] == 0), ("bottom", bb[3] == h),
                             ("left", bb[0] == 0), ("right", bb[2] == w)) if hit]


def halo(im):
    px = im.load()
    n = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0 or min(r, g, b) < 200:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and 0 <= yy < h and px[xx, yy][3] == 0:
                    n += 1
                    break
    return n


def moved(im, ref):
    d = ImageChops.difference(im, ref).convert("L").point(lambda v: 255 if v > 24 else 0)
    px = d.load()
    tot = feet = 0
    for y in range(h):
        for x in range(w):
            if px[x, y]:
                tot += 1
                if y >= h * 3 // 4:
                    feet += 1
    return tot, feet


rows = []
for i, f in enumerate(frames):
    o = sum(1 for v in opaque(f).getdata() if v)
    t, ft = moved(f, frames[0])
    rows.append((names[i], f.size, clip(f), halo(f), o, t, ft))

gif = os.path.join(run, "preview.gif")
big = [f.resize((w * 3, h * 3), Image.NEAREST) for f in frames]
big[0].save(gif, save_all=True, append_images=big[1:], duration=150, loop=0,
            disposal=2, transparency=0)

# verify the gif clears: decode every frame and compare to its source
g = Image.open(gif)
gif_ok = True
for i in range(g.n_frames):
    g.seek(i)
    dec = g.convert("RGBA")
    ref = big[i]
    diff = ImageChops.difference(dec, ref).convert("L").point(lambda v: 255 if v > 40 else 0)
    bad = sum(1 for v in diff.getdata() if v)
    if bad > (w * h * 9) // 100:   # more than 1% of pixels off
        gif_ok = False

for s, out in ((1, "strip1x.png"), (scale, f"strip{scale}x.png")):
    strip = Image.new("RGB", (len(frames) * (w * s + 8) + 8, h * s + 16), (60, 60, 60))
    d = ImageDraw.Draw(strip)
    for i, f in enumerate(frames):
        b = f.resize((w * s, h * s), Image.NEAREST)
        strip.paste(b, (8 + i * (w * s + 8), 12), b)
        d.text((8 + i * (w * s + 8), 0), f"frame {i}", fill=(230, 230, 230))
    strip.save(os.path.join(run, out))

print(f"{run}: {len(frames)} frames {w}x{h}")
print(f"  gif clears between frames: {'yes' if gif_ok else 'NO'}")
for n, sz, c, hl, o, t, ft in rows:
    print(f"  {n} size {sz} clip {c or 'none'} halo {hl} opaque {o} moved {t} (feet region {ft})")

html = f"""<!doctype html><meta charset=utf-8><title>{run}</title>
<style>body{{background:#3c3c3c;color:#ddd;font:12px sans-serif;padding:16px}}img{{image-rendering:pixelated;display:block;margin-bottom:12px}}</style>
<h2>{run}</h2>
<img src="/{run}/preview.gif">
<img src="/{run}/strip1x.png">
<img src="/{run}/strip{scale}x.png" style="max-width:100%">
<pre>{chr(10).join(f'{n} clip {c or "none"} halo {hl} opaque {o} moved {t} feet {ft}' for n, sz, c, hl, o, t, ft in rows)}
gif clears between frames: {'yes' if gif_ok else 'NO'}</pre>
"""
open(os.path.join(run, "review.html"), "w").write(html)
print(f"  page: {run}/review.html")
